#ifndef FUNIQ_SIMILARITY_H
#define FUNIQ_SIMILARITY_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace similarity {

	unsigned int levenshteinDistance(const std::string& s1, const std::string& s2) {
		unsigned int len1 = s1.size();
		unsigned int len2 = s2.size();

		// Early exit: if either string is empty the distance is the
		// length of the other string.
		if(len1 == 0) return len2;
		if(len2 == 0) return len1;

		// Keep the shorter string as s2 to minimise the column vector size
		if(len1 < len2)
			return levenshteinDistance(s2, s1);

		std::vector<unsigned int> col(len2+1);
		std::vector<unsigned int> prevCol(len2+1);

		for (unsigned int i = 0; i < prevCol.size(); i++) {
			prevCol[i] = i;
		}
		for (unsigned int i = 0; i < len1; i++) {
			col[0] = i+1;
			for (unsigned int j = 0; j < len2; j++) {
				col[j+1] = std::min(
						std::min( 1 + col[j], 1 + prevCol[1 + j]),
						prevCol[j] + (s1[i]==s2[j] ? 0 : 1)
				);
			}
			col.swap(prevCol);
		}
		return prevCol[len2];
	}

	float normalizedLevenshtein(const std::string& s1, const std::string& s2) {
		// Identical strings are always distance 0, and avoids division
		// by zero when both strings are empty.
		if(s1 == s2) return 0.0f;

		unsigned int editDistance = levenshteinDistance(s1, s2);
		unsigned int maxLength = std::max(s1.length(), s2.length());
		return (float)editDistance / (float)maxLength;
	}

	// Levenshtein distance with a cutoff: returns the exact distance when it
	// is <= k, otherwise k + 1. Almost every comparison funiq makes is a
	// non-match against a far-away key, so rejecting early is what matters:
	//  1. length bound: at least |len1 - len2| insertions/deletions needed
	//  2. common prefix/suffix never affect the distance; strip them
	//  3. Ukkonen band: only cells with |i - j| <= k can stay <= k
	//  4. abort as soon as a whole row exceeds k (rows never decrease)
	// Technique follows rapidfuzz-cpp's uniform_levenshtein_distance (MIT).
	// `scratch` holds the two DP rows; pass the same vector to every call
	// (one per thread) so the hot loop never allocates. Deliberately not a
	// thread_local: MinGW static builds can run non-trivial thread_local
	// destructors after their TLS storage is freed, corrupting the heap.
	unsigned int boundedLevenshtein(const std::string& s1, const std::string& s2, unsigned int k,
	                                std::vector<unsigned int>& scratch) {
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

		if(scratch.size() < 2 * (m + 2)) scratch.resize(2 * (m + 2));
		unsigned int* prev = scratch.data();
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

	// Convenience overload for one-off calls; allocates its own rows.
	unsigned int boundedLevenshtein(const std::string& s1, const std::string& s2, unsigned int k) {
		std::vector<unsigned int> scratch;
		return boundedLevenshtein(s1, s2, k, scratch);
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
	Profile trigramProfile(const std::string& s) {
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
	double cosineDistance(const Profile& a, const Profile& b) {
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
