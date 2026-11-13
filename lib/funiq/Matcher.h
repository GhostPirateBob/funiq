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
#include <sstream>
#include <unordered_map>
#include <vector>

#include "Color.h"
#include "Settings.h"
#include "similarity.h"

#define TOTALS_FIELD_WIDTH 7 // count-field width 7 used by GNU uniq

typedef std::vector<std::string> StringList;

// Each group stores its original key, cached normalized key (and trigram
// profile for -m cosine), its members, and each member's distance from the
// key (matches[0] is the key itself, distance 0).
struct MatchGroup {
	std::string key;
	std::string normalizedKey;
	similarity::Profile profile;
	StringList matches;
	std::vector<double> scores;
};

static const size_t NO_GROUP = (size_t)-1;

// A line prepared for comparison: normalized once, profiled once (cosine).
struct Candidate {
	std::string normalized;
	similarity::Profile profile;
};

// The group a line joins (NO_GROUP: start a new one) and its distance.
struct Hit {
	size_t group;
	double score;
	Hit() : group(NO_GROUP), score(0) {}
	Hit(size_t g, double s) : group(g), score(s) {}
};

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
	// normalized line -> where it went. Safe under greedy first-match: groups
	// created later have higher indices, so a repeat line's first match can
	// never change. Not under --best, where a later group can be closer.
	std::unordered_map<std::string, Hit> groupOf;
	// keyLengths[i] == groups[i].normalizedKey.size(), stored contiguously
	// so the length pre-check scans one compact array instead of the groups.
	std::vector<unsigned int> keyLengths;
	// DP rows for the serial path; each addBatch worker brings its own.
	std::vector<unsigned int> scratch;

	std::string normalize(const std::string& s) const;
	Candidate prepare(const std::string& line) const;
	bool lengthRulesOut(unsigned int len, unsigned int keyLen) const;
	bool distanceTo(const Candidate& c, size_t i, std::vector<unsigned int>& rows, double& score) const;
	// A negative (or NaN) threshold matches nothing, not even an identical
	// line, so repeats must not be memoised into one group there.
	bool memoEnabled() const { return _settings.maxDistance >= 0 && !_settings.bestMatch; }
	Hit lookupMemo(const std::string& normalizedLine) const;
	Hit findMatch(const Candidate& c, size_t from, size_t to, std::vector<unsigned int>& rows) const;
	void place(const std::string& line, Candidate& c, Hit hit);
	std::string formatScore(double score) const;
};

// Distance is at least the length difference, so if even that fails the
// threshold the pair can't match. Uses the same arithmetic as distanceTo, so
// it only ever skips pairs that would be rejected (most of them, in practice).
// Cosine has no such bound.
inline bool Matcher::lengthRulesOut(unsigned int len, unsigned int keyLen) const {
	if(_settings.comparisonMethod == Cosine) return false;
	unsigned int diff = len > keyLen ? len - keyLen : keyLen - len;
	if(_settings.comparisonMethod != Levenshtein) {
		unsigned int maxLen = std::max(len, keyLen);
		return maxLen > 0 && (float)diff / (float)maxLen > _settings.maxDistance;
	}
	return diff > _settings.maxDistance;
}

Matcher::Matcher(Settings& settings):_settings(settings) {
}

// Normalize a string according to the current settings: lowercase, collapse
// digit runs, then either sort its words (-m token-sort; with -I punctuation
// separates words) or strip non-alphanumerics (-I). Paid once per line.
std::string Matcher::normalize(const std::string& s) const {
	std::string result = s;
	if(_settings.caseInsensitive)
		std::transform(result.begin(), result.end(), result.begin(),
			[](unsigned char c) { return (char)std::tolower(c); });
	if(_settings.maskDigits) {
		std::string masked;
		masked.reserve(result.size());
		for(size_t i = 0; i < result.size(); i++) {
			bool digit = std::isdigit((unsigned char)result[i]);
			if(digit && i > 0 && std::isdigit((unsigned char)result[i - 1])) continue;
			masked += digit ? '0' : result[i];
		}
		result.swap(masked);
	}
	if(_settings.comparisonMethod == TokenSort) {
		if(_settings.ignoreNonAlphaNumeric)
			for(auto& c : result) if(!std::isalnum((unsigned char)c)) c = ' ';
		std::istringstream words(result);
		std::vector<std::string> tokens;
		for(std::string w; words >> w; ) tokens.push_back(w);
		std::sort(tokens.begin(), tokens.end());
		result.clear();
		for(size_t i = 0; i < tokens.size(); i++) result += (i ? " " : "") + tokens[i];
	} else if(_settings.ignoreNonAlphaNumeric) {
		result.erase(std::remove_if(result.begin(), result.end(), [](unsigned char c){
			return !std::isalnum(c);
		}), result.end());
	}
	return result;
}

Candidate Matcher::prepare(const std::string& line) const {
	Candidate c;
	c.normalized = normalize(line);
	if(_settings.comparisonMethod == Cosine) c.profile = similarity::trigramProfile(c.normalized);
	return c;
}

Hit Matcher::lookupMemo(const std::string& normalizedLine) const {
	if(!memoEnabled()) return Hit();
	auto it = groupOf.find(normalizedLine);
	return it == groupOf.end() ? Hit() : it->second;
}

// Without --best: the first group in [from, to) within the threshold. With
// --best: the closest one, lowest index on ties. Read-only apart from `rows`,
// so threads can call it at once as long as each passes its own buffer.
Hit Matcher::findMatch(const Candidate& c, size_t from, size_t to,
                       std::vector<unsigned int>& rows) const {
	const unsigned int len = c.normalized.size();
	Hit best;
	for(size_t i = from; i < to; i++) {
		if(lengthRulesOut(len, keyLengths[i])) continue;
		double score;
		if(!distanceTo(c, i, rows, score)) continue;
		if(!_settings.bestMatch) return Hit(i, score);
		if(best.group == NO_GROUP || score < best.score) best = Hit(i, score);
		if(score == 0) break; // nothing can beat an exact match
	}
	return best;
}

// Adds the line to the hit group, or starts a new group if there is none.
void Matcher::place(const std::string& line, Candidate& c, Hit hit) {
	if(memoEnabled())
		groupOf.emplace(c.normalized, hit.group == NO_GROUP ? Hit(groups.size(), 0) : hit);
	if(hit.group != NO_GROUP) {
		groups[hit.group].matches.push_back(line);
		groups[hit.group].scores.push_back(hit.score);
		return;
	}
	MatchGroup group;
	group.key = line;
	group.normalizedKey = c.normalized;
	group.profile = std::move(c.profile);
	group.matches.push_back(line);
	group.scores.push_back(0);
	groups.push_back(std::move(group));
	keyLengths.push_back(c.normalized.size());
}

// A line joins only one group, so counts sum to the number of lines.
void Matcher::add(std::string line) {
	Candidate c = prepare(line);
	Hit hit = lookupMemo(c.normalized);
	if(hit.group == NO_GROUP) hit = findMatch(c, 0, groups.size(), scratch);
	place(line, c, hit);
}

// Two phases, giving exactly the serial result:
//  1. parallel, read-only: each line's match among the groups that existed
//     before the batch ("frozen", indices < frozen)
//  2. serial, in input order, against groups created earlier in the batch
//     (all with higher indices). First-match: a phase-1 hit already wins.
//     --best: a new group wins only if strictly closer (ties go to the
//     lower index, as in the serial scan).
void Matcher::addBatch(const std::vector<std::string>& lines, unsigned threads) {
	const size_t frozen = groups.size();
	std::vector<Candidate> candidates(lines.size());
	std::vector<Hit> hit(lines.size());

	std::atomic<size_t> next(0);
	auto worker = [&]() {
		std::vector<unsigned int> rows; // this thread's DP buffer
		for(size_t i; (i = next++) < lines.size(); ) {
			candidates[i] = prepare(lines[i]);
			Hit memo = lookupMemo(candidates[i].normalized);
			hit[i] = (memo.group != NO_GROUP) ? memo : findMatch(candidates[i], 0, frozen, rows);
		}
	};
	unsigned workers = (unsigned)std::max<size_t>(1, std::min<size_t>(threads, lines.size()));
	std::vector<std::thread> pool;
	for(unsigned t = 1; t < workers; t++) pool.emplace_back(worker);
	worker();
	for(auto& t : pool) t.join();

	for(size_t i = 0; i < lines.size(); i++) {
		Hit h = lookupMemo(candidates[i].normalized); // an earlier line in this batch may have memoised it
		if(h.group == NO_GROUP) {
			h = hit[i];
			if(_settings.bestMatch) {
				Hit later = findMatch(candidates[i], frozen, groups.size(), scratch);
				if(later.group != NO_GROUP && (h.group == NO_GROUP || later.score < h.score)) h = later;
			} else if(h.group == NO_GROUP) {
				h = findMatch(candidates[i], frozen, groups.size(), scratch);
			}
		}
		place(lines[i], candidates[i], h);
	}
}

std::string Matcher::formatScore(double score) const {
	std::ostringstream out;
	if(_settings.comparisonMethod == Levenshtein) out << (unsigned long)score;
	else out << std::fixed << std::setprecision(3) << score;
	return out.str();
}

void Matcher::show(std::ostream* output) {
	// Each group is printed as its representative (first line seen, or the
	// longest member with --rep longest; ties go to the earlier line).
	struct Row { const MatchGroup* group; size_t rep; };
	std::vector<Row> rows;
	rows.reserve(groups.size());
	for(const auto& group : groups) {
		size_t rep = 0;
		if(_settings.representative == RepLongest)
			for(size_t j = 1; j < group.matches.size(); j++)
				if(group.matches[j].size() > group.matches[rep].size()) rep = j;
		rows.push_back(Row{&group, rep});
	}
	// Sort alphabetically by representative, like GNU uniq on sorted input.
	// Sort rows, not the groups, so groupOf's indices stay valid.
	std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
		return a.group->matches[a.rep] < b.group->matches[b.rep];
	});

	// Colour adds SGR codes around fields and changes nothing else, so the
	// output with codes stripped is byte-identical to the plain output.
	const bool on = _settings.color;
	const bool all = _settings.showAllMatches || _settings.showScores;
	for(const Row& row : rows) {
		const MatchGroup& group = *row.group;
		if(_settings.showTotals) {
			// Pad first, then colour, so codes don't disturb the alignment.
			// Frequency buckets make the hot lines of a log jump out.
			const size_t n = group.matches.size();
			std::ostringstream count;
			count << std::setw(TOTALS_FIELD_WIDTH) << n;
			const color::Style& style = n >= 100 ? color::BoldRed : n >= 10 ? color::Yellow
			                          : n >= 2 ? color::Green : color::Gray;
			*output << color::paint(on, style, count.str()) << " "; // space for compatibility with GNU uniq
		}
		*output << group.matches[row.rep];
		if(all) {
			for(size_t j = 0; j < group.matches.size(); j++) {
				if(j == row.rep) continue;
				*output << "\t" << color::paint(on, color::Dim, group.matches[j]);
				if(_settings.showScores) {
					// Green: well inside the threshold; yellow: near its edge.
					bool close = group.scores[j] <= _settings.maxDistance / 2.0;
					*output << " " << color::paint(on, close ? color::Green : color::Yellow,
					                               "(" + formatScore(group.scores[j]) + ")");
				}
			}
		}
		// Use '\n' instead of std::endl to avoid flushing the output
		// buffer on every line, which is significantly faster when
		// piping to other commands.
		*output << '\n';
	}
}

// Whether the candidate matches group i, and its distance from the key in
// the method's units (edits; edits / longer length; 1 - cosine). The
// Levenshtein methods turn the threshold into an edit budget k and ask for
// the distance only up to k, so far-away keys are rejected without the full
// O(n*m) DP; the final comparisons are the same expressions as an unbounded
// computation, so results are identical.
bool Matcher::distanceTo(const Candidate& c, size_t i, std::vector<unsigned int>& rows,
                         double& score) const {
	const float d = _settings.maxDistance;
	if(!(d >= 0)) return false; // negative or NaN: no distance qualifies
	const std::string& s1 = c.normalized;
	const std::string& s2 = groups[i].normalizedKey;

	if(_settings.comparisonMethod == Cosine) {
		score = similarity::cosineDistance(c.profile, groups[i].profile);
		return score <= d;
	}

	const unsigned int maxLen = std::max(s1.size(), s2.size());
	if(_settings.comparisonMethod != Levenshtein) { // normalized, token-sort
		if(maxLen == 0) { score = 0; return true; } // both empty: distance 0
		// One edit of slack so float rounding can't make k too tight;
		// the exact float test below still has the final say.
		double budget = (double)d * maxLen;
		unsigned int k = budget >= maxLen ? maxLen : (unsigned int)budget + 1;
		unsigned int dist = similarity::boundedLevenshtein(s1, s2, k, rows);
		float ratio = (float)dist / (float)maxLen;
		score = ratio;
		return ratio <= d;
	}

	unsigned int k = d >= maxLen ? maxLen : (unsigned int)d;
	unsigned int dist = similarity::boundedLevenshtein(s1, s2, k, rows);
	score = dist;
	return dist <= d;
}


#endif
