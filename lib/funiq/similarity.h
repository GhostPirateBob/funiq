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
}

#endif //FUNIQ_SIMILARITY_H
