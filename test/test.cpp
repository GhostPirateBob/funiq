#define CATCH_CONFIG_MAIN
#include <iostream>
#include <random>

#include "catch/catch.hpp"
#include "funiq/Settings.h"
#include "funiq/Matcher.h"
#include "funiq/similarity.h"
#include "funiq/Color.h"

// show() output as one string per line.
static std::vector<std::string> showLines(Matcher& matcher) {
	std::stringstream output;
	matcher.show(&output);
	std::vector<std::string> out;
	for(std::string line; std::getline(output, line); ) out.push_back(line);
	return out;
}

// The full (n+1) x (m+1) table, kept as plain as possible: an independent
// reference for the optimised implementations in similarity.h.
static unsigned int referenceLevenshtein(const std::string& a, const std::string& b) {
	std::vector<std::vector<unsigned int>> d(a.size() + 1, std::vector<unsigned int>(b.size() + 1));
	for(size_t i = 0; i <= a.size(); i++) d[i][0] = i;
	for(size_t j = 0; j <= b.size(); j++) d[0][j] = j;
	for(size_t i = 1; i <= a.size(); i++)
		for(size_t j = 1; j <= b.size(); j++)
			d[i][j] = std::min({ d[i - 1][j] + 1, d[i][j - 1] + 1,
			                     d[i - 1][j - 1] + (a[i - 1] == b[j - 1] ? 0u : 1u) });
	return d[a.size()][b.size()];
}

static const char* const SHIPS[] = {
	"USS Enterprise", "uss enterprize", "U.S.S. Enterprise", "Enterprize",
	"USS Voyager", "USS Voyagr", "uss-voyager",
	"USS Defiant", "USS Defient", "Defiant" };

TEST_CASE( "README example: -iI groups the misspelt ships, -d 4 also catches Enterprize", "[matcher]" ) {
	for(float d : {3.0f, 4.0f}) {
		Settings settings;
		settings.caseInsensitive = true;
		settings.ignoreNonAlphaNumeric = true;
		settings.maxDistance = d;
		Matcher matcher(settings);
		for(const char* ship : SHIPS) matcher.add(ship);
		INFO("d=" << d);
		// "Enterprize" is 4 edits from "ussenterprise" once case and punctuation go
		REQUIRE(showLines(matcher) == (d < 4
			? std::vector<std::string>{ "Enterprize", "USS Defiant", "USS Enterprise", "USS Voyager" }
			: std::vector<std::string>{ "USS Defiant", "USS Enterprise", "USS Voyager" }));
	}
}

TEST_CASE( "normalized threshold scales with length", "[matcher]" ) {
	Settings settings;
	settings.comparisonMethod = NormalizedLevenshtein;
	settings.maxDistance = 0.25f;
	settings.showTotals = true;
	Matcher matcher(settings);
	matcher.add("ab");     // "ac" is 1 edit in 2 chars (0.5): too far
	matcher.add("ac");
	matcher.add("abcd");   // "abce" is 1 edit in 4 chars (0.25): close enough
	matcher.add("abce");
	REQUIRE(showLines(matcher) == std::vector<std::string>{ "      1 ab", "      2 abcd", "      1 ac" });
}

TEST_CASE( "similarity::levenshteinDistance and normalizedLevenshtein values", "[levenshtein]" ) {
	REQUIRE(similarity::levenshteinDistance("kitten", "sitting") == 3);
	REQUIRE(similarity::levenshteinDistance("flaw", "lawn") == 2);
	REQUIRE(similarity::levenshteinDistance("", "abc") == 3);
	REQUIRE(similarity::levenshteinDistance("same", "same") == 0);
	REQUIRE(similarity::normalizedLevenshtein("abcd", "abce") == 0.25f);
	REQUIRE(similarity::normalizedLevenshtein("ab", "ba") == 1.0f);
	REQUIRE(similarity::normalizedLevenshtein("", "") == 0.0f);
}

TEST_CASE( "similarity::boundedLevenshtein is exact up to k, else k+1", "[levenshtein]" ) {
	REQUIRE(similarity::boundedLevenshtein("kitten", "sitting", 3) == 3);
	REQUIRE(similarity::boundedLevenshtein("kitten", "sitting", 2) == 3); // over cutoff -> k+1
	REQUIRE(similarity::boundedLevenshtein("", "abc", 5) == 3);
	REQUIRE(similarity::boundedLevenshtein("abc", "", 1) == 2);          // length bound -> k+1
	REQUIRE(similarity::boundedLevenshtein("same", "same", 0) == 0);
	REQUIRE(similarity::boundedLevenshtein("ab", "ba", 0) == 1);
	REQUIRE(similarity::boundedLevenshtein("prefix-x-suffix", "prefix-yy-suffix", 2) == 2);
}

TEST_CASE( "levenshteinDistance and boundedLevenshtein agree with the reference table", "[levenshtein]" ) {
	// Small alphabet => lots of shared prefixes/suffixes and near-misses,
	// which exercises the affix strip, the band edges and the early abort.
	std::mt19937 rng(20261113);
	auto randomString = [&rng]() {
		std::string s(rng() % 25, 'a');
		for(auto& c : s) c = "abc"[rng() % 3];
		return s;
	};
	for(int iter = 0; iter < 20000; iter++) {
		std::string a = randomString(), b = randomString();
		unsigned int k = rng() % 13;
		unsigned int full = referenceLevenshtein(a, b);
		unsigned int want = full <= k ? full : k + 1;
		INFO("a=\"" << a << "\" b=\"" << b << "\" k=" << k);
		REQUIRE(similarity::levenshteinDistance(a, b) == full);
		REQUIRE(similarity::boundedLevenshtein(a, b, k) == want);
	}
}

TEST_CASE( "a repeated line joins the same group as its first occurrence", "[matcher]" ) {
	Settings settings;
	settings.maxDistance = 1;
	settings.showAllMatches = true;

	Matcher matcher(settings);
	matcher.add("aaaa");
	matcher.add("zzzz");
	matcher.add("aaab");
	matcher.add("zzzz");
	matcher.add("aaab");
	REQUIRE(showLines(matcher) == std::vector<std::string>{ "aaaa\taaab\taaab", "zzzz\tzzzz" });
}

TEST_CASE( "show-all prints duplicates as originally written, not normalized", "[matcher]" ) {
	Settings settings;
	settings.caseInsensitive = true;
	settings.ignoreNonAlphaNumeric = true;
	settings.showAllMatches = true;

	Matcher matcher(settings);
	matcher.add("Hello, World!");
	matcher.add("HELLO WORLD");
	REQUIRE(showLines(matcher) == std::vector<std::string>{ "Hello, World!\tHELLO WORLD" });
}

TEST_CASE( "a line joins only the first group it matches", "[matcher]" ) {
	Settings settings;
	settings.maxDistance = 1;
	settings.showTotals = true;

	Matcher matcher(settings);
	matcher.add("aa");
	matcher.add("bb");
	matcher.add("ab"); // distance 1 from both "aa" and "bb"
	// Counts must sum to the number of input lines (3), not 4
	REQUIRE(showLines(matcher) == std::vector<std::string>{ "      2 aa", "      1 bb" });
}

static std::string runMatcher(Settings settings, const std::vector<std::string>& lines,
                              unsigned threads, std::mt19937* splitRng) {
	Matcher matcher(settings);
	if(threads == 0) {
		for(const auto& line : lines) matcher.add(line);
	} else {
		// Feed addBatch randomly sized chunks, including empty ones.
		size_t pos = 0;
		while(pos < lines.size()) {
			size_t n = std::min(lines.size() - pos, (size_t)((*splitRng)() % 40));
			std::vector<std::string> chunk(lines.begin() + pos, lines.begin() + pos + n);
			matcher.addBatch(chunk, threads);
			pos += n;
		}
	}
	std::stringstream output;
	matcher.show(&output);
	return output.str();
}

TEST_CASE( "addBatch with threads gives the same groups as serial add", "[matcher][threads]" ) {
	std::mt19937 rng(424242);
	// Lines built from a few templates with small random edits, so there
	// are near-duplicates, exact repeats, and lines matching several groups.
	const char* templates[] = { "error at line ##", "warning: x## not found", "GET /a/## 200", "ok" };
	std::vector<std::string> lines;
	for(int i = 0; i < 600; i++) {
		std::string s = templates[rng() % 4];
		for(auto& c : s) if(c == '#') c = '0' + rng() % 10;
		if(rng() % 5 == 0 && !s.empty()) s[rng() % s.size()] = 'a' + rng() % 26;
		lines.push_back(s);
	}

	struct Case { ComparisonMethod method; float d; bool ci; bool best; bool mask; bool scores; };
	const Case cases[] = {
		{Levenshtein, 3, false, false, false, false}, {Levenshtein, 1, true, false, false, false},
		{NormalizedLevenshtein, 0.125f, false, false, false, false}, {NormalizedLevenshtein, 0.3f, true, false, false, false},
		{Levenshtein, 3, false, true, false, true}, {NormalizedLevenshtein, 0.3f, true, true, false, false},
		{Levenshtein, 2, false, false, true, false}, {TokenSort, 0.2f, false, false, false, true},
		{Cosine, 0.2f, false, false, false, true}, {Cosine, 0.3f, true, true, true, false} };
	for(const Case& c : cases) {
		Settings settings;
		settings.comparisonMethod = c.method;
		settings.maxDistance = c.d;
		settings.caseInsensitive = c.ci;
		settings.bestMatch = c.best;
		settings.maskDigits = c.mask;
		settings.showScores = c.scores;
		settings.showAllMatches = true;
		settings.showTotals = true;

		std::string serial = runMatcher(settings, lines, 0, &rng);
		for(unsigned threads : {1u, 2u, 3u, 8u}) {
			INFO("method=" << c.method << " d=" << c.d << " best=" << c.best << " threads=" << threads);
			REQUIRE(runMatcher(settings, lines, threads, &rng) == serial);
		}
	}
}

TEST_CASE( "--mask-digits: lines differing only in numbers become duplicates", "[tier3]" ) {
	Settings settings;
	settings.maxDistance = 0;
	settings.showTotals = true;
	settings.maskDigits = true;
	Matcher matcher(settings);
	matcher.add("pid 123 exited at 10:42:07");
	matcher.add("pid 98765 exited at 09:01:59");
	matcher.add("pid exited");
	REQUIRE(showLines(matcher) == std::vector<std::string>{
		"      2 pid 123 exited at 10:42:07", "      1 pid exited" });
}

TEST_CASE( "-m token-sort: word order does not matter", "[tier3]" ) {
	Settings settings;
	settings.comparisonMethod = TokenSort;
	settings.maxDistance = 0;
	settings.showTotals = true;
	Matcher matcher(settings);
	matcher.add("connection reset by peer");
	matcher.add("peer by reset connection");
	matcher.add("connection refused");
	REQUIRE(showLines(matcher) == std::vector<std::string>{
		"      1 connection refused", "      2 connection reset by peer" });
}

TEST_CASE( "-m token-sort with -I treats punctuation as a word separator", "[tier3]" ) {
	Settings settings;
	settings.comparisonMethod = TokenSort;
	settings.ignoreNonAlphaNumeric = true;
	settings.maxDistance = 0;
	settings.showTotals = true;
	Matcher matcher(settings);
	matcher.add("user=bob,ip=10.0.0.1");
	matcher.add("ip=10.0.0.1 user=bob");
	REQUIRE(showLines(matcher) == std::vector<std::string>{ "      2 user=bob,ip=10.0.0.1" });
}

TEST_CASE( "-m cosine: reordered fields match where edit distance does not", "[tier3]" ) {
	const char* a = "user=alice action=login status=ok";
	const char* b = "status=ok action=login user=alice";
	const char* c = "disk full on /dev/sda1";
	for(ComparisonMethod method : {NormalizedLevenshtein, Cosine}) {
		Settings settings;
		settings.comparisonMethod = method;
		settings.maxDistance = 0.3f;
		Matcher matcher(settings);
		matcher.add(a); matcher.add(b); matcher.add(c);
		size_t groups = showLines(matcher).size();
		INFO("method=" << method);
		REQUIRE(groups == (method == Cosine ? 2u : 3u));
	}
}

TEST_CASE( "similarity::cosineDistance basics", "[tier3]" ) {
	REQUIRE(similarity::cosineDistance(similarity::trigramProfile("abcdef"),
	                                   similarity::trigramProfile("abcdef")) == Approx(0.0));
	REQUIRE(similarity::cosineDistance(similarity::trigramProfile("abcdef"),
	                                   similarity::trigramProfile("uvwxyz")) == Approx(1.0));
	REQUIRE(similarity::cosineDistance(similarity::trigramProfile(""),
	                                   similarity::trigramProfile("")) == Approx(0.0));
	REQUIRE(similarity::cosineDistance(similarity::trigramProfile("ab"),
	                                   similarity::trigramProfile("ab")) == Approx(0.0));
}

TEST_CASE( "--rep longest: the longest member represents the group", "[tier3]" ) {
	Settings settings;
	settings.maxDistance = 3;
	settings.showTotals = true;
	settings.representative = RepLongest;
	Matcher matcher(settings);
	matcher.add("abc");
	matcher.add("abcdef");
	matcher.add("abcd");
	REQUIRE(showLines(matcher) == std::vector<std::string>{ "      3 abcdef" });
}

TEST_CASE( "--scores: each duplicate shows its distance from the group key", "[tier3]" ) {
	Settings settings;
	settings.maxDistance = 2;
	settings.showScores = true;
	Matcher matcher(settings);
	matcher.add("hello");
	matcher.add("hallo");
	matcher.add("help!");
	REQUIRE(showLines(matcher) == std::vector<std::string>{ "hello\thallo (1)\thelp! (2)" });

	Settings normalized;
	normalized.comparisonMethod = NormalizedLevenshtein;
	normalized.maxDistance = 0.5f;
	normalized.showScores = true;
	Matcher matcher2(normalized);
	matcher2.add("abcd");
	matcher2.add("abce");
	REQUIRE(showLines(matcher2) == std::vector<std::string>{ "abcd\tabce (0.250)" });
}

TEST_CASE( "--best: a line joins its closest group, not the first in range", "[tier3]" ) {
	for(bool best : {false, true}) {
		Settings settings;
		settings.maxDistance = 2;
		settings.showTotals = true;
		settings.bestMatch = best;
		Matcher matcher(settings);
		matcher.add("aaaaaa");
		matcher.add("aaabbb"); // distance 3 from "aaaaaa": new group
		matcher.add("aaaabb"); // distance 2 from "aaaaaa", 1 from "aaabbb"
		INFO("best=" << best);
		REQUIRE(showLines(matcher) == (best
			? std::vector<std::string>{ "      1 aaaaaa", "      2 aaabbb" }
			: std::vector<std::string>{ "      2 aaaaaa", "      1 aaabbb" }));
	}
}

TEST_CASE( "color::want follows --color, then FORCE_COLOR, NO_COLOR, TERM=dumb, tty", "[color]" ) {
	// --color always/never beats every environment variable
	REQUIRE(color::want(color::Always, "0", "1", "dumb", false));
	REQUIRE_FALSE(color::want(color::Never, "1", nullptr, "xterm", true));
	// auto: FORCE_COLOR comes first ("0"/"false" off, anything else on, "" unset)
	REQUIRE(color::want(color::Auto, "1", "1", "dumb", false));
	REQUIRE_FALSE(color::want(color::Auto, "0", nullptr, "xterm", true));
	REQUIRE_FALSE(color::want(color::Auto, "false", nullptr, "xterm", true));
	REQUIRE(color::want(color::Auto, "", nullptr, "xterm", true));
	// then NO_COLOR (any non-empty value, per no-color.org)
	REQUIRE_FALSE(color::want(color::Auto, nullptr, "1", "xterm", true));
	REQUIRE(color::want(color::Auto, nullptr, "", "xterm", true));
	// then TERM=dumb, then whether the stream is a terminal
	REQUIRE_FALSE(color::want(color::Auto, nullptr, nullptr, "dumb", true));
	REQUIRE(color::want(color::Auto, nullptr, nullptr, nullptr, true));
	REQUIRE_FALSE(color::want(color::Auto, nullptr, nullptr, "xterm", false));
}

static std::string stripAnsi(const std::string& s) {
	std::string out;
	for(size_t i = 0; i < s.size(); i++) {
		if(s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
			i += 2;
			while(i < s.size() && s[i] != 'm') i++;
			continue;
		}
		out += s[i];
	}
	return out;
}

TEST_CASE( "coloured output is exactly the plain output plus SGR codes", "[color]" ) {
	auto render = [](bool colored) {
		Settings settings;
		settings.maxDistance = 2;
		settings.showTotals = true;
		settings.showScores = true;
		settings.color = colored;
		Matcher matcher(settings);
		matcher.add("aaaaaa"); matcher.add("aaaabb");                  // count 2, scores 0 and 2
		for(int i = 0; i < 12; i++) matcher.add("cccccc");           // count 12
		for(int i = 0; i < 150; i++) matcher.add("dddddd");          // count 150
		matcher.add("zzzzzz");                                       // count 1
		std::stringstream out;
		matcher.show(&out);
		return out.str();
	};
	const std::string plain = render(false), colored = render(true);
	REQUIRE(plain.find('\x1b') == std::string::npos);
	REQUIRE(stripAnsi(colored) == plain);
	// counts are coloured by frequency: 1 gray, 2-9 green, 10-99 yellow, 100+ bold red
	REQUIRE(colored.find("\x1b[90m      1\x1b[39m zzzzzz") != std::string::npos);
	REQUIRE(colored.find("\x1b[32m      2\x1b[39m aaaaaa") != std::string::npos);
	REQUIRE(colored.find("\x1b[33m     12\x1b[39m cccccc") != std::string::npos);
	REQUIRE(colored.find("\x1b[1;31m    150\x1b[22;39m dddddd") != std::string::npos);
	// duplicates are dim; a score within half the threshold is green, else yellow
	REQUIRE(colored.find("\t\x1b[2maaaabb\x1b[22m \x1b[33m(2)\x1b[39m") != std::string::npos);
	REQUIRE(colored.find("\t\x1b[2mcccccc\x1b[22m \x1b[32m(0)\x1b[39m") != std::string::npos);
}
