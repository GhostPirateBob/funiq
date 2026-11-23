package main

import (
	"math"
	"math/rand/v2"
	"strings"
	"testing"
)

// referenceLevenshtein is the full (n+1) x (m+1) table, kept as plain as
// possible: an independent reference for the optimised implementations.
func referenceLevenshtein(a, b []byte) uint32 {
	d := make([][]uint32, len(a)+1)
	for i := range d {
		d[i] = make([]uint32, len(b)+1)
		d[i][0] = uint32(i)
	}
	for j := range d[0] {
		d[0][j] = uint32(j)
	}
	for i := 1; i <= len(a); i++ {
		for j := 1; j <= len(b); j++ {
			sub := d[i-1][j-1]
			if a[i-1] != b[j-1] {
				sub++
			}
			d[i][j] = min(sub, d[i-1][j]+1, d[i][j-1]+1)
		}
	}
	return d[len(a)][len(b)]
}

// randomPair: an edited copy (close, as funiq's matches are) or an unrelated
// string.
func randomPair(rng *rand.Rand, minLen, maxLen int, alphabet string) ([]byte, []byte) {
	randomString := func(n int) []byte {
		s := make([]byte, n)
		for i := range s {
			s[i] = alphabet[rng.IntN(len(alphabet))]
		}
		return s
	}
	a := randomString(minLen + rng.IntN(maxLen-minLen+1))
	if rng.IntN(2) == 0 {
		return a, randomString(minLen + rng.IntN(maxLen-minLen+1))
	}
	b := append([]byte(nil), a...)
	for edits := rng.IntN(20); edits > 0; edits-- {
		at := 0
		if len(b) > 0 {
			at = rng.IntN(len(b))
		}
		switch rng.IntN(3) {
		case 0:
			if len(b) > 0 {
				b[at] = alphabet[rng.IntN(len(alphabet))]
			}
		case 1:
			b = append(b[:at], append([]byte{alphabet[rng.IntN(len(alphabet))]}, b[at:]...)...)
		default:
			if len(b) > 0 {
				b = append(b[:at], b[at+1:]...)
			}
		}
	}
	return a, b
}

func capped(full, k uint32) uint32 {
	if full <= k {
		return full
	}
	return k + 1
}

func bounded(a, b string, k uint32) uint32 {
	return boundedLevenshtein([]byte(a), []byte(b), k, newScratch())
}

func TestExactDistances(t *testing.T) {
	exact := func(a, b string) uint32 { return bounded(a, b, uint32(max(len(a), len(b)))) }
	for _, c := range []struct {
		a, b string
		want uint32
	}{{"kitten", "sitting", 3}, {"flaw", "lawn", 2}, {"", "abc", 3}, {"same", "same", 0}} {
		if got := exact(c.a, c.b); got != c.want {
			t.Errorf("%q %q: %d, want %d", c.a, c.b, got, c.want)
		}
	}
}

func TestBoundedIsExactUpToKElseKPlus1(t *testing.T) {
	for _, c := range []struct {
		a, b    string
		k, want uint32
	}{{"kitten", "sitting", 3, 3}, {"kitten", "sitting", 2, 3}, {"", "abc", 5, 3}, {"abc", "", 1, 2},
		{"same", "same", 0, 0}, {"ab", "ba", 0, 1}, {"prefix-x-suffix", "prefix-yy-suffix", 2, 2}} {
		if got := bounded(c.a, c.b, c.k); got != c.want {
			t.Errorf("%q %q k=%d: %d, want %d", c.a, c.b, c.k, got, c.want)
		}
	}
}

func TestShortRandomPairsAgreeWithTheReference(t *testing.T) {
	rng := rand.New(rand.NewPCG(20261113, 1))
	s := newScratch()
	for range 20000 {
		a, b := randomPair(rng, 0, 24, "abc")
		k := uint32(rng.IntN(13))
		if got, want := boundedLevenshtein(a, b, k, s), capped(referenceLevenshtein(a, b), k); got != want {
			t.Fatalf("a=%q b=%q k=%d: %d, want %d", a, b, k, got, want)
		}
	}
}

func TestWordKernelAgreesWithTheReferenceOnItsDomain(t *testing.T) {
	rng := rand.New(rand.NewPCG(424243, 2))
	s := newScratch()
	for iter := range 20000 {
		alphabet := "abcdefghijklmnopqrstuvwxyz0123456789"
		if iter%2 == 1 {
			alphabet = "abc"
		}
		text, pattern := randomPair(rng, 1, 64, alphabet)
		if len(text) < len(pattern) {
			text, pattern = pattern, text
		}
		if len(pattern) == 0 {
			continue
		}
		k := uint32(rng.IntN(40))
		if got, want := levenshteinWord(text, pattern, k, s), capped(referenceLevenshtein(text, pattern), k); got != want {
			t.Fatalf("text=%q pattern=%q k=%d: %d, want %d", text, pattern, k, got, want)
		}
	}
}

func TestBandKernelAgreesWithTheReferenceOnItsDomain(t *testing.T) {
	rng := rand.New(rand.NewPCG(77, 3))
	s := newScratch()
	checked := 0
	for iter := range 20000 {
		alphabet := "abcdefghijklmnopqrstuvwxyz0123456789"
		if iter%2 == 1 {
			alphabet = "abc"
		}
		s1, s2 := randomPair(rng, 1, 200, alphabet)
		if len(s1) < len(s2) {
			s1, s2 = s2, s1
		}
		k := uint32(rng.IntN(32)) // 2k + 1 <= 64
		if int(k) > len(s2) || len(s1)-len(s2) > int(k) {
			continue // outside the band kernel's domain
		}
		if got, want := levenshteinBand(s1, s2, k, s), capped(referenceLevenshtein(s1, s2), k); got != want {
			t.Fatalf("s1=%q s2=%q k=%d: %d, want %d", s1, s2, k, got, want)
		}
		checked++
	}
	if checked < 5000 {
		t.Fatalf("only %d pairs in the band domain", checked)
	}
}

func TestDispatcherAgreesWithTheReferenceOnLongStrings(t *testing.T) {
	// every path with one scratch throughout, so dirty tables would show
	rng := rand.New(rand.NewPCG(20261123, 4))
	s := newScratch()
	for iter := range 6000 {
		alphabet := "abcdefghijklmnopqrstuvwxyz"
		if iter%2 == 1 {
			alphabet = "ab"
		}
		a, b := randomPair(rng, 0, 200, alphabet)
		k := uint32(rng.IntN(46))
		if got, want := boundedLevenshtein(a, b, k, s), capped(referenceLevenshtein(a, b), k); got != want {
			t.Fatalf("a=%q b=%q k=%d: %d, want %d", a, b, k, got, want)
		}
	}
}

func TestCosineBasics(t *testing.T) {
	d := func(a, b string) float64 {
		pa, pb := trigramProfile([]byte(a)), trigramProfile([]byte(b))
		return cosineDistance(&pa, &pb)
	}
	if math.Abs(d("abcdef", "abcdef")) > 1e-12 || math.Abs(d("abcdef", "uvwxyz")-1) > 1e-12 ||
		d("", "") != 0 || d("", "abc") != 1 || math.Abs(d("ab", "ab")) > 1e-12 || math.Abs(d("ab", "abc")-1) > 1e-12 {
		t.Fatal("cosine basics")
	}
}

func TestTrigramsArePackedExactly(t *testing.T) {
	eq := func(p profile, want []gram) bool {
		if len(p.grams) != len(want) {
			return false
		}
		for i := range want {
			if p.grams[i] != want[i] {
				return false
			}
		}
		return true
	}
	if !eq(trigramProfile([]byte("abab")), []gram{{0x616261, 1}, {0x626162, 1}}) ||
		!eq(trigramProfile([]byte("aaaa")), []gram{{0x616161, 2}}) ||
		!eq(trigramProfile([]byte("ab")), []gram{{1<<24 | 2<<16 | 0x61<<8 | 0x62, 1}}) ||
		!eq(trigramProfile([]byte("a")), []gram{{1<<24 | 1<<16 | 0x61<<8, 1}}) ||
		len(trigramProfile(nil).grams) != 0 || trigramProfile([]byte("aaaa")).norm != 2 {
		t.Fatal("trigram packing")
	}
}

func histogramBoundOf(a, b string) uint32 {
	var ha, hb byteHistogram
	buildHistogram([]byte(a), &ha)
	buildHistogram([]byte(b), &hb)
	return histogramBound(&ha, &hb)
}

func TestHistogramBoundIsTheLargerSurplus(t *testing.T) {
	cases := []struct {
		a, b string
		want uint32
	}{
		{"", "", 0},
		{"abc", "abd", 1},                   // one substitution
		{"aaaa", "", 4},                     // four deletions
		{"abc", "cba", 0},                   // an anagram: counts can't tell
		{"aab", "abbbb", 3},                 // surplus a:1, b:3
		{"\x80", "\x00", 0},                 // bytes fold to c & 127
		{strings.Repeat("a", 300), "", 255}, // counts saturate
	}
	for _, c := range cases {
		if got := histogramBoundOf(c.a, c.b); got != c.want {
			t.Errorf("histogramBound(%q, %q) = %d, want %d", c.a, c.b, got, c.want)
		}
	}
}

// The prefilter skips a pair when this bound exceeds the budget, so it must
// never exceed the edit distance or a real match would be lost.
func TestHistogramBoundNeverExceedsTheEditDistance(t *testing.T) {
	rng := rand.New(rand.NewPCG(20261124, 0))
	alphabets := []string{"ab", "abcdefghijklmnopqrstuvwxyz0123456789 .:/-", "a\x80\xff\x01\x7f"}
	for iter := range 20000 {
		maxLen := 80
		if iter%50 == 0 {
			maxLen = 700
		}
		a, b := randomPair(rng, 0, maxLen, alphabets[iter%3])
		if bound, full := histogramBoundOf(string(a), string(b)), referenceLevenshtein(a, b); bound > full {
			t.Fatalf("bound %d > distance %d for a=%q b=%q", bound, full, a, b)
		}
	}
}

// histogramExceeds is the matcher's fast form (one saturating subtract per
// word, early exit): it must answer exactly bound > k.
func TestHistogramExceedsIsBoundGreaterThanK(t *testing.T) {
	rng := rand.New(rand.NewPCG(20261125, 0))
	alphabets := []string{"ab", "abcdefghijklmnopqrstuvwxyz0123456789 .:/-", "a\x80\xff\x01\x7f"}
	for iter := range 20000 {
		maxLen := 80
		if iter%50 == 0 {
			maxLen = 700
		}
		a, b := randomPair(rng, 0, maxLen, alphabets[iter%3])
		var ha, hb byteHistogram
		buildHistogram(a, &ha)
		buildHistogram(b, &hb)
		bound := histogramBound(&ha, &hb)
		for _, k := range []uint32{0, 1, 3, bound - 1, bound, bound + 1, 5000} {
			if got := histogramExceeds(&ha, &hb, k); got != (bound > k) {
				t.Fatalf("histogramExceeds(k=%d) = %v, bound %d, a=%q b=%q", k, got, bound, a, b)
			}
		}
	}
}
