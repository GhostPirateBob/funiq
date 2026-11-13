#ifndef FUNIQ_SIMILARITY_H
#define FUNIQ_SIMILARITY_H

#include <algorithm>
#include <string>
#include <vector>
#include <cmath>

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
	unsigned int boundedLevenshtein(const std::string& s1, const std::string& s2, unsigned int k) {
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

		// Reused across calls: no allocation in the hot loop.
		static std::vector<unsigned int> prev, cur;
		if(prev.size() < m + 2) { prev.resize(m + 2); cur.resize(m + 2); }
		const unsigned int INF = k + 1; // anything > k is "too far"

		for(unsigned int j = 0; j <= m; j++) prev[j] = std::min(j, INF);
		prev[m + 1] = INF;
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
			prev.swap(cur);
		}
		return std::min(prev[m], INF);
	}
}

#endif //FUNIQ_SIMILARITY_H
