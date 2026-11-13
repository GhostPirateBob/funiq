#ifndef _FUNIQ_SETTINGS_
#define _FUNIQ_SETTINGS_

// Levenshtein: absolute edit distance. NormalizedLevenshtein: edits / longer
// length. TokenSort: NormalizedLevenshtein on whitespace tokens sorted
// alphabetically (word order ignored). Cosine: 1 - cosine similarity of
// character-trigram counts (order-tolerant, length-independent).
enum ComparisonMethod { Levenshtein, NormalizedLevenshtein, TokenSort, Cosine };

// Which member is printed for a group: the first line seen (also the key
// that matching compares against), or the longest member.
enum Representative { RepFirst, RepLongest };

struct Settings
{
	Settings() :
		maxDistance(3),
		caseInsensitive(false),
        comparisonMethod(Levenshtein),
		showAllMatches(false),
		showTotals(false),
		ignoreNonAlphaNumeric(false),
		threads(1),
		maskDigits(false),
		representative(RepFirst),
		showScores(false),
		bestMatch(false),
		color(false)
		{}

	float maxDistance;
	bool caseInsensitive;
	ComparisonMethod comparisonMethod;
	bool showAllMatches;
	bool showTotals;
	bool ignoreNonAlphaNumeric;
	unsigned threads; // worker threads for matching; results never depend on it
	bool maskDigits;  // each run of digits compares as a single '0'
	Representative representative;
	bool showScores;  // print each duplicate's distance from its group key (implies -a)
	bool bestMatch;   // join the closest group instead of the first one in range
	bool color;       // colour stdout (decided once, see color::want)
	int totalsFieldWidth;
};

#endif
