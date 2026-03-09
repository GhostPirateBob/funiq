#ifndef _FUNIQ_MATCHER_
#define _FUNIQ_MATCHER_

#include <algorithm>
#include <string>
#include <iostream>
#include <iomanip>
#include <functional>
#include <cctype>
#include <map>
#include <vector>

#include "Settings.h"
#include "similarity.h"

#define TOTALS_FIELD_WIDTH 7 // count-field width 7 used by GNU uniq

typedef std::vector<std::string> StringList;

// Each group stores its original key, cached normalized key, and list of matches
struct MatchGroup {
	std::string key;
	std::string normalizedKey;
	StringList matches;
};


class Matcher{
public:
	Matcher(Settings& settings);
	void add(std::string line);
	void show(std::ostream* output);
private:
	Settings& _settings;
	std::vector<MatchGroup> groups;
	std::string normalize(const std::string& s);
	bool isMatch(const std::string& s1, const std::string& s2);
};

Matcher::Matcher(Settings& settings):_settings(settings) {
}

// Normalize a string according to the current settings (lowercase, strip
// non-alphanumeric). Used once per key and once per input line so the
// normalization cost is paid only at insertion time.
std::string Matcher::normalize(const std::string& s) {
	std::string result = s;
	if(_settings.caseInsensitive)
		std::transform(result.begin(), result.end(), result.begin(), ::tolower);
	if(_settings.ignoreNonAlphaNumeric)
		result.erase(std::remove_if(result.begin(), result.end(), [](const char& c){
			return !std::isalnum(c);
		}), result.end());
	return result;
}

void Matcher::add(std::string line) {
	bool matchFound = false;
	std::string normalizedLine = normalize(line);

	// Compare against each group's cached normalized key to avoid
	// re-normalizing existing keys on every insertion.
	for(auto& group : groups) {
		if(isMatch(normalizedLine, group.normalizedKey)) {
			matchFound = true;
			group.matches.push_back(normalizedLine);
			continue;
		}
	}

	if(!matchFound) {
		MatchGroup group;
		group.key = line;
		group.normalizedKey = normalizedLine;
		group.matches.push_back(line);
		groups.push_back(std::move(group));
	}
}

void Matcher::show(std::ostream* output) {
	// Sort groups alphabetically by key to match the behaviour of the
	// previous std::map-based implementation and GNU uniq conventions.
	std::sort(groups.begin(), groups.end(),
		[](const MatchGroup& a, const MatchGroup& b) {
			return a.key < b.key;
		});

	for(const auto& group : groups) {
		bool first = true;
		for(const auto& matchItem : group.matches) {
			if(first || _settings.showAllMatches) {
				if(first && _settings.showTotals)
					*output <<
						std::setw(TOTALS_FIELD_WIDTH) <<
						group.matches.size() << " "; // space for compatibility with GNU uniq
				if(!first) *output << "\t";
				*output << matchItem;
				first = false;
			}
		}
		// Use '\n' instead of std::endl to avoid flushing the output
		// buffer on every line, which is significantly faster when
		// piping to other commands.
		*output << '\n';
	}
}

bool Matcher::isMatch(const std::string& s1, const std::string& s2) {
	if (_settings.comparisonMethod == NormalizedLevenshtein) {
		return similarity::normalizedLevenshtein(s1, s2) <= _settings.maxDistance;
	}
	return similarity::levenshteinDistance(s1, s2) <= _settings.maxDistance;
}


#endif
