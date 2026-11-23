#include <algorithm>
#include <functional>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX            // keep windows.h's min/max macros away from std::min
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <unistd.h>
#endif

#include "tclap/CmdLine.h"
#include "funiq/Color.h"
#include "funiq/Settings.h"
#include "funiq/Matcher.h"

// Colour for stderr: decided from the environment before parsing (so
// argument errors are coloured too), then again once --color is known.
static bool g_errColor = false;

#ifdef _WIN32
// Console modes as found, restored at exit so the shell's console is left as
// it was.
static HANDLE g_console[2] = { NULL, NULL };
static DWORD g_consoleMode[2] = { 0, 0 };
static void restoreConsoleModes() {
	for(int i = 0; i < 2; i++)
		if(g_console[i]) SetConsoleMode(g_console[i], g_consoleMode[i]);
}
#endif

// Whether fd 1 or 2 is an interactive terminal. On Windows that means a
// console (_isatty is also true for NUL), and the console must accept VT
// escape handling, which is switched on here, or colour codes print raw.
static bool isTerminal(int fd) {
#ifdef _WIN32
	HANDLE h = GetStdHandle(fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
	DWORD mode = 0;
	if(h == INVALID_HANDLE_VALUE || h == NULL || !GetConsoleMode(h, &mode)) return false;
	if(mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) return true;
	if(!SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) return false;
	if(!g_console[0] && !g_console[1]) std::atexit(restoreConsoleModes);
	g_console[fd - 1] = h;
	g_consoleMode[fd - 1] = mode;
	return true;
#else
	return isatty(fd);
#endif
}

static bool wantColor(color::Mode mode, bool terminal) {
	return color::want(mode, std::getenv("FORCE_COLOR"), std::getenv("NO_COLOR"),
	                   std::getenv("TERM"), terminal);
}

// Custom help output that replaces TCLAP's verbose default with a compact,
// man-page-style layout.
class FuniqOutput : public TCLAP::CmdLineOutput {
public:
	virtual void version(TCLAP::CmdLineInterface& cmd) {
		std::cout << cmd.getProgramName() << " " << cmd.getVersion() << std::endl;
	}

	virtual void usage(TCLAP::CmdLineInterface& /*cmd*/) {
		std::cout
			<< "funiq - Fuzzy unique line filtering (like uniq, but fuzzy)\n"
			<< "\n"
			<< "Usage: funiq [options] [filename]\n"
			<< "\n"
			<< "Options:\n"
			<< "  -d <n>     Max distance threshold (default: 3 edits for levenshtein,\n"
			<< "             0.125 for normalized-levenshtein and token-sort, 0.05 for cosine)\n"
			<< "  -m <name>  Comparison method:\n"
			<< "               levenshtein             edit distance (default)\n"
			<< "               normalized-levenshtein  edits / longer length (0.0-1.0)\n"
			<< "               token-sort              normalized, ignoring word order\n"
			<< "               cosine                  1 - trigram cosine similarity (0.0-1.0)\n"
			<< "  -i         Case-insensitive matching\n"
			<< "  -I         Ignore non-alphanumeric characters (word separators for token-sort)\n"
			<< "  -D         Mask digits: each run of digits compares as a single 0\n"
			<< "  -c         Show duplicate counts (like uniq -c)\n"
			<< "  -a         Show all duplicates, not just the representative\n"
			<< "  --scores   Show each duplicate's distance from its group (implies -a)\n"
			<< "  --best     Join the closest group instead of the first within range\n"
			<< "  --rep <r>  Group representative: first (default) or longest\n"
			<< "  --default  Shorthand for -iI -m normalized-levenshtein -d 0.125\n"
			<< "             (a bare -d, not followed by a number, means the same)\n"
			<< "  -j <n>     Worker threads (default: all CPU cores; output is identical)\n"
			<< "  --color <when>  auto (default: when output is a terminal), always, never;\n"
			<< "             NO_COLOR and FORCE_COLOR are honoured\n"
			<< "  -h         Show this help\n"
			<< "  --version  Show version\n"
			<< "\n"
			<< "  If no filename is given, reads from stdin.\n"
			<< "\n"
			<< "Examples:\n"
			<< "  funiq names.txt                                        Basic deduplication\n"
			<< "  cat names.txt | funiq -iI                              Pipe, ignore case & symbols\n"
			<< "  funiq -iI -d 5 names.txt                               Higher edit distance\n"
			<< "  funiq -iI -m normalized-levenshtein -d 0.125 file.txt  Length-independent matching\n"
			<< "  funiq -iI -c names.txt                                 Count duplicates\n"
			<< "  grep error log.txt | funiq -iI -d 5 | sort            Combine with other tools\n"
			<< "  funiq --default -D -c error.log                        Logs: ignore PIDs, times, IPs\n";
	}

	virtual void failure(TCLAP::CmdLineInterface& cmd, TCLAP::ArgException& e) {
		std::cerr << color::paint(g_errColor, color::BoldRed, "Error:") << " "
		          << e.error() << " for arg " << e.argId() << "\n"
		          << "Try: " << cmd.getProgramName() << " -h\n";
		throw TCLAP::ExitException(1);
	}
};

void parseCommandLine(int argc, char** argv, std::string& filename, Settings& settings,
                      color::Mode& colorMode) {

	// Strip directory path from argv[0] so --help and --version display
	// "funiq" instead of "bin/funiq" or a full path.
	std::string progName = argv[0];
	std::string::size_type pos = progName.find_last_of("/\\");
	if(pos != std::string::npos)
		progName = progName.substr(pos + 1);

	// Rewrites before TCLAP sees the arguments (everything after a bare "--"
	// is passed through untouched):
	//  - "--opt=value" becomes "--opt value": TCLAP only takes the latter, so
	//    this adds the GNU spelling (--color=never, --rep=longest)
	//  - a bare "-d" not followed by a number means the --default preset, so
	//    `funiq -d log.txt` works while `-d 0.125` still sets the distance
	auto isNumber = [](const std::string& s) {
		if(s.empty()) return false;
		char* end = nullptr;
		std::strtod(s.c_str(), &end);
		return *end == '\0';
	};
	std::vector<std::string> args(1, progName);
	std::vector<std::string> options; // the arguments before any "--"
	bool literal = false;
	for(int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if(!literal && arg != "--") options.push_back(arg);
		std::string::size_type eq = arg.find('=');
		if(!literal && arg.compare(0, 2, "--") == 0 && eq != std::string::npos && eq > 2) {
			args.push_back(arg.substr(0, eq));
			args.push_back(arg.substr(eq + 1));
		} else if(!literal && arg == "-d" && !(i + 1 < argc && isNumber(argv[i + 1]))) {
			args.push_back("--default");
		} else {
			if(arg == "--") literal = true;
			args.push_back(arg);
		}
	}

	TCLAP::CmdLine cmd("", ' ', "1.3.1");

	// Replace TCLAP's default verbose output with our compact format
	FuniqOutput customOutput;
	cmd.setOutput(&customOutput);

	// Option descriptions here only surface in TCLAP's own error messages;
	// --help prints FuniqOutput::usage above. Flag names are the stable CLI.
	const std::vector<std::pair<std::string, ComparisonMethod>> methods = {
		{ "levenshtein", Levenshtein }, { "normalized-levenshtein", NormalizedLevenshtein },
		{ "token-sort", TokenSort }, { "cosine", Cosine } };
	std::vector<std::string> methodNames, repNames = { "first", "longest" },
		colorNames = { "auto", "always", "never" };
	for(const auto& m : methods) methodNames.push_back(m.first);
	TCLAP::ValuesConstraint<std::string> methodChoices(methodNames), repChoices(repNames),
		colorChoices(colorNames);

	TCLAP::UnlabeledValueArg<std::string> inputFile("filename", "Input file (stdin when omitted)", false, "", "file");
	TCLAP::ValueArg<std::string> method("m", "method", "Comparison method", false, "levenshtein", &methodChoices);
	TCLAP::ValueArg<float> threshold("d", "distance", "Distance threshold", false, 3, "n");
	// int, not unsigned: an unsigned stream read wraps "-1" to UINT_MAX.
	TCLAP::ValueArg<int> workers("j", "threads", "Worker threads", false, 0, "n");
	TCLAP::ValueArg<std::string> representative("", "rep", "Group representative", false, "first", &repChoices);
	TCLAP::ValueArg<std::string> colorWhen("", "color", "Colour output", false, "auto", &colorChoices);
	TCLAP::SwitchArg foldCase("i", "case-insensitive", "Ignore case");
	TCLAP::SwitchArg dropPunct("I", "ignore-non-alpha-numeric", "Ignore non-alphanumerics");
	TCLAP::SwitchArg maskDigits("D", "mask-digits", "Mask runs of digits");
	TCLAP::SwitchArg counts("c", "show-counts", "Prefix each group with its size");
	TCLAP::SwitchArg everyMember("a", "show-all", "Print every member of each group");
	TCLAP::SwitchArg scores("", "scores", "Print each member's distance");
	TCLAP::SwitchArg closest("", "best", "Join the closest group");
	TCLAP::SwitchArg preset("", "default", "Preset: -iI -m normalized-levenshtein -d 0.125");
	TCLAP::Arg* const all[] = { &inputFile, &method, &threshold, &workers, &representative, &colorWhen,
		&foldCase, &dropPunct, &maskDigits, &counts, &everyMember, &scores, &closest, &preset };
	for(TCLAP::Arg* arg : all) cmd.add(arg);
	const std::vector<std::string> rewritten = args; // parse() consumes args
	cmd.parse(args);

	// Errors found after parsing, reported like TCLAP's own and with its exit.
	auto reject = [&](const std::string& message, const std::string& id) {
		TCLAP::CmdLineParseException e(message, id);
		try { customOutput.failure(cmd, e); }
		catch(TCLAP::ExitException& ee) { std::exit(ee.getExitStatus()); }
	};
	// TCLAP hands any unmatched argument to the filename, so an unknown option
	// would surface as "-x: No such file or directory". Before "--", nothing
	// starting with '-' is a filename.
	const std::string& file = inputFile.getValue();
	if(file.size() > 1 && file[0] == '-' && std::find(options.begin(), options.end(), file) != options.end())
		reject("Couldn't find match for argument", file);
	// TCLAP reads an empty number as no value at all and keeps the default
	// (so `--distance= -m cosine` ran at 3 and merged every line), and skips
	// leading whitespace; both are typos, not numbers.
	for(size_t i = 1; i + 1 < rewritten.size() && rewritten[i] != "--"; i++) {
		const std::string& opt = rewritten[i];
		const std::string& value = rewritten[i + 1];
		if((opt == "-d" || opt == "--distance" || opt == "-j" || opt == "--threads")
		   && (value.empty() || std::isspace((unsigned char)value[0])))
			reject("Couldn't read argument value from string '" + value + "'", opt);
	}
	if(workers.getValue() < 0)
		reject("Value '" + std::to_string(workers.getValue()) + "' does not meet constraint: 0 or more", workers.toString());

	colorMode = colorWhen.getValue() == "always" ? color::Always
	          : colorWhen.getValue() == "never" ? color::Never : color::Auto;

	settings.threads = workers.getValue() ? (unsigned)workers.getValue() : std::thread::hardware_concurrency();
	if(settings.threads == 0) settings.threads = 1; // hardware_concurrency() may not know

	// The preset first; explicit flags then refine it.
	if(preset.getValue()) {
		settings.caseInsensitive = settings.ignoreNonAlphaNumeric = true;
		settings.comparisonMethod = NormalizedLevenshtein;
	}
	if(foldCase.getValue()) settings.caseInsensitive = true;
	if(dropPunct.getValue()) settings.ignoreNonAlphaNumeric = true;
	if(method.isSet())
		for(const auto& m : methods)
			if(m.first == method.getValue()) settings.comparisonMethod = m.second;

	// The default threshold depends on the method: 3 edits is meaningless
	// as a 0.0-1.0 ratio (everything would match). 0.125 is the tuned
	// normalized preset. Cosine's 0.05 is deliberately tight and not yet
	// tuned on real data: on nginx access logs 0.2 already merged different
	// request paths and status codes, because long shared User-Agent strings
	// dominate the trigram vector.
	if(threshold.isSet()) settings.maxDistance = threshold.getValue();
	else if(settings.comparisonMethod == Levenshtein) settings.maxDistance = 3;
	else if(settings.comparisonMethod == Cosine) settings.maxDistance = 0.05;
	else settings.maxDistance = 0.125;

	settings.maskDigits = maskDigits.getValue();
	settings.bestMatch = closest.getValue();
	settings.showTotals = counts.getValue();
	settings.showAllMatches = everyMember.getValue();
	settings.showScores = scores.getValue();
	settings.representative = representative.getValue() == "longest" ? RepLongest : RepFirst;

	filename = inputFile.getValue();
}

// Read from file if a filename is provided, otherwise read from stdin.
// Returns a file stream if applicable, leaving the caller to manage its lifetime.
// Input and output go through 1 MiB buffers. iostreams buffer only BUFSIZ
// bytes, which is 512 on MinGW: a read or write system call every 512 bytes.
static const size_t IO_BUFFER = 1 << 20;

// Splits input into lines with memchr over large binary reads. Binary mode
// matters on Windows, where text mode ends input at the first 0x1A (Ctrl-Z)
// byte; a trailing \r is dropped there instead, exactly what text mode's
// CRLF -> LF did. Elsewhere the \r stays part of the line, like uniq.
class LineReader {
public:
	explicit LineReader(std::FILE* f) : file(f), buf(IO_BUFFER) {}
	// The next line without its '\n'. Like getline, a last line without a
	// '\n' still counts. False at the end of the input.
	bool next(std::string& line) {
		line.clear();
		for(bool partial = false; ; partial = true) {
			if(pos == end) {
				end = std::fread(&buf[0], 1, buf.size(), file);
				pos = 0;
				if(end == 0) {
					if(!partial) return false;
					break;
				}
			}
			const char* start = &buf[pos];
			const char* nl = static_cast<const char*>(std::memchr(start, '\n', end - pos));
			const size_t n = nl ? (size_t)(nl - start) : end - pos;
			line.append(start, n);
			pos += n + (nl ? 1 : 0);
			consumed += n + (nl ? 1 : 0);
			if(nl) break;
		}
#ifdef _WIN32
		if(!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
#endif
		return true;
	}
	unsigned long long bytesRead() const { return consumed; }
	bool failed() const { return std::ferror(file) != 0; }
private:
	std::FILE* file;
	std::vector<char> buf;
	size_t pos = 0, end = 0;
	unsigned long long consumed = 0;
};

// The input's size in bytes when it is a regular file (also for `< file`),
// for the progress percentage; 0 for pipes, FIFOs and terminals.
static unsigned long long regularFileSize(std::FILE* f) {
#ifdef _WIN32
	struct _stat64 st;
	return _fstat64(_fileno(f), &st) == 0 && (st.st_mode & _S_IFMT) == _S_IFREG ? st.st_size : 0;
#else
	struct stat st;
	return fstat(fileno(f), &st) == 0 && S_ISREG(st.st_mode) ? st.st_size : 0;
#endif
}

// A std::streambuf that writes to a FILE through a 1 MiB buffer, so the
// results reach stdout in a few large writes.
class FileOutBuf : public std::streambuf {
public:
	explicit FileOutBuf(std::FILE* f) : file(f), buf(IO_BUFFER) { setp(&buf[0], &buf[0] + buf.size()); }
	~FileOutBuf() { flushBuffer(); }
protected:
	int_type overflow(int_type c) override {
		if(!flushBuffer()) return traits_type::eof();
		if(!traits_type::eq_int_type(c, traits_type::eof())) {
			*pptr() = traits_type::to_char_type(c);
			pbump(1);
		}
		return traits_type::not_eof(c);
	}
	int sync() override { return flushBuffer() && std::fflush(file) == 0 ? 0 : -1; }
private:
	std::FILE* file;
	std::vector<char> buf;
	bool flushBuffer() {
		const size_t n = pptr() - pbase();
		const bool ok = n == 0 || std::fwrite(pbase(), 1, n, file) == n;
		setp(&buf[0], &buf[0] + buf.size());
		return ok;
	}
};

int main(int argc, char* argv[]) {

	// Exit quietly on broken pipe (e.g. funiq file.txt | head) instead
	// of printing an error or crashing.
#ifndef _WIN32
	std::signal(SIGPIPE, SIG_DFL);
#else
	// Text-mode stdout turns every '\n' into "\r\n" on Windows, which leaks
	// a stray \r into downstream sed/awk/bat pipelines. Emit LF like uniq.
	_setmode(_fileno(stdout), _O_BINARY);
	// Text-mode stdin ends input at the first 0x1A (Ctrl-Z) byte, which
	// silently dropped the rest of a log. Read bytes; see LineReader for CRLF.
	_setmode(_fileno(stdin), _O_BINARY);
#endif

	// Colour is decided once per stream (inside a pipe stdout isn't a
	// terminal, so asking per write would be wrong anyway).
	const bool stdoutTerminal = isTerminal(1);
	const bool stderrTerminal = isTerminal(2);
	g_errColor = wantColor(color::Auto, stderrTerminal);

	try {

		std::string filename;
		Settings settings;
		color::Mode colorMode = color::Auto;
		parseCommandLine(argc, argv, filename, settings, colorMode);
		g_errColor = wantColor(colorMode, stderrTerminal);
		settings.color = wantColor(colorMode, stdoutTerminal);

		Matcher matcher(settings);
		std::FILE* input = stdin;
		if(!filename.empty()) {
			input = std::fopen(filename.c_str(), "rb");
			if(!input) {
				std::cerr << color::paint(g_errColor, color::BoldRed, "funiq:") << " "
				          << filename << ": " << std::strerror(errno) << "\n";
				return 1;
			}
		}
		LineReader reader(input);

		// Show progress on stderr when it's a terminal. This keeps the
		// progress indicator visible even when stdout is piped or
		// redirected (e.g. funiq file.txt | tee out.txt).
		// isTerminal, not isatty: on Windows _isatty is also true for NUL.
		const bool showProgress = stderrTerminal;
		unsigned long lineCount = 0;
		// The percentage comes from bytes read out of the file's size, so the
		// input is read once (it used to be read twice to count lines first).
		// Pipes and FIFOs have no size and show a line count only.
		const unsigned long long totalBytes = showProgress ? regularFileSize(input) : 0;

		auto startTime = std::chrono::steady_clock::now();
		auto lastDraw = startTime;

		auto drawProgress = [&](bool done) {
				auto now = std::chrono::steady_clock::now();
				double elapsed = std::chrono::duration<double>(now - startTime).count();
				double rate = (elapsed > 0) ? lineCount / elapsed : 0;

				const bool c = g_errColor;
				std::cerr << "\r" << color::paint(c, color::Dim, "Processing:") << " ";
				if(totalBytes > 0) {
					const int pct = done ? 100
						: (int)std::min<unsigned long long>(100, reader.bytesRead() * 100 / totalBytes);
					const int barWidth = 30;
					const int filled = pct * barWidth / 100;
					std::string bar;
					for(int b = 0; b < barWidth; b++)
						bar += (b < filled ? "\xe2\x96\x88" : " ");
					std::cerr << color::paint(c, color::Bold, std::to_string(pct) + "%")
					          << "|" << color::paint(c, done ? color::Green : color::Cyan, bar) << "| ";
				}
				std::cerr << lineCount << " lines";
				std::ostringstream rateText;
				rateText << "[" << std::fixed << std::setprecision(1) << rate << " lines/s]";
				std::cerr << " " << color::paint(c, color::Dim, rateText.str()) << (done ? "  \n" : "  ")
				          << std::flush;
		};

		// Lines are matched in batches so each batch's scan against existing
		// groups can run on several threads (Matcher::addBatch gives the same
		// result as adding lines one by one). Larger batches give threads
		// more work per round; smaller ones keep the serial in-batch check
		// cheap when nearly every line starts a new group.
		const size_t BATCH_SIZE = 256;
		std::vector<std::string> batch;
		batch.reserve(BATCH_SIZE);
		for(std::string line; ; ) {
			bool more = reader.next(line);
			if(more) batch.push_back(std::move(line)); // next() clears it
			if(!batch.empty() && (batch.size() == BATCH_SIZE || !more)) {
				lineCount += batch.size();
				matcher.addBatch(std::move(batch), settings.threads);
				batch.clear(); // moved from: make it empty and usable again
				batch.reserve(BATCH_SIZE);
				// Small inputs finish before a bar would mean anything, and the
				// final newline below is only printed from 50 lines on. At most
				// 10 redraws a second: memo-heavy input runs thousands of
				// batches a second, and every redraw is a terminal write.
				if(showProgress && lineCount >= 50) {
					auto now = std::chrono::steady_clock::now();
					if(now - lastDraw >= std::chrono::milliseconds(100)) {
						drawProgress(false);
						lastDraw = now;
					}
				}
			}
			if(!more) break;
		}
		if(reader.failed()) { // e.g. a directory on Linux: fopen works, reading doesn't
			std::cerr << color::paint(g_errColor, color::BoldRed, "funiq:") << " "
			          << (filename.empty() ? "stdin" : filename) << ": " << std::strerror(errno) << "\n";
			return 1;
		}
		if(input != stdin) std::fclose(input);

		// The completed bar, ending the progress line before the results.
		if(showProgress && lineCount >= 50) drawProgress(true);

		// Flushed before exit so downstream programs in a pipe chain
		// (e.g. funiq ... | bat) receive all output.
		FileOutBuf outBuf(stdout);
		std::ostream out(&outBuf);
		matcher.show(&out);
		out.flush();

	} catch (TCLAP::ArgException& e) {
		std::cerr << color::paint(g_errColor, color::BoldRed, "funiq:") << " "
		          << e.error() << " (" << e.argId() << ")\n";
		return 1;
	}

	return 0;
}
