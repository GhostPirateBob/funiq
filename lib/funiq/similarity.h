#ifndef FUNIQ_SIMILARITY_H
#define FUNIQ_SIMILARITY_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace similarity {

	// The bit-parallel kernels win from k = 1 (measured: -d 3 on 24k log lines
	// 5.5s -> 3.4s, -d 1 1.76s -> 1.61s); rapidfuzz waits until 4 because
	// below that it uses mbleven, which funiq doesn't have. k = 0 stays on the
	// DP, which settles it in one row.
	const unsigned int BIT_PARALLEL_MIN_K = 1;
	// Position of the band kernel's untouched window entries (see Scratch).
	const std::ptrdiff_t FAR_PAST = std::numeric_limits<std::ptrdiff_t>::min() / 4;

	// Reusable working memory for the distance functions, one per thread so
	// the hot loop never allocates. Deliberately not a thread_local: MinGW
	// static builds can run non-trivial thread_local destructors after their
	// TLS storage is freed, corrupting the heap. The per-byte tables are back
	// to their initial state after every call (each kernel resets only the
	// entries it touched), so no call pays for clearing all 256.
	struct Scratch {
		// Bits of one byte in the band kernel's sliding window; bit 63 is
		// position `at`. Untouched entries sit far in the past, so any shift
		// pushes their (empty) bits out.
		struct Window { std::ptrdiff_t at; uint64_t bits; };
		std::vector<unsigned int> rows;  // banded DP: two rows
		uint64_t peq[256];               // single-word kernel: pattern positions of each byte
		Window window[256];
		Scratch() {
			for(auto& p : peq) p = 0;
			for(auto& w : window) w = Window{FAR_PAST, 0};
		}
	};

	inline uint64_t shr64(uint64_t x, std::ptrdiff_t s) { return s < 64 ? x >> s : 0; }

	// Hyyro's bit-parallel Levenshtein (2003) for a pattern of 1 to 64 bytes:
	// one 64-bit word holds a whole DP column, so each text byte costs a
	// dozen word operations. Stops once the last row, which moves by at most
	// one per remaining column, can no longer come back within k. Exact
	// distance when <= k, else k + 1. Technique from rapidfuzz-cpp (MIT).
	inline unsigned int levenshteinWord(const unsigned char* text, unsigned int n,
	                                    const unsigned char* pattern, unsigned int m,
	                                    unsigned int k, Scratch& scratch) {
		uint64_t* peq = scratch.peq;
		for(unsigned int j = 0; j < m; j++) peq[pattern[j]] |= uint64_t(1) << j;
		uint64_t VP = ~uint64_t(0), VN = 0;
		const uint64_t last = uint64_t(1) << (m - 1);
		unsigned int dist = m;
		unsigned int i = 0;
		for(; i < n; i++) {
			const uint64_t X = peq[text[i]];
			const uint64_t D0 = (((X & VP) + VP) ^ VP) | X | VN;
			uint64_t HP = VN | ~(D0 | VP);
			uint64_t HN = D0 & VP;
			dist += (HP & last) != 0;
			dist -= (HN & last) != 0;
			if(dist > k + (n - 1 - i)) break;
			HP = (HP << 1) | 1;
			HN <<= 1;
			VP = HN | ~(D0 | HP);
			VN = HP & D0;
		}
		for(unsigned int j = 0; j < m; j++) peq[pattern[j]] = 0;
		return (i < n || dist > k) ? k + 1 : dist;
	}

	// Hyyro's banded variant for long strings: the 2k + 1 diagonals around the
	// main one fit one 64-bit word (k <= 31), and the pattern bits are kept as
	// a window sliding along s1, so there is no table of s1 to build. Gives up
	// as soon as the diagonal, which never decreases, passes the bound.
	// Requires s1 the longer, k <= len(s2) and len(s1) - len(s2) <= k. Exact
	// distance when <= k, else k + 1. Technique from rapidfuzz-cpp (MIT).
	inline unsigned int levenshteinBand(const unsigned char* s1, unsigned int n,
	                                    const unsigned char* s2, unsigned int m,
	                                    unsigned int k, Scratch& scratch) {
		Scratch::Window* pm = scratch.window;
		unsigned int inserted = 0; // s1 bytes added to the window so far
		auto insert = [&](std::ptrdiff_t at) {
			Scratch::Window& w = pm[s1[inserted++]];
			w.bits = shr64(w.bits, at - w.at) | (uint64_t(1) << 63);
			w.at = at;
		};
		auto lookup = [&](unsigned char c, std::ptrdiff_t at) { return shr64(pm[c].bits, at - pm[c].at); };

		uint64_t VP = ~uint64_t(0) << (64 - k - 1);
		uint64_t VN = 0;
		unsigned int dist = k;
		const uint64_t diagonal = uint64_t(1) << 63;
		uint64_t horizontal = uint64_t(1) << 62;
		// the score can fall along the horizontal stretch, never on the diagonal
		const unsigned int breakScore = 2 * k + m - n;
		bool gaveUp = false;

		for(std::ptrdiff_t j = -(std::ptrdiff_t)k; j < 0; j++) insert(j);
		unsigned int i = 0;
		for(; i < n - k; i++) {
			insert(i);
			const uint64_t X = lookup(s2[i], i);
			const uint64_t D0 = (((X & VP) + VP) ^ VP) | X | VN;
			const uint64_t HP = VN | ~(D0 | VP);
			const uint64_t HN = D0 & VP;
			dist += !(D0 & diagonal);
			if(dist > breakScore) { gaveUp = true; break; }
			VP = HN | ~((D0 >> 1) | HP);
			VN = (D0 >> 1) & HP;
		}
		if(!gaveUp) {
			for(; i < m; i++) {
				if(inserted < n) insert(i);
				const uint64_t X = lookup(s2[i], i);
				const uint64_t D0 = (((X & VP) + VP) ^ VP) | X | VN;
				const uint64_t HP = VN | ~(D0 | VP);
				const uint64_t HN = D0 & VP;
				dist += (HP & horizontal) != 0;
				dist -= (HN & horizontal) != 0;
				horizontal >>= 1;
				if(dist > breakScore) { gaveUp = true; break; }
				VP = HN | ~((D0 >> 1) | HP);
				VN = (D0 >> 1) & HP;
			}
		}
		for(unsigned int t = 0; t < inserted; t++) pm[s1[t]] = Scratch::Window{FAR_PAST, 0};
		return (gaveUp || dist > k) ? k + 1 : dist;
	}

	// Levenshtein distance with a cutoff: returns the exact distance when it
	// is <= k, otherwise k + 1. Almost every comparison funiq makes is a
	// non-match against a far-away key, so rejecting early is what matters:
	//  1. length bound: at least |len1 - len2| insertions/deletions needed
	//  2. common prefix/suffix never affect the distance; strip them
	//  3. bit-parallel kernels (k = 1..31 in a 64-bit band, or a short side
	//     of at most 64 bytes): one word of the DP per step, so the cost no
	//     longer grows with k (the normalized methods' k is ~12% of the line)
	//  4. otherwise a banded DP (Ukkonen) that aborts once a whole row
	//     exceeds k
	// Technique follows rapidfuzz-cpp's uniform_levenshtein_distance (MIT).
	inline unsigned int boundedLevenshtein(const std::string& s1, const std::string& s2, unsigned int k,
	                                Scratch& scratch) {
		const std::string* longer = &s1;
		const std::string* shorter = &s2;
		if(longer->size() < shorter->size()) std::swap(longer, shorter);
		const char* a = longer->data();
		const char* b = shorter->data();
		unsigned int n = longer->size();
		unsigned int m = shorter->size();

		if(n - m > k) return k + 1;

		while(m > 0 && *a == *b) { a++; b++; n--; m--; }
		while(m > 0 && a[n - 1] == b[m - 1]) { n--; m--; }
		if(m == 0) return n; // n <= k, guaranteed by the length bound

		const unsigned char* ua = reinterpret_cast<const unsigned char*>(a);
		const unsigned char* ub = reinterpret_cast<const unsigned char*>(b);
		if(k >= BIT_PARALLEL_MIN_K) {
			if(k <= m && 2 * k + 1 <= 64) return levenshteinBand(ua, n, ub, m, k, scratch);
			if(m <= 64) return levenshteinWord(ua, n, ub, m, k, scratch);
		}

		std::vector<unsigned int>& rows = scratch.rows;
		if(rows.size() < 2 * (m + 2)) rows.resize(2 * (m + 2));
		unsigned int* prev = rows.data();
		unsigned int* cur = prev + (m + 2);
		const unsigned int INF = k + 1; // anything > k is "too far"

		// Row 0 only needs initialising inside the band: row i reads prev up
		// to column i + k, and every later row writes its own INF sentinel
		// just past its band. With early abort most calls finish within a
		// few rows, so initialising all m + 1 cells used to dominate.
		const unsigned int initEnd = std::min(m, k + 1);
		for(unsigned int j = 0; j <= initEnd; j++) prev[j] = std::min(j, INF);
		for(unsigned int i = 1; i <= n; i++) {
			unsigned int lo = i > k ? i - k : 1;
			unsigned int hi = std::min(m, i + k);
			cur[lo - 1] = (lo == 1) ? std::min(i, INF) : INF;
			unsigned int rowMin = cur[lo - 1];
			for(unsigned int j = lo; j <= hi; j++) {
				unsigned int v = prev[j - 1] + (a[i - 1] != b[j - 1] ? 1 : 0);
				v = std::min(v, prev[j] + 1);
				v = std::min(v, cur[j - 1] + 1);
				cur[j] = std::min(v, INF);
				rowMin = std::min(rowMin, cur[j]);
			}
			if(hi < m) cur[hi + 1] = INF;
			if(rowMin > k) return k + 1;
			std::swap(prev, cur);
		}
		return std::min(prev[m], INF);
	}

	// Convenience overload for one-off calls; allocates its own scratch.
	inline unsigned int boundedLevenshtein(const std::string& s1, const std::string& s2, unsigned int k) {
		Scratch scratch;
		return boundedLevenshtein(s1, s2, k, scratch);
	}

	// Exact distance: the bounded version with a budget no pair can exceed.
	inline unsigned int levenshteinDistance(const std::string& a, const std::string& b) {
		return boundedLevenshtein(a, b, (unsigned int)std::max(a.size(), b.size()));
	}

	// Edits per character of the longer string: 0.0 (equal) to 1.0.
	inline float normalizedLevenshtein(const std::string& a, const std::string& b) {
		const size_t longer = std::max(a.size(), b.size());
		return longer == 0 ? 0.0f : (float)levenshteinDistance(a, b) / (float)longer;
	}

	// Character-trigram count vector, sorted by gram, plus its Euclidean norm.
	// Built once per line/key so each cosine comparison is a linear merge.
	struct Profile {
		std::vector<std::pair<uint32_t, unsigned int>> grams;
		double norm = 0;
	};

	// Trigrams are packed exactly into 24 bits (no hashing, no collisions).
	// A string shorter than 3 is one gram of its own, tagged above bit 24 so
	// it can't equal a real trigram; the empty string has no grams.
	inline Profile trigramProfile(const std::string& s) {
		std::vector<uint32_t> keys;
		const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
		if(s.size() >= 3) {
			keys.reserve(s.size() - 2);
			for(size_t i = 0; i + 2 < s.size(); i++)
				keys.push_back((uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2]);
		} else if(!s.empty()) {
			keys.push_back(1u << 24 | (uint32_t)s.size() << 16 | (uint32_t)p[0] << 8 | (s.size() > 1 ? p[1] : 0));
		}
		std::sort(keys.begin(), keys.end());
		Profile profile;
		double sumSquares = 0;
		for(size_t i = 0; i < keys.size(); ) {
			size_t j = i;
			while(j < keys.size() && keys[j] == keys[i]) j++;
			unsigned int count = j - i;
			profile.grams.push_back(std::make_pair(keys[i], count));
			sumSquares += (double)count * count;
			i = j;
		}
		profile.norm = std::sqrt(sumSquares);
		return profile;
	}

	// 1 - cosine similarity, in [0, 1]. Two empty profiles are identical (0);
	// an empty and a non-empty one share nothing (1).
	inline double cosineDistance(const Profile& a, const Profile& b) {
		if(a.norm == 0 || b.norm == 0) return (a.norm == 0 && b.norm == 0) ? 0.0 : 1.0;
		double dot = 0;
		size_t i = 0, j = 0;
		while(i < a.grams.size() && j < b.grams.size()) {
			if(a.grams[i].first < b.grams[j].first) i++;
			else if(b.grams[j].first < a.grams[i].first) j++;
			else { dot += (double)a.grams[i].second * b.grams[j].second; i++; j++; }
		}
		double distance = 1.0 - dot / (a.norm * b.norm);
		return distance < 0 ? 0.0 : distance; // rounding can dip just below 0
	}
}

#endif //FUNIQ_SIMILARITY_H
