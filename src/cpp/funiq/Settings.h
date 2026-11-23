#ifndef FUNIQ_SETTINGS_H
#define FUNIQ_SETTINGS_H

// Levenshtein: absolute edit distance. NormalizedLevenshtein: edits / longer
// length. TokenSort: NormalizedLevenshtein on whitespace tokens sorted
// alphabetically (word order ignored). Cosine: 1 - cosine similarity of
// character-trigram counts (order-tolerant, length-independent).
enum ComparisonMethod { Levenshtein, NormalizedLevenshtein, TokenSort, Cosine };

// Which member is printed for a group: the first line seen (also the key
// that matching compares against), or the longest member.
enum Representative { RepFirst, RepLongest };

// Everything that shapes matching and output, filled in from the command
// line by funiq.cpp. The defaults are those of a plain `funiq` run.
struct Settings {
	// matching
	ComparisonMethod comparisonMethod = Levenshtein;
	float maxDistance = 3;               // threshold, in the method's units
	bool caseInsensitive = false;        // -i
	bool ignoreNonAlphaNumeric = false;  // -I
	bool maskDigits = false;             // -D: each run of digits compares as a single '0'
	bool bestMatch = false;              // --best: closest group instead of the first in range
	unsigned threads = 1;                // -j; results never depend on it

	// output
	bool showTotals = false;             // -c
	bool showAllMatches = false;         // -a
	bool showScores = false;             // --scores (implies -a)
	Representative representative = RepFirst;  // --rep
	bool color = false;                  // colour stdout (decided once, see color::want)
};

#endif
