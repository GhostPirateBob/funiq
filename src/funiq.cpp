#include <algorithm>
#include <functional>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define STDERR_FD 2
#else
#include <unistd.h>
#define STDERR_FD STDERR_FILENO
#endif

#include "tclap/CmdLine.h"
#include "funiq/Settings.h"
#include "funiq/Matcher.h"

// Custom help output that replaces TCLAP's verbose default with a compact,
// man-page-style layout.
class FuniqOutput : public TCLAP::CmdLineOutput {
public:
	virtual void version(TCLAP::CmdLineInterface& cmd) {
		std::cout << cmd.getProgramName() << " " << cmd.getVersion() << std::endl;
	}

	virtual void usage(TCLAP::CmdLineInterface& cmd) {
		std::cout
			<< "funiq - Fuzzy unique line filtering (like uniq, but fuzzy)\n"
			<< "\n"
			<< "Usage: funiq [options] [filename]\n"
			<< "\n"
			<< "Options:\n"
			<< "  -d <n>     Max distance threshold (default: 3)\n"
			<< "  -m <name>  Comparison method: levenshtein (default), normalized-levenshtein\n"
			<< "  -i         Case-insensitive matching\n"
			<< "  -I         Ignore non-alphanumeric characters\n"
			<< "  -c         Show duplicate counts (like uniq -c)\n"
			<< "  -a         Show all duplicates, not just the representative\n"
			<< "  --default  Shorthand for -iI -m normalized-levenshtein -d 0.125\n"
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
			<< "  grep error log.txt | funiq -iI -d 5 | sort            Combine with other tools\n";
	}

	virtual void failure(TCLAP::CmdLineInterface& cmd, TCLAP::ArgException& e) {
		std::cerr << "Error: " << e.error() << " for arg " << e.argId() << "\n"
		          << "Try: " << cmd.getProgramName() << " -h\n";
		throw TCLAP::ExitException(1);
	}
};

void parseCommandLine(int argc, char** argv, std::string& filename, Settings& settings) {

	// Strip directory path from argv[0] so --help and --version display
	// "funiq" instead of "bin/funiq" or a full path.
	std::string progName = argv[0];
	std::string::size_type pos = progName.find_last_of("/\\");
	if(pos != std::string::npos)
		progName = progName.substr(pos + 1);
	argv[0] = const_cast<char*>(progName.c_str());

	TCLAP::CmdLine cmd("", ' ', "0.4.2");

	// Replace TCLAP's default verbose output with our compact format
	FuniqOutput customOutput;
	cmd.setOutput(&customOutput);

	TCLAP::UnlabeledValueArg<std::string> filenameArg (
		"filename",
		"File to read. If omitted will read from stdin.",
		false, "", "filename");
	TCLAP::ValueArg<float> distanceArg(
		"d","distance",
		"Max distance threshold (default: 3)",
		false, 3, "number");
	TCLAP::SwitchArg caseSwitch(
		"i","case-insensitive",
		"Case-insensitive matching");
	TCLAP::SwitchArg showAllSwitch(
		"a","show-all",
		"Show all duplicates");
	TCLAP::SwitchArg showTotalsSwitch(
		"c","show-counts",
		"Show duplicate counts");
	TCLAP::SwitchArg ignoreNonAlphaNumericSwitch(
		"I","ignore-non-alpha-numeric",
		"Ignore non-alphanumeric characters");
	TCLAP::SwitchArg defaultSwitch(
		"","default",
		"Shorthand for -iI -m normalized-levenshtein -d 0.125");

	std::vector<std::string> allowedComparisonMethods;
	allowedComparisonMethods.push_back("levenshtein");
	allowedComparisonMethods.push_back("normalized-levenshtein");
	TCLAP::ValuesConstraint<std::string> comparisonMethodsConstraint(allowedComparisonMethods);

	TCLAP::ValueArg<std::string> comparisonMethodArg(
			"m","method",
			"Comparison method (default: levenshtein)",
			false, "levenshtein", &comparisonMethodsConstraint);

	cmd.add(filenameArg);
	cmd.add(comparisonMethodArg);
	cmd.add(distanceArg);
	cmd.add(caseSwitch);
	cmd.add(showAllSwitch);
	cmd.add(showTotalsSwitch);
	cmd.add(ignoreNonAlphaNumericSwitch);
	cmd.add(defaultSwitch);
	cmd.parse(argc, argv);

	// --default sets sensible defaults for general-purpose fuzzy matching
	if(defaultSwitch.getValue()) {
		settings.caseInsensitive = true;
		settings.ignoreNonAlphaNumeric = true;
		settings.comparisonMethod = NormalizedLevenshtein;
		settings.maxDistance = 0.125;
	} else {
		settings.comparisonMethod = Levenshtein;
	}

	// Explicit flags override --default
	if(caseSwitch.isSet()) settings.caseInsensitive = caseSwitch.getValue();
	if(ignoreNonAlphaNumericSwitch.isSet()) settings.ignoreNonAlphaNumeric = ignoreNonAlphaNumericSwitch.getValue();
	if(distanceArg.isSet()) settings.maxDistance = distanceArg.getValue();
	if(comparisonMethodArg.isSet()) {
		std::string comparisonMethod = comparisonMethodArg.getValue();
		if(comparisonMethod == "levenshtein") settings.comparisonMethod = Levenshtein;
		if(comparisonMethod == "normalized-levenshtein") settings.comparisonMethod = NormalizedLevenshtein;
	}

	settings.showAllMatches = showAllSwitch.getValue();
	settings.showTotals = showTotalsSwitch.getValue();

	filename = filenameArg.getValue();
}

// Read from file if a filename is provided, otherwise read from stdin.
// Returns a file stream if applicable, leaving the caller to manage its lifetime.
std::ifstream getFileInput(const std::string& filename) {
	return std::ifstream(filename.c_str());
}

int main(int argc, char* argv[]) {

	// Exit quietly on broken pipe (e.g. funiq file.txt | head) instead
	// of printing an error or crashing.
#ifndef _WIN32
	std::signal(SIGPIPE, SIG_DFL);
#endif

	// Disable C/C++ I/O synchronization for faster stdin/stdout throughput
	// when reading large inputs or piping output.
	std::ios_base::sync_with_stdio(false);
	std::cin.tie(nullptr);

	try {

		std::string filename;
		Settings settings;
		parseCommandLine(argc, argv, filename, settings);

		Matcher matcher(settings);
		// Use file stream if filename provided, otherwise read from stdin
		std::ifstream fileStream;
		if(!filename.empty()) {
			fileStream = getFileInput(filename);
		}
		std::istream& inputStream = filename.empty() ? std::cin : fileStream;

		// Show progress on stderr when it's a terminal. This keeps the
		// progress indicator visible even when stdout is piped or
		// redirected (e.g. funiq file.txt | tee out.txt).
		bool showProgress = isatty(STDERR_FD);
		unsigned long lineCount = 0;
		unsigned long totalLines = 0;

		// For file input, count total lines first so we can show a
		// percentage progress bar. Seek back to the start afterwards.
		if(showProgress && !filename.empty()) {
			std::string tmp;
			while(getline(inputStream, tmp)) totalLines++;
			fileStream.clear();
			fileStream.seekg(0);
		}

		auto startTime = std::chrono::steady_clock::now();

		for (std::string line; getline(inputStream, line); ) {
			matcher.add(line);
			lineCount++;
			if(showProgress && (lineCount % 50 == 0)) {
				auto now = std::chrono::steady_clock::now();
				double elapsed = std::chrono::duration<double>(now - startTime).count();
				double rate = (elapsed > 0) ? lineCount / elapsed : 0;

				std::cerr << "\rProcessing: ";
				if(totalLines > 0) {
					// File mode: show percentage bar
					int pct = (int)(lineCount * 100 / totalLines);
					int barWidth = 30;
					int filled = pct * barWidth / 100;
					std::cerr << pct << "%|";
					for(int b = 0; b < barWidth; b++)
						std::cerr << (b < filled ? "\xe2\x96\x88" : " ");
					std::cerr << "| " << lineCount << "/" << totalLines;
				} else {
					// Stdin mode: show line count only
					std::cerr << lineCount << " lines";
				}
				std::cerr << " [" << std::fixed << std::setprecision(1)
				          << rate << " lines/s]  " << std::flush;
			}
		}

		// Clear the progress line before outputting results
		if(showProgress && lineCount >= 50) {
			auto now = std::chrono::steady_clock::now();
			double elapsed = std::chrono::duration<double>(now - startTime).count();
			double rate = (elapsed > 0) ? lineCount / elapsed : 0;

			// Show completed state briefly
			std::cerr << "\rProcessing: ";
			if(totalLines > 0) {
				std::cerr << "100%|";
				for(int b = 0; b < 30; b++) std::cerr << "\xe2\x96\x88";
				std::cerr << "| " << lineCount << "/" << totalLines;
			} else {
				std::cerr << lineCount << " lines";
			}
			std::cerr << " [" << std::fixed << std::setprecision(1)
			          << rate << " lines/s]  \n" << std::flush;
		}

		matcher.show(&std::cout);
		// Explicitly flush stdout so downstream programs in a pipe chain
		// (e.g. funiq ... | bat) receive all output before we exit.
		std::cout.flush();

	} catch (TCLAP::ArgException &e) {
		std::cerr << "An error occurred: ";
		std::cerr << e.error() << " for arg " << e.argId() << std::endl;
		return 1;
	}

	return 0;
}
