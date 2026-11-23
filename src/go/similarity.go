package main

import (
	"math"
	"slices"
)

// Distance functions, a line-by-line port of the C++ similarity.h: bounded
// Levenshtein (Hyyro's bit-parallel band and column kernels, banded DP
// otherwise) and character-trigram cosine distance. Techniques from
// rapidfuzz-cpp (MIT), as in the C++.

// bitParallelMinK: the kernels win from k = 1 against the banded DP
// (measured on the C++); k = 0 stays on the DP, which settles it in a row.
const bitParallelMinK = 1

// farPast is the position of the band kernel's untouched window entries.
const farPast = math.MinInt / 4

type window struct {
	at   int // position of bit 63
	bits uint64
}

// scratch is reusable working memory, one per goroutine, so the hot loop
// never allocates. The per-byte tables are back to their initial state after
// every call (each kernel resets only the entries it touched).
type scratch struct {
	rows   []uint32
	peq    [256]uint64 // column kernel: pattern positions of each byte
	window [256]window // band kernel: each byte's sliding window
}

func newScratch() *scratch {
	s := &scratch{}
	for i := range s.window {
		s.window[i] = window{at: farPast}
	}
	return s
}

func shr64(x uint64, s int) uint64 {
	if s < 64 {
		return x >> uint(s)
	}
	return 0
}

// levenshteinWord is Hyyro's bit-parallel Levenshtein (2003) for a pattern of
// 1 to 64 bytes: one word holds a whole DP column. It stops once the last
// row, which moves by at most one per remaining column, can't come back
// within k. Exact distance when <= k, else k + 1.
func levenshteinWord(text, pattern []byte, k uint32, s *scratch) uint32 {
	n, m := len(text), len(pattern)
	for j, c := range pattern {
		s.peq[c] |= 1 << uint(j)
	}
	vp, vn := ^uint64(0), uint64(0)
	last := uint64(1) << uint(m-1)
	dist := uint64(m)
	i := 0
	for ; i < n; i++ {
		x := s.peq[text[i]]
		d0 := (((x & vp) + vp) ^ vp) | x | vn
		hp := vn | ^(d0 | vp)
		hn := d0 & vp
		if hp&last != 0 {
			dist++
		}
		if hn&last != 0 {
			dist--
		}
		if dist > uint64(k)+uint64(n-1-i) {
			break
		}
		hp = hp<<1 | 1
		hn <<= 1
		vp = hn | ^(d0 | hp)
		vn = hp & d0
	}
	for _, c := range pattern {
		s.peq[c] = 0
	}
	if i < n || dist > uint64(k) {
		return k + 1
	}
	return uint32(dist)
}

// levenshteinBand is Hyyro's banded variant: the 2k + 1 diagonals fit one
// word (k <= 31), the pattern bits are a window sliding along s1, and it
// gives up once the diagonal, which never decreases, passes the bound.
// Requires s1 the longer, k <= len(s2), len(s1) - len(s2) <= k. Exact
// distance when <= k, else k + 1.
func levenshteinBand(s1, s2 []byte, k uint32, s *scratch) uint32 {
	n, m, ku := len(s1), len(s2), int(k)
	pm := &s.window
	inserted := 0 // s1 bytes added to the window so far
	insert := func(at int) {
		w := &pm[s1[inserted]]
		inserted++
		w.bits = shr64(w.bits, at-w.at) | 1<<63
		w.at = at
	}

	vp := ^uint64(0) << uint(64-ku-1)
	vn := uint64(0)
	dist := ku
	const diagonal = uint64(1) << 63
	horizontal := uint64(1) << 62
	// the score can fall along the horizontal stretch, never on the diagonal
	breakScore := 2*ku + m - n
	gaveUp := false

	for j := -ku; j < 0; j++ {
		insert(j)
	}
	i := 0
	for ; i < n-ku; i++ {
		insert(i)
		w := pm[s2[i]]
		x := shr64(w.bits, i-w.at)
		d0 := (((x & vp) + vp) ^ vp) | x | vn
		hp := vn | ^(d0 | vp)
		hn := d0 & vp
		if d0&diagonal == 0 {
			dist++
		}
		if dist > breakScore {
			gaveUp = true
			break
		}
		vp = hn | ^(d0>>1 | hp)
		vn = d0 >> 1 & hp
	}
	if !gaveUp {
		for ; i < m; i++ {
			if inserted < n {
				insert(i)
			}
			w := pm[s2[i]]
			x := shr64(w.bits, i-w.at)
			d0 := (((x & vp) + vp) ^ vp) | x | vn
			hp := vn | ^(d0 | vp)
			hn := d0 & vp
			if hp&horizontal != 0 {
				dist++
			}
			if hn&horizontal != 0 {
				dist--
			}
			horizontal >>= 1
			if dist > breakScore {
				gaveUp = true
				break
			}
			vp = hn | ^(d0>>1 | hp)
			vn = d0 >> 1 & hp
		}
	}
	for _, c := range s1[:inserted] {
		pm[c] = window{at: farPast}
	}
	if gaveUp || dist > ku {
		return k + 1
	}
	return uint32(dist)
}

// boundedLevenshtein returns the exact Levenshtein distance when it is <= k,
// otherwise k + 1: length bound, common affix strip, then the bit-parallel
// kernels where they fit, else a banded DP that aborts once a row exceeds k.
func boundedLevenshtein(s1, s2 []byte, k uint32, s *scratch) uint32 {
	a, b := s1, s2
	if len(a) < len(b) {
		a, b = b, a
	}
	if uint64(len(a)-len(b)) > uint64(k) {
		return k + 1
	}
	for len(b) > 0 && a[0] == b[0] {
		a, b = a[1:], b[1:]
	}
	for len(b) > 0 && a[len(a)-1] == b[len(b)-1] {
		a, b = a[:len(a)-1], b[:len(b)-1]
	}
	n, m := len(a), len(b)
	if m == 0 {
		return uint32(n) // n <= k, guaranteed by the length bound
	}

	if k >= bitParallelMinK {
		if int(k) <= m && 2*int(k)+1 <= 64 {
			return levenshteinBand(a, b, k, s)
		}
		if m <= 64 {
			return levenshteinWord(a, b, k, s)
		}
	}

	ku := int(k)
	if len(s.rows) < 2*(m+2) {
		s.rows = make([]uint32, 2*(m+2))
	}
	prev, cur := s.rows[:m+2], s.rows[m+2:2*(m+2)]
	inf := k + 1 // anything > k is "too far"
	// Row 0 only needs initialising inside the band: row i reads prev up to
	// column i + k, and every later row writes its own sentinel past its band.
	for j := 0; j <= min(m, ku+1); j++ {
		prev[j] = min(uint32(j), inf)
	}
	for i := 1; i <= n; i++ {
		lo := 1
		if i > ku {
			lo = i - ku
		}
		hi := min(m, i+ku)
		if lo == 1 {
			cur[0] = min(uint32(i), inf)
		} else {
			cur[lo-1] = inf
		}
		rowMin := cur[lo-1]
		ai := a[i-1]
		for j := lo; j <= hi; j++ {
			v := prev[j-1]
			if ai != b[j-1] {
				v++
			}
			v = min(v, prev[j]+1, cur[j-1]+1, inf)
			cur[j] = v
			rowMin = min(rowMin, v)
		}
		if hi < m {
			cur[hi+1] = inf
		}
		if rowMin > k {
			return k + 1
		}
		prev, cur = cur, prev
	}
	return min(prev[m], inf)
}

type gram struct{ key, count uint32 }

// profile holds character-trigram counts sorted by gram, and their norm.
type profile struct {
	grams []gram
	norm  float64
}

// trigramProfile packs trigrams exactly into 24 bits; a string of 1-2 bytes
// is one gram tagged above bit 24; the empty string has none.
func trigramProfile(s []byte) profile {
	var keys []uint32
	switch {
	case len(s) >= 3:
		keys = make([]uint32, 0, len(s)-2)
		for i := 0; i+2 < len(s); i++ {
			keys = append(keys, uint32(s[i])<<16|uint32(s[i+1])<<8|uint32(s[i+2]))
		}
	case len(s) > 0:
		second := uint32(0)
		if len(s) > 1 {
			second = uint32(s[1])
		}
		keys = []uint32{1<<24 | uint32(len(s))<<16 | uint32(s[0])<<8 | second}
	}
	slices.Sort(keys)
	var p profile
	sumSquares := 0.0
	for i := 0; i < len(keys); {
		j := i
		for j < len(keys) && keys[j] == keys[i] {
			j++
		}
		count := uint32(j - i)
		p.grams = append(p.grams, gram{keys[i], count})
		// the product of two integers is exact; the explicit conversion also
		// keeps the compiler from fusing it into an FMA on arm64
		sumSquares += float64(float64(count) * float64(count))
		i = j
	}
	p.norm = math.Sqrt(sumSquares)
	return p
}

// cosineDistance is 1 - cosine similarity, in [0, 1]: a sorted merge,
// accumulated in float64 in the same order as the C++. Two empty profiles
// are identical (0); an empty and a non-empty one share nothing (1).
func cosineDistance(a, b *profile) float64 {
	if a.norm == 0 || b.norm == 0 {
		if a.norm == 0 && b.norm == 0 {
			return 0
		}
		return 1
	}
	dot := 0.0
	for i, j := 0, 0; i < len(a.grams) && j < len(b.grams); {
		switch {
		case a.grams[i].key < b.grams[j].key:
			i++
		case b.grams[j].key < a.grams[i].key:
			j++
		default:
			dot += float64(float64(a.grams[i].count) * float64(b.grams[j].count))
			i++
			j++
		}
	}
	distance := 1 - dot/(a.norm*b.norm)
	if distance < 0 {
		return 0 // rounding can dip just below 0
	}
	return distance
}
