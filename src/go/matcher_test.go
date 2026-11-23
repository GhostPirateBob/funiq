package main

import (
	"bytes"
	"fmt"
	"math/rand/v2"
	"slices"
	"strings"
	"testing"
)

func render(m *Matcher) string {
	var out bytes.Buffer
	if err := m.PrintTo(&out); err != nil {
		panic(err)
	}
	return out.String()
}

func rowsOf(s Settings, lines ...string) []string {
	m := NewMatcher(s)
	for _, l := range lines {
		m.Add([]byte(l))
	}
	return strings.Split(strings.TrimSuffix(render(m), "\n"), "\n")
}

func expect(t *testing.T, got []string, want ...string) {
	t.Helper()
	if !slices.Equal(got, want) {
		t.Fatalf("got  %q\nwant %q", got, want)
	}
}

var ships = []string{"USS Enterprise", "uss enterprize", "U.S.S. Enterprise", "Enterprize", "USS Voyager",
	"USS Voyagr", "uss-voyager", "USS Defiant", "USS Defient", "Defiant"}

func TestReadmeShips(t *testing.T) {
	s := defaultSettings()
	s.CaseInsensitive, s.IgnoreNonAlnum = true, true
	expect(t, rowsOf(s, ships...), "Enterprize", "USS Defiant", "USS Enterprise", "USS Voyager")
	s.MaxDistance = 4
	expect(t, rowsOf(s, ships...), "USS Defiant", "USS Enterprise", "USS Voyager")
}

func TestNormalizedThresholdScalesWithLength(t *testing.T) {
	s := defaultSettings()
	s.Method, s.MaxDistance, s.ShowCounts = NormalizedLevenshtein, 0.25, true
	expect(t, rowsOf(s, "ab", "ac", "abcd", "abce"), "      1 ab", "      2 abcd", "      1 ac")
}

func TestARepeatedLineJoinsItsFirstOccurrencesGroup(t *testing.T) {
	s := defaultSettings()
	s.MaxDistance, s.ShowAll = 1, true
	expect(t, rowsOf(s, "aaaa", "zzzz", "aaab", "zzzz", "aaab"), "aaaa\taaab\taaab", "zzzz\tzzzz")
}

func TestShowAllPrintsMembersAsWritten(t *testing.T) {
	s := defaultSettings()
	s.CaseInsensitive, s.IgnoreNonAlnum, s.ShowAll = true, true, true
	expect(t, rowsOf(s, "Hello, World!", "HELLO WORLD"), "Hello, World!\tHELLO WORLD")
}

func TestALineJoinsOnlyTheFirstGroupItMatches(t *testing.T) {
	s := defaultSettings()
	s.MaxDistance, s.ShowCounts = 1, true
	expect(t, rowsOf(s, "aa", "bb", "ab"), "      2 aa", "      1 bb")
}

func TestMaskDigits(t *testing.T) {
	s := defaultSettings()
	s.MaxDistance, s.ShowCounts, s.MaskDigits = 0, true, true
	expect(t, rowsOf(s, "pid 123 exited at 10:42:07", "pid 98765 exited at 09:01:59", "pid exited"),
		"      2 pid 123 exited at 10:42:07", "      1 pid exited")
}

func TestTokenSortIgnoresWordOrder(t *testing.T) {
	s := defaultSettings()
	s.Method, s.MaxDistance, s.ShowCounts = TokenSort, 0, true
	expect(t, rowsOf(s, "connection reset by peer", "peer by reset connection", "connection refused"),
		"      1 connection refused", "      2 connection reset by peer")
	s.IgnoreNonAlnum = true
	expect(t, rowsOf(s, "user=bob,ip=10.0.0.1", "ip=10.0.0.1 user=bob"), "      2 user=bob,ip=10.0.0.1")
}

func TestTokenSortSplitsOnCWhitespaceOnly(t *testing.T) {
	s := defaultSettings()
	s.Method, s.MaxDistance, s.ShowCounts = TokenSort, 0, true
	if n := len(rowsOf(s, "b\x0ba", "a b")); n != 1 {
		t.Fatalf("vertical tab: %d groups, want 1", n)
	}
	if n := len(rowsOf(s, "b a", "a b")); n != 2 {
		t.Fatalf("U+00A0: %d groups, want 2", n)
	}
}

func TestCosineMatchesReorderedFields(t *testing.T) {
	lines := []string{"user=alice action=login status=ok", "status=ok action=login user=alice", "disk full on /dev/sda1"}
	s := defaultSettings()
	s.MaxDistance, s.Method = 0.3, NormalizedLevenshtein
	if n := len(rowsOf(s, lines...)); n != 3 {
		t.Fatalf("normalized: %d groups", n)
	}
	s.Method = Cosine
	if n := len(rowsOf(s, lines...)); n != 2 {
		t.Fatalf("cosine: %d groups", n)
	}
}

func TestRepLongest(t *testing.T) {
	s := defaultSettings()
	s.ShowCounts, s.Rep = true, RepLongest
	expect(t, rowsOf(s, "abc", "abcdef", "abcd"), "      3 abcdef")
}

func TestScores(t *testing.T) {
	s := defaultSettings()
	s.MaxDistance, s.ShowScores = 2, true
	expect(t, rowsOf(s, "hello", "hallo", "help!"), "hello\thallo (1)\thelp! (2)")
	n := defaultSettings()
	n.Method, n.MaxDistance, n.ShowScores = NormalizedLevenshtein, 0.5, true
	expect(t, rowsOf(n, "abcd", "abce"), "abcd\tabce (0.250)")
	n.MaxDistance = 0.1 // 1/16 = 0.0625: ties round to even, like C++
	expect(t, rowsOf(n, "abcdefghijklmnop", "abcdefghijklmnoX"), "abcdefghijklmnop\tabcdefghijklmnoX (0.062)")
}

func TestBestJoinsTheClosestGroup(t *testing.T) {
	s := defaultSettings()
	s.MaxDistance, s.ShowCounts = 2, true
	expect(t, rowsOf(s, "aaaaaa", "aaabbb", "aaaabb"), "      2 aaaaaa", "      1 aaabbb")
	s.Best = true
	expect(t, rowsOf(s, "aaaaaa", "aaabbb", "aaaabb"), "      1 aaaaaa", "      2 aaabbb")
}

func TestNegativeThresholdMatchesNothingNotEvenRepeats(t *testing.T) {
	s := defaultSettings()
	s.MaxDistance, s.ShowCounts = -1, true
	expect(t, rowsOf(s, "x", "x"), "      1 x", "      1 x")
}

func TestEqualRepresentativesKeepCreationOrder(t *testing.T) {
	s := defaultSettings()
	s.MaxDistance, s.Best, s.Rep, s.ShowCounts, s.ShowAll = 2, true, RepLongest, true, true
	block := []string{"bba", "baab", "a", "aab", "aaaaa", "baab"}
	expect(t, rowsOf(s, block...), "      1 aaaaa", "      3 baab\tbba\ta", "      2 baab\taab")
	var lines, prefixes []string
	for b := range 40 {
		p := strings.Repeat(string(rune('0'+b)), 4)
		prefixes = append(prefixes, p)
		for _, l := range block {
			lines = append(lines, p+l)
		}
	}
	out := rowsOf(s, lines...)
	if len(out) != 120 {
		t.Fatalf("%d rows", len(out))
	}
	at := func(start string) int {
		return slices.IndexFunc(out, func(r string) bool { return strings.HasPrefix(r, start) })
	}
	for _, p := range prefixes {
		if at("      3 "+p+"baab\t") >= at("      2 "+p+"baab\t") {
			t.Fatalf("prefix %s out of order", p)
		}
	}
}

func templateLines(rng *rand.Rand, templates []string, count, editOneIn int) [][]byte {
	lines := make([][]byte, count)
	for i := range lines {
		s := []byte(templates[rng.IntN(len(templates))])
		for j, c := range s {
			if c == '#' {
				s[j] = byte('0' + rng.IntN(10))
			}
		}
		if rng.IntN(editOneIn) == 0 && len(s) > 0 {
			s[rng.IntN(len(s))] = byte('a' + rng.IntN(26))
		}
		lines[i] = s
	}
	return lines
}

// runMatcher: serial Add (threads 0) or AddBatch with random chunk sizes,
// empty chunks included.
func runMatcher(s Settings, lines [][]byte, threads int, rng *rand.Rand) string {
	s.Threads = max(threads, 1)
	m := NewMatcher(s)
	if threads == 0 {
		for _, l := range lines {
			m.Add(l)
		}
	} else {
		for pos := 0; pos < len(lines); {
			n := min(len(lines)-pos, rng.IntN(40))
			m.AddBatch(lines[pos : pos+n])
			pos += n
		}
	}
	return render(m)
}

func TestAddBatchWithThreadsEqualsSerialAdd(t *testing.T) {
	rng := rand.New(rand.NewPCG(424242, 5))
	lines := templateLines(rng, []string{"error at line ##", "warning: x## not found", "GET /a/## 200", "ok"}, 600, 5)
	cases := []struct {
		method                 Method
		d                      float32
		ci, best, mask, scores bool
	}{{Levenshtein, 3, false, false, false, false}, {Levenshtein, 1, true, false, false, false},
		{NormalizedLevenshtein, 0.125, false, false, false, false}, {NormalizedLevenshtein, 0.3, true, false, false, false},
		{Levenshtein, 3, false, true, false, true}, {NormalizedLevenshtein, 0.3, true, true, false, false},
		{Levenshtein, 2, false, false, true, false}, {TokenSort, 0.2, false, false, false, true},
		{Cosine, 0.2, false, false, false, true}, {Cosine, 0.3, true, true, true, false}}
	for _, c := range cases {
		s := defaultSettings()
		s.Method, s.MaxDistance, s.CaseInsensitive, s.Best, s.MaskDigits, s.ShowScores = c.method, c.d, c.ci, c.best, c.mask, c.scores
		s.ShowAll, s.ShowCounts = true, true
		serial := runMatcher(s, lines, 0, rng)
		for _, threads := range []int{1, 2, 3, 8} {
			if got := runMatcher(s, lines, threads, rng); got != serial {
				t.Fatalf("%+v threads=%d differs from serial", c, threads)
			}
		}
	}
}

func TestCountsOnlyRunsEqualShowAllRuns(t *testing.T) {
	rng := rand.New(rand.NewPCG(777, 6))
	lines := templateLines(rng, []string{"GET /x/## 200", "error ## at line ##", "ok", "warning: v## is deprecated"}, 500, 1000)
	for i := range lines {
		if rng.IntN(4) == 0 {
			lines[i] = append(lines[i], bytes.Repeat([]byte("z"), rng.IntN(6))...)
		}
	}
	for _, method := range []Method{Levenshtein, NormalizedLevenshtein, Cosine} {
		for _, best := range []bool{false, true} {
			for _, rep := range []Rep{RepFirst, RepLongest} {
				counted := defaultSettings()
				counted.Method, counted.Best, counted.Rep, counted.ShowCounts = method, best, rep, true
				counted.MaxDistance = 0.2
				if method == Levenshtein {
					counted.MaxDistance = 2
				}
				listed := counted
				listed.ShowAll = true
				var cut strings.Builder
				for _, r := range strings.SplitAfter(runMatcher(listed, lines, 3, rng), "\n") {
					if r != "" {
						fmt.Fprintf(&cut, "%s\n", strings.SplitN(strings.TrimSuffix(r, "\n"), "\t", 2)[0])
					}
				}
				for _, threads := range []int{3, 0} {
					if got := runMatcher(counted, lines, threads, rng); got != cut.String() {
						t.Fatalf("method=%v best=%v rep=%v threads=%d", method, best, rep, threads)
					}
				}
			}
		}
	}
}
