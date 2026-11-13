#ifndef _FUNIQ_MATCHER_
#define _FUNIQ_MATCHER_

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <iostream>
#include <iomanip>
#include <functional>
#include <cctype>
#include <map>
#include <unordered_map>
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


static const size_t NO_GROUP = (size_t)-1;

class Matcher{
public:
	Matcher(Settings& settings);
	void add(std::string line);
	// Same result as calling add() on each line in order, but the scan
	// against groups that existed before the batch runs on up to `threads`
	// threads.
	void addBatch(const std::vector<std::string>& lines, unsigned threads);
	void show(std::ostream* output);
private:
	Settings& _settings;
	std::vector<MatchGroup> groups;
	// normalized line -> index of the group it joined. Safe under greedy
	// first-match: groups created later have higher indices, so a repeat
	// line's first match can never change.
	std::unordered_map<std::string, size_t> groupOf;
	// keyLengths[i] == groups[i].normalizedKey.size(), stored contiguously
	// so the length pre-check scans one compact array instead of the groups.
	std::vector<unsigned int> keyLengths;
	std::string normalize(const std::string& s) const;
	bool lengthRulesOut(unsigned int len, unsigned int keyLen) const;
	// DP rows for the serial path; each addBatch worker brings its own.
	std::vector<unsigned int> scratch;
	bool isMatch(const std::string& s1, const std::string& s2, std::vector<unsigned int>& rows) const;
	// A negative (or NaN) threshold matches nothing, not even an identical
	// line, so repeats must not be memoised into one group there.
	bool memoEnabled() const { return _settings.maxDistance >= 0; }
	size_t lookupMemo(const std::string& normalizedLine) const;
	size_t firstMatch(const std::string& normalizedLine, size_t from, size_t to,
	                  std::vector<unsigned int>& rows) const;
	void place(const std::string& line, const std::string& normalizedLine, size_t g);
};

// Distance is at least the length difference, so if even that fails the
// threshold the pair can't match. Uses the same arithmetic as isMatch, so it
// only ever skips pairs isMatch would reject (most of them, in practice).
inline bool Matcher::lengthRulesOut(unsigned int len, unsigned int keyLen) const {
	unsigned int diff = len > keyLen ? len - keyLen : keyLen - len;
	if(_settings.comparisonMethod == NormalizedLevenshtein) {
		unsigned int maxLen = std::max(len, keyLen);
		return maxLen > 0 && (float)diff / (float)maxLen > _settings.maxDistance;
	}
	return diff > _settings.maxDistance;
}

Matcher::Matcher(Settings& settings):_settings(settings) {
}

// Normalize a string according to the current settings (lowercase, strip
// non-alphanumeric). Used once per key and once per input line so the
// normalization cost is paid only at insertion time.
std::string Matcher::normalize(const std::string& s) const {
	std::string result = s;
	if(_settings.caseInsensitive)
		std::transform(result.begin(), result.end(), result.begin(), ::tolower);
	if(_settings.ignoreNonAlphaNumeric)
		result.erase(std::remove_if(result.begin(), result.end(), [](const char& c){
			return !std::isalnum(c);
		}), result.end());
	return result;
}

size_t Matcher::lookupMemo(const std::string& normalizedLine) const {
	if(!memoEnabled()) return NO_GROUP;
	auto it = groupOf.find(normalizedLine);
	return it == groupOf.end() ? NO_GROUP : it->second;
}

// Index of the first group in [from, to) whose cached normalized key matches,
// or NO_GROUP. Read-only apart from `rows`, so threads can call it at once
// as long as each passes its own rows buffer.
size_t Matcher::firstMatch(const std::string& normalizedLine, size_t from, size_t to,
                           std::vector<unsigned int>& rows) const {
	const unsigned int len = normalizedLine.size();
	for(size_t i = from; i < to; i++) {
		if(lengthRulesOut(len, keyLengths[i])) continue;
		if(isMatch(normalizedLine, groups[i].normalizedKey, rows)) return i;
	}
	return NO_GROUP;
}

// Adds the line to group g, or starts a new group if g is NO_GROUP.
void Matcher::place(const std::string& line, const std::string& normalizedLine, size_t g) {
	if(memoEnabled()) groupOf.emplace(normalizedLine, g == NO_GROUP ? groups.size() : g);
	if(g != NO_GROUP) {
		groups[g].matches.push_back(line);
		return;
	}
	MatchGroup group;
	group.key = line;
	group.normalizedKey = normalizedLine;
	group.matches.push_back(line);
	groups.push_back(std::move(group));
	keyLengths.push_back(normalizedLine.size());
}

// A line joins only the first group it matches, so counts sum to the number
// of lines.
void Matcher::add(std::string line) {
	std::string normalizedLine = normalize(line);
	size_t g = lookupMemo(normalizedLine);
	if(g == NO_GROUP) g = firstMatch(normalizedLine, 0, groups.size(), scratch);
	place(line, normalizedLine, g);
}

// Two phases, giving exactly the serial result:
//  1. parallel, read-only: each line's first match among the groups that
//     existed before the batch ("frozen", indices < frozen)
//  2. serial, in input order: a phase-1 hit is the line's overall first match,
//     because every group created during the batch has a higher index; a miss
//     only needs checking against the groups created earlier in this batch.
void Matcher::addBatch(const std::vector<std::string>& lines, unsigned threads) {
	const size_t frozen = groups.size();
	std::vector<std::string> normalized(lines.size());
	std::vector<size_t> hit(lines.size(), NO_GROUP);

	std::atomic<size_t> next(0);
	auto worker = [&]() {
		std::vector<unsigned int> rows; // this thread's DP buffer
		for(size_t i; (i = next++) < lines.size(); ) {
			normalized[i] = normalize(lines[i]);
			size_t g = lookupMemo(normalized[i]);
			hit[i] = (g != NO_GROUP) ? g : firstMatch(normalized[i], 0, frozen, rows);
		}
	};
	unsigned workers = (unsigned)std::max<size_t>(1, std::min<size_t>(threads, lines.size()));
	std::vector<std::thread> pool;
	for(unsigned t = 1; t < workers; t++) pool.emplace_back(worker);
	worker();
	for(auto& t : pool) t.join();

	for(size_t i = 0; i < lines.size(); i++) {
		size_t g = lookupMemo(normalized[i]); // an earlier line in this batch may have memoised it
		if(g == NO_GROUP) g = hit[i];
		if(g == NO_GROUP) g = firstMatch(normalized[i], frozen, groups.size(), scratch);
		place(lines[i], normalized[i], g);
	}
}

void Matcher::show(std::ostream* output) {
	// Sort groups alphabetically by key to match the behaviour of the
	// previous std::map-based implementation and GNU uniq conventions.
	// Sort pointers, not the groups, so groupOf's indices stay valid.
	std::vector<const MatchGroup*> sorted;
	sorted.reserve(groups.size());
	for(const auto& group : groups) sorted.push_back(&group);
	std::sort(sorted.begin(), sorted.end(),
		[](const MatchGroup* a, const MatchGroup* b) {
			return a->key < b->key;
		});

	for(const MatchGroup* groupPtr : sorted) {
		const MatchGroup& group = *groupPtr;
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

// Turns the threshold into an integer edit budget k and asks for the distance
// only up to k, so far-away keys are rejected without the full O(n*m) DP.
// The final comparisons are the same expressions as the unbounded version,
// so results are identical.
bool Matcher::isMatch(const std::string& s1, const std::string& s2,
                      std::vector<unsigned int>& rows) const {
	const float d = _settings.maxDistance;
	if(!(d >= 0)) return false; // negative or NaN: no distance qualifies
	const unsigned int maxLen = std::max(s1.size(), s2.size());

	if (_settings.comparisonMethod == NormalizedLevenshtein) {
		if(maxLen == 0) return true; // both empty: distance 0
		// One edit of slack so float rounding can't make k too tight;
		// the exact float test below still has the final say.
		double budget = (double)d * maxLen;
		unsigned int k = budget >= maxLen ? maxLen : (unsigned int)budget + 1;
		unsigned int dist = similarity::boundedLevenshtein(s1, s2, k, rows);
		return (float)dist / (float)maxLen <= d;
	}

	unsigned int k = d >= maxLen ? maxLen : (unsigned int)d;
	return similarity::boundedLevenshtein(s1, s2, k, rows) <= d;
}


#endif
