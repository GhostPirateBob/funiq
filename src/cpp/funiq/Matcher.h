#ifndef FUNIQ_MATCHER_H
#define FUNIQ_MATCHER_H

#include <algorithm>
#include <atomic>
#include <string>
#include <iostream>
#include <iomanip>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "Color.h"
#include "PieceIndex.h"
#include "Settings.h"
#include "WorkerPool.h"
#include "similarity.h"


// A group: its key (the first line, which matching compares against), the
// cached normalized key (and trigram profile for -m cosine), and its size.
// Members themselves are kept only when they are printed (-a, --scores), so a
// plain or -c run doesn't hold every input line in memory.
struct MatchGroup {
	std::string key;
	std::string normalizedKey;
	similarity::Profile profile;
	size_t count = 0;                  // lines in the group, the key included
	std::string longest;               // --rep longest: longest line so far (earliest on ties)
	size_t longestAt = 0;              // its position in the group (0: the key)
	std::vector<std::string> matches;  // -a/--scores: every line, matches[0] is the key
	std::vector<double> scores;        // ... and its distance from the key
};

static const size_t NO_GROUP = (size_t)-1;

// A line prepared for comparison: normalized once, profiled once (cosine).
struct Candidate {
	std::string normalized;
	similarity::Profile profile;
	// set by prepareScan, only when a scan needs it (not on memo hits); points
	// into the matcher's reused storage, so a line carries 8 bytes, not 128
	const similarity::ByteHistogram* histogram = nullptr;
};

// The group a line joins (NO_GROUP: start a new one) and its distance.
struct Hit {
	size_t group;
	double score;
	Hit() : group(NO_GROUP), score(0) {}
	Hit(size_t g, double s) : group(g), score(s) {}
};

class Matcher {
public:
	explicit Matcher(const Settings& settings);
	// Use the piece index once a scan would cover at least this many groups
	// (tests force it on with 0, off with (size_t)-1).
	void setIndexMinGroups(size_t n) { indexMinGroups = n; }
	void add(std::string line);
	// Same result as calling add() on each line in order, but the scan
	// against groups that existed before the batch runs on up to `threads`
	// threads.
	void addBatch(std::vector<std::string> lines, unsigned threads);
	void show(std::ostream* output) const;
private:
	// Width of the -c count column, right-aligned like `uniq -c`.
	static const int countWidth = 7;
	const Settings& opts;
	std::vector<MatchGroup> groups;
	// normalized line -> where it went. Safe under greedy first-match: groups
	// created later have higher indices, so a repeat line's first match can
	// never change. Not under --best, where a later group can be closer.
	std::unordered_map<std::string, Hit> groupOf;
	// keyHistograms[i]: groups[i].normalizedKey's byte counts, for the
	// prefilter (empty for -m cosine, which has no edit budget)
	std::vector<similarity::ByteHistogram> keyHistograms;
	// -m levenshtein with a small whole budget: a pigeonhole index of the
	// keys, so a line visits only groups that could be within k edits
	// (24k-288k diverse lines: each line visited ~16k groups, 99.97% misses)
	bool indexed = false;
	PieceIndex index;
	size_t indexMinGroups = (size_t)-1;
	// keyLengths[i] == groups[i].normalizedKey.size(), stored contiguously
	// so the length pre-check scans one compact array instead of the groups.
	std::vector<unsigned int> keyLengths;
	// Distance working memory for the serial path.
	similarity::Scratch scratch;
	// addBatch's threads, started on first use and kept, each with its own
	// distance working memory.
	std::unique_ptr<WorkerPool> pool;
	std::vector<similarity::Scratch> poolScratch;
	bool keepMembers() const { return opts.showAllMatches || opts.showScores; }

	std::string normalize(const std::string& s) const;
	Candidate prepare(const std::string& line) const;
	bool lengthRulesOut(unsigned int len, unsigned int keyLen) const;
	bool distanceTo(const Candidate& c, size_t i, similarity::Scratch& scratch, double& score) const;
	// A negative (or NaN) threshold matches nothing, not even an identical
	// line, so repeats must not be memoised into one group there.
	bool memoEnabled() const { return opts.maxDistance >= 0 && !opts.bestMatch; }
	bool usesHistogram() const { return opts.comparisonMethod != Cosine; }
	// before a scan: the candidate's histogram, built into `storage`
	void prepareScan(Candidate& c, similarity::ByteHistogram& storage) const {
		if(!usesHistogram()) return;
		similarity::buildHistogram(c.normalized, storage);
		c.histogram = &storage;
	}
	similarity::ByteHistogram addHistogram;                 // add()'s histogram
	std::vector<similarity::ByteHistogram> batchHistograms; // addBatch(): one per line, reused
	Hit lookupMemo(const std::string& normalizedLine) const;
	Hit findMatch(const Candidate& c, size_t from, size_t to, similarity::Scratch& scratch) const;
	// memoised: the hit came from groupOf, so there is nothing to insert
	void place(std::string line, Candidate& c, Hit hit, bool memoised);
	std::string formatScore(double score) const;
};

// Distance is at least the length difference, so if even that fails the
// threshold the pair can't match. Uses the same arithmetic as distanceTo, so
// it only ever skips pairs that would be rejected (most of them, in practice).
// Cosine has no such bound.
inline bool Matcher::lengthRulesOut(unsigned int len, unsigned int keyLen) const {
	if(opts.comparisonMethod == Cosine) return false;
	unsigned int diff = len > keyLen ? len - keyLen : keyLen - len;
	if(opts.comparisonMethod != Levenshtein) {
		unsigned int maxLen = std::max(len, keyLen);
		return maxLen > 0 && (float)diff / (float)maxLen > opts.maxDistance;
	}
	return diff > opts.maxDistance;
}

inline Matcher::Matcher(const Settings& settings) : opts(settings) {
	// Lookups per line grow as k^3 and pieces get less selective, so only
	// small budgets (96k diverse lines: k = 1 7.8x faster, 2 2.8x, 3 1.8x,
	// 4 1.2x, 5 0.83x). A line costs about (2k+1)(k+1)(k/2+1) lookups, so
	// the index pays off once a scan would visit some multiple of that.
	const float d = opts.maxDistance;
	if(opts.comparisonMethod == Levenshtein && d >= 0 && d < 5) {
		const unsigned k = (unsigned)d;
		indexed = true;
		index = PieceIndex(k);
		indexMinGroups = 16 * (size_t)(2 * k + 1) * (k + 1) * (k / 2 + 1);
	}
}

// ASCII classes, as <cctype> gives them in the C locale funiq runs in, but
// inline: on MinGW/UCRT each <cctype> call is a real call through the locale.
inline bool asciiDigit(unsigned char c) { return c >= '0' && c <= '9'; }
inline bool asciiAlnum(unsigned char c) {
	return asciiDigit(c) || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z');
}
inline bool cSpace(unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

// Normalize a string according to the current settings: lowercase, collapse
// digit runs, then either sort its words (-m token-sort; with -I punctuation
// separates words) or strip non-alphanumerics (-I). One pass and one
// allocation per line: memo-heavy log runs do little else per line.
inline std::string Matcher::normalize(const std::string& s) const {
	const bool tokenSort = opts.comparisonMethod == TokenSort;
	const bool punctuation = opts.ignoreNonAlphaNumeric;
	std::string result;
	result.reserve(s.size());
	for(size_t i = 0; i < s.size(); i++) {
		unsigned char c = s[i];
		if(opts.maskDigits && asciiDigit(c)) {
			if(i > 0 && asciiDigit(s[i - 1])) continue; // the run is already one '0'
			c = '0';
		}
		if(opts.caseInsensitive && c >= 'A' && c <= 'Z') c += 'a' - 'A';
		if(punctuation && !asciiAlnum(c)) {
			if(!tokenSort) continue; // -I: drop it
			c = ' ';                 // token-sort -I: it separates words
		}
		result += (char)c;
	}
	if(!tokenSort) return result;
	// the words, split on C whitespace, sorted bytewise, joined by one space
	std::vector<std::string> tokens;
	for(size_t i = 0; i < result.size(); ) {
		while(i < result.size() && cSpace(result[i])) i++;
		size_t start = i;
		while(i < result.size() && !cSpace(result[i])) i++;
		if(i > start) tokens.push_back(result.substr(start, i - start));
	}
	std::sort(tokens.begin(), tokens.end());
	result.clear();
	for(size_t i = 0; i < tokens.size(); i++) {
		if(i) result += ' ';
		result += tokens[i];
	}
	return result;
}

inline Candidate Matcher::prepare(const std::string& line) const {
	Candidate c;
	c.normalized = normalize(line);
	if(opts.comparisonMethod == Cosine) c.profile = similarity::trigramProfile(c.normalized);
	return c;
}

inline Hit Matcher::lookupMemo(const std::string& normalizedLine) const {
	if(!memoEnabled()) return Hit();
	auto it = groupOf.find(normalizedLine);
	return it == groupOf.end() ? Hit() : it->second;
}

// Without --best: the first group in [from, to) within the threshold. With
// --best: the closest one, lowest index on ties. Read-only apart from `scratch`,
// so threads can call it at once as long as each passes its own buffer.
inline Hit Matcher::findMatch(const Candidate& c, size_t from, size_t to,
                       similarity::Scratch& scratch) const {
	const unsigned int len = c.normalized.size();
	Hit best;
	auto visit = [&](size_t i) -> bool { // true: stop looking
		if(lengthRulesOut(len, keyLengths[i])) return false;
		double score;
		if(!distanceTo(c, i, scratch, score)) return false;
		if(!opts.bestMatch) { best = Hit(i, score); return true; }
		if(best.group == NO_GROUP || score < best.score) best = Hit(i, score);
		return score == 0; // nothing can beat an exact match
	};
	if(indexed && to - from >= indexMinGroups) {
		// the same groups in the same order, minus those that can't match
		index.candidates(c.normalized, from, to, scratch.candidates);
		for(uint32_t i : scratch.candidates) if(visit(i)) break;
		return best;
	}
	for(size_t i = from; i < to; i++) if(visit(i)) break;
	return best;
}

// Adds the line to the hit group, or starts a new group if there is none.
// A memo hit inserts nothing: unordered_map::emplace builds (allocates,
// copies, hashes) its node before finding the key is already there.
inline void Matcher::place(std::string line, Candidate& c, Hit hit, bool memoised) {
	if(memoEnabled() && !memoised)
		groupOf.emplace(c.normalized, hit.group == NO_GROUP ? Hit(groups.size(), 0) : hit);
	if(hit.group != NO_GROUP) {
		MatchGroup& g = groups[hit.group];
		if(opts.representative == RepLongest && line.size() > g.longest.size()) {
			g.longest = line;
			g.longestAt = g.count;
		}
		g.count++;
		if(keepMembers()) {
			g.matches.push_back(std::move(line));
			g.scores.push_back(hit.score);
		}
		return;
	}
	MatchGroup group;
	if(opts.representative == RepLongest) group.longest = line;
	if(keepMembers()) {
		group.matches.push_back(line);
		group.scores.push_back(0);
	}
	group.key = std::move(line);
	keyLengths.push_back(c.normalized.size());
	if(usesHistogram()) keyHistograms.push_back(*c.histogram);
	if(indexed) index.add(c.normalized, (uint32_t)groups.size());
	group.normalizedKey = std::move(c.normalized);
	group.profile = std::move(c.profile);
	group.count = 1;
	groups.push_back(std::move(group));
}

// A line joins only one group, so counts sum to the number of lines.
inline void Matcher::add(std::string line) {
	Candidate c = prepare(line);
	Hit hit = lookupMemo(c.normalized);
	const bool memoised = hit.group != NO_GROUP;
	if(!memoised) {
		prepareScan(c, addHistogram);
		hit = findMatch(c, 0, groups.size(), scratch);
	}
	place(std::move(line), c, hit, memoised);
}

// Two phases, giving exactly the serial result:
//  1. parallel, read-only: each line's match among the groups that existed
//     before the batch ("frozen", indices < frozen)
//  2. serial, in input order, against groups created earlier in the batch
//     (all with higher indices). First-match: a phase-1 hit already wins.
//     --best: a new group wins only if strictly closer (ties go to the
//     lower index, as in the serial scan).
inline void Matcher::addBatch(std::vector<std::string> lines, unsigned threads) {
	const size_t frozen = groups.size();
	std::vector<Candidate> candidates(lines.size());
	std::vector<Hit> hit(lines.size());
	std::vector<char> memoised(lines.size()); // hit[i] came from groupOf
	if(batchHistograms.size() < lines.size()) batchHistograms.resize(lines.size());

	const unsigned workers = std::max(1u, threads);
	if(!pool || pool->size() != workers) {
		pool.reset(new WorkerPool(workers));
		poolScratch.assign(workers, similarity::Scratch());
	}
	std::atomic<size_t> next(0);
	pool->run([&](unsigned w) {
		similarity::Scratch& own = poolScratch[w]; // this thread's working memory
		// 16 lines per grab: one atomic increment per line contended across
		// threads (4 threads: -12%)
		for(size_t start; (start = next.fetch_add(16)) < lines.size(); )
		for(size_t i = start, end = std::min(start + 16, lines.size()); i < end; i++) {
			candidates[i] = prepare(lines[i]);
			Hit memo = lookupMemo(candidates[i].normalized);
			memoised[i] = memo.group != NO_GROUP;
			if(memoised[i]) hit[i] = memo;
			else {
				prepareScan(candidates[i], batchHistograms[i]); // phase 2 reuses it
				hit[i] = findMatch(candidates[i], 0, frozen, own);
			}
		}
	});

	for(size_t i = 0; i < lines.size(); i++) {
		// A phase-1 memo hit stands: memo entries never change. Otherwise an
		// earlier line in this batch may have memoised it since.
		Hit h = memoised[i] ? hit[i] : lookupMemo(candidates[i].normalized);
		const bool fromMemo = h.group != NO_GROUP;
		if(!fromMemo) {
			h = hit[i];
			if(opts.bestMatch) {
				Hit later = findMatch(candidates[i], frozen, groups.size(), scratch);
				if(later.group != NO_GROUP && (h.group == NO_GROUP || later.score < h.score)) h = later;
			} else if(h.group == NO_GROUP) {
				h = findMatch(candidates[i], frozen, groups.size(), scratch);
			}
		}
		place(std::move(lines[i]), candidates[i], h, fromMemo);
	}
}

inline std::string Matcher::formatScore(double score) const {
	std::ostringstream out;
	if(opts.comparisonMethod == Levenshtein) out << (unsigned long)score;
	else out << std::fixed << std::setprecision(3) << score;
	return out.str();
}

inline void Matcher::show(std::ostream* output) const {
	// Each group is printed as its representative (first line seen, or the
	// longest member with --rep longest; ties go to the earlier line).
	const bool longest = opts.representative == RepLongest;
	struct Row { const MatchGroup* group; const std::string* rep; size_t repAt; };
	std::vector<Row> rows;
	rows.reserve(groups.size());
	for(const auto& group : groups)
		rows.push_back(longest ? Row{&group, &group.longest, group.longestAt} : Row{&group, &group.key, 0});
	// Sort alphabetically by representative, like GNU uniq on sorted input.
	// Sort rows, not the groups, so groupOf's indices stay valid. Stable, so
	// groups with the same representative (possible with --best --rep
	// longest) keep creation order on every standard library.
	std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return *a.rep < *b.rep; });

	// Colour adds SGR codes around fields and changes nothing else, so the
	// output with codes stripped is byte-identical to the plain output.
	// Each row is built in one buffer and written once: no stream insertion
	// per field, and with colour off no copy of each member.
	const bool on = opts.color;
	const bool all = keepMembers();
	std::string row;
	for(const Row& r : rows) {
		const MatchGroup& group = *r.group;
		row.clear();
		if(opts.showTotals) {
			// Pad first, then colour, so codes don't disturb the alignment.
			// Frequency buckets make the hot lines of a log jump out.
			const size_t n = group.count;
			std::string count = std::to_string(n);
			if(count.size() < (size_t)countWidth) count.insert(0, countWidth - count.size(), ' ');
			const color::Style& style = n >= 100 ? color::BoldRed : n >= 10 ? color::Yellow
			                          : n >= 2 ? color::Green : color::Gray;
			color::paintTo(row, on, style, count);
			row += ' '; // uniq -c also separates count and line with one space
		}
		row += *r.rep;
		if(all) {
			for(size_t j = 0; j < group.matches.size(); j++) {
				if(j == r.repAt) continue;
				row += '\t';
				color::paintTo(row, on, color::Dim, group.matches[j]);
				if(opts.showScores) {
					// Green: well inside the threshold; yellow: near its edge.
					bool close = group.scores[j] <= opts.maxDistance / 2.0;
					row += ' ';
					color::paintTo(row, on, close ? color::Green : color::Yellow,
					               "(" + formatScore(group.scores[j]) + ")");
				}
			}
		}
		row += '\n';
		output->write(row.data(), row.size());
	}
}

// Whether the candidate matches group i, and its distance from the key in
// the method's units (edits; edits / longer length; 1 - cosine). The
// Levenshtein methods turn the threshold into an edit budget k and ask for
// the distance only up to k, so far-away keys are rejected without the full
// O(n*m) DP; the final comparisons are the same expressions as an unbounded
// computation, so results are identical.
inline bool Matcher::distanceTo(const Candidate& c, size_t i, similarity::Scratch& scratch,
                         double& score) const {
	const float d = opts.maxDistance;
	if(!(d >= 0)) return false; // negative or NaN: no distance qualifies
	const std::string& s1 = c.normalized;
	const std::string& s2 = groups[i].normalizedKey;

	if(opts.comparisonMethod == Cosine) {
		score = similarity::cosineDistance(c.profile, groups[i].profile);
		return score <= d;
	}

	const unsigned int maxLen = std::max(s1.size(), s2.size());
	if(opts.comparisonMethod != Levenshtein) { // normalized, token-sort
		if(maxLen == 0) { score = 0; return true; } // both empty: distance 0
		// One edit of slack so float rounding can't make k too tight;
		// the exact float test below still has the final say.
		double budget = (double)d * maxLen;
		unsigned int k = budget >= maxLen ? maxLen : (unsigned int)budget + 1;
		if(similarity::histogramBound(*c.histogram, keyHistograms[i]) > k) return false;
		unsigned int dist = similarity::boundedLevenshtein(s1, s2, k, scratch);
		float ratio = (float)dist / (float)maxLen;
		score = ratio;
		return ratio <= d;
	}

	unsigned int k = d >= maxLen ? maxLen : (unsigned int)d;
	if(similarity::histogramBound(*c.histogram, keyHistograms[i]) > k) return false;
	unsigned int dist = similarity::boundedLevenshtein(s1, s2, k, scratch);
	score = dist;
	return dist <= d;
}


#endif // FUNIQ_MATCHER_H
