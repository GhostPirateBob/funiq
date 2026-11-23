#ifndef FUNIQ_PIECEINDEX_H
#define FUNIQ_PIECEINDEX_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

// Pigeonhole index for a fixed edit budget k: which group keys could be within
// k edits of a line, without visiting every group.
//
// Each key is split into k + 1 pieces. k edits touch at most k of them, so a
// line within k edits of the key contains at least one piece unchanged. That
// piece sits shifted by delta (insertions minus deletions before it), and the
// edits before and after it share the budget: |delta| + |(n - L) - delta| <= k
// for a line of length n and a key of length L. candidates() looks up the
// substrings at those positions, so its answer contains every key within k
// edits (more, on hash collisions or chance matches; the matcher verifies).
// Keys too short to split are always candidates.
class PieceIndex {
public:
	explicit PieceIndex(unsigned k = 0) : k(k) {}

	unsigned budget() const { return k; }

	// Ids must be added in increasing order (groups are numbered as created).
	void add(const std::string& key, uint32_t id) {
		const size_t L = key.size();
		if(L < (size_t)k + 1) { shortKeys.push_back(id); return; }
		for(unsigned i = 0; i <= k; i++) {
			size_t start = pieceStart(L, i), end = pieceStart(L, i + 1);
			pieces[pieceHash(key.data() + start, end - start, L, i)].push_back(id);
		}
	}

	// Every id in [from, to) whose key could be within k edits of s, sorted
	// and without duplicates. `out` is reused (no allocation once grown).
	void candidates(const std::string& s, size_t from, size_t to, std::vector<uint32_t>& out) const {
		out.clear();
		appendRange(shortKeys, from, to, out);
		const long n = (long)s.size(), K = (long)k;
		for(long L = std::max(n - K, K + 1); L <= n + K; L++) {
			const long D = n - L; // the line is D bytes longer than the key
			for(unsigned i = 0; i <= k; i++) {
				const long start = (long)pieceStart(L, i), len = (long)pieceStart(L, i + 1) - start;
				for(long delta = -K; delta <= K; delta++) {
					if(std::labs(delta) + std::labs(D - delta) > K) continue;
					const long at = start + delta;
					if(at < 0 || at + len > n) continue;
					auto it = pieces.find(pieceHash(s.data() + at, (size_t)len, (size_t)L, i));
					if(it != pieces.end()) appendRange(it->second, from, to, out);
				}
			}
		}
		std::sort(out.begin(), out.end());
		out.erase(std::unique(out.begin(), out.end()), out.end());
	}

private:
	unsigned k;
	std::unordered_map<uint64_t, std::vector<uint32_t>> pieces; // (L, piece, bytes) -> ids, increasing
	std::vector<uint32_t> shortKeys;                              // keys under k + 1 bytes, increasing

	size_t pieceStart(size_t L, unsigned i) const { return L * i / (k + 1); }

	// FNV-1a over the bytes, mixed with the key length and piece number, then
	// a splitmix64 finish (unordered_map's std::hash<uint64_t> is the
	// identity). A collision only adds candidates.
	static uint64_t pieceHash(const char* p, size_t len, size_t L, unsigned piece) {
		uint64_t h = 1469598103934665603ULL;
		for(size_t i = 0; i < len; i++) h = (h ^ (unsigned char)p[i]) * 1099511628211ULL;
		h ^= (uint64_t)L * 0x9E3779B97F4A7C15ULL;
		h ^= (uint64_t)(piece + 1) * 0xC2B2AE3D27D4EB4FULL;
		h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ULL;
		h ^= h >> 27; h *= 0x94D049BB133111EBULL;
		return h ^ (h >> 31);
	}

	static void appendRange(const std::vector<uint32_t>& ids, size_t from, size_t to, std::vector<uint32_t>& out) {
		for(auto it = std::lower_bound(ids.begin(), ids.end(), (uint32_t)from); it != ids.end() && *it < to; ++it)
			out.push_back(*it);
	}
};

#endif // FUNIQ_PIECEINDEX_H
