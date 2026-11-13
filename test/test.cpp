#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include <iostream>
#include <random>

#include "catch/catch.hpp"
#include "funiq/Settings.h"
#include "funiq/Matcher.h"
#include "funiq/similarity.h"

std::string getNextLine(std::stringstream *stream) {
    std::string line;
    std::getline(*stream, line);
    return line;
}

TEST_CASE( "First README example is correct", "[matcher]" ) {
    Settings settings;
    settings.caseInsensitive = true;
    settings.ignoreNonAlphaNumeric = true;

    Matcher matcher(settings);

    matcher.add("The Fellowship of The Ring");
    matcher.add("the fellowship ofteh reing");
    matcher.add("The Flopship of the Rung");
    matcher.add("Felloship of the Ring");
    matcher.add("The Two Towers");
    matcher.add("The Twin Towers");
    matcher.add("the towers");
    matcher.add("The Return of the King");
    matcher.add("Return of the King");
    matcher.add("Teh return of theking");

    std::stringstream output;
    matcher.show(&output);

    REQUIRE(getNextLine(&output) == "Felloship of the Ring");
    REQUIRE(getNextLine(&output) == "The Fellowship of The Ring");
    REQUIRE(getNextLine(&output) == "The Flopship of the Rung");
    REQUIRE(getNextLine(&output) == "The Return of the King");
    REQUIRE(getNextLine(&output) == "The Two Towers");
    REQUIRE(getNextLine(&output) == "");
}

TEST_CASE( "Second README example is correct", "[matcher]" ) {
    Settings settings;
    settings.caseInsensitive = true;
    settings.ignoreNonAlphaNumeric = true;
    settings.maxDistance = 4;

    Matcher matcher(settings);

    matcher.add("The Fellowship of The Ring");
    matcher.add("the fellowship ofteh reing");
    matcher.add("The Flopship of the Rung");
    matcher.add("Felloship of the Ring");
    matcher.add("The Two Towers");
    matcher.add("The Twin Towers");
    matcher.add("the towers");
    matcher.add("The Return of the King");
    matcher.add("Return of the King");
    matcher.add("Teh return of theking");

    std::stringstream output;
    matcher.show(&output);

    REQUIRE(getNextLine(&output) == "The Fellowship of The Ring");
    REQUIRE(getNextLine(&output) == "The Return of the King");
    REQUIRE(getNextLine(&output) == "The Two Towers");
    REQUIRE(getNextLine(&output) == "");
}

TEST_CASE( "Normalised Levenshtein setting for short strings", "[matcher]" ) {
    Settings settings;
    settings.comparisonMethod = NormalizedLevenshtein;

	// _foo -> _ normalized distance is 0.75
	// _foo -> _moo normalized distance is 0.25
    settings.maxDistance = 0.26;

    Matcher matcher(settings);

    matcher.add("_");
    matcher.add("_foo");
    matcher.add("_moo");

    std::stringstream output;
    matcher.show(&output);

    REQUIRE(getNextLine(&output) == "_");
    REQUIRE(getNextLine(&output) == "_foo");
    REQUIRE(getNextLine(&output) == "");
}

TEST_CASE( "Normalised Levenshtein setting for short and long strings", "[matcher]" ) {
	Settings settings;
	settings.comparisonMethod = NormalizedLevenshtein;
	settings.maxDistance = 0.35;

	Matcher matcher(settings);

	// One character difference
	matcher.add("rat");
	matcher.add("cat");

	// Two character difference
	matcher.add("elephant");
	matcher.add("elephenty");

	// All different
	matcher.add("monkey");

	std::stringstream output;
	matcher.show(&output);

	REQUIRE(getNextLine(&output) == "elephant");
	REQUIRE(getNextLine(&output) == "monkey");
	REQUIRE(getNextLine(&output) == "rat");
	REQUIRE(getNextLine(&output) == "");
}

TEST_CASE( "similarity::levenshteinDistance returns correct values", "[levenshtein]" ) {
	REQUIRE(similarity::levenshteinDistance("cat", "rat") == 1);
	REQUIRE(similarity::levenshteinDistance("hello", "helo") == 1);
	REQUIRE(similarity::levenshteinDistance("think", "thinky") == 1);
	REQUIRE(similarity::levenshteinDistance("hair", "head") == 3);
}

TEST_CASE( "similarity::normalizedLevenshtein returns correct values", "[levenshtein]" ) {
	REQUIRE(similarity::normalizedLevenshtein("fear", "wear") == 0.25);
	REQUIRE(similarity::normalizedLevenshtein("here", "hear") == 0.5);
	REQUIRE(similarity::normalizedLevenshtein("not", "yes") == 1);
	REQUIRE(similarity::normalizedLevenshtein("same", "same") == 0);
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

TEST_CASE( "similarity::boundedLevenshtein agrees with the full DP", "[levenshtein]" ) {
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
		unsigned int full = similarity::levenshteinDistance(a, b);
		unsigned int want = full <= k ? full : k + 1;
		INFO("a=\"" << a << "\" b=\"" << b << "\" k=" << k);
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

	std::stringstream output;
	matcher.show(&output);

	REQUIRE(getNextLine(&output) == "aaaa\taaab\taaab");
	REQUIRE(getNextLine(&output) == "zzzz\tzzzz");
	REQUIRE(getNextLine(&output) == "");
}

TEST_CASE( "show-all prints duplicates as originally written, not normalized", "[matcher]" ) {
	Settings settings;
	settings.caseInsensitive = true;
	settings.ignoreNonAlphaNumeric = true;
	settings.showAllMatches = true;

	Matcher matcher(settings);
	matcher.add("Hello, World!");
	matcher.add("HELLO WORLD");

	std::stringstream output;
	matcher.show(&output);

	REQUIRE(getNextLine(&output) == "Hello, World!\tHELLO WORLD");
	REQUIRE(getNextLine(&output) == "");
}

TEST_CASE( "a line joins only the first group it matches", "[matcher]" ) {
	Settings settings;
	settings.maxDistance = 1;
	settings.showTotals = true;

	Matcher matcher(settings);
	matcher.add("aa");
	matcher.add("bb");
	matcher.add("ab"); // distance 1 from both "aa" and "bb"

	std::stringstream output;
	matcher.show(&output);

	// Counts must sum to the number of input lines (3), not 4
	REQUIRE(getNextLine(&output) == "      2 aa");
	REQUIRE(getNextLine(&output) == "      1 bb");
	REQUIRE(getNextLine(&output) == "");
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

	struct Case { ComparisonMethod method; float d; bool ci; };
	const Case cases[] = { {Levenshtein, 3, false}, {Levenshtein, 1, true},
	                       {NormalizedLevenshtein, 0.125f, false}, {NormalizedLevenshtein, 0.3f, true} };
	for(const Case& c : cases) {
		Settings settings;
		settings.comparisonMethod = c.method;
		settings.maxDistance = c.d;
		settings.caseInsensitive = c.ci;
		settings.showAllMatches = true;
		settings.showTotals = true;

		std::string serial = runMatcher(settings, lines, 0, &rng);
		for(unsigned threads : {1u, 2u, 3u, 8u}) {
			INFO("method=" << c.method << " d=" << c.d << " threads=" << threads);
			REQUIRE(runMatcher(settings, lines, threads, &rng) == serial);
		}
	}
}
