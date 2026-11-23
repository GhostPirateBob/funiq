package main

import (
	"bytes"
	"io"
	"slices"
	"strconv"
	"sync"
	"sync/atomic"
)

// Grouping, a port of the C++ Matcher.h: normalisation, the memo,
// first-match / --best grouping, two-phase batches on goroutines,
// counts-only storage and output. Results never depend on the thread count.

const noGroup = -1

// hit is the group a line joins (noGroup: start a new one) and its distance.
type hit struct {
	group int
	score float64
}

var miss = hit{group: noGroup}

// candidate is a line prepared for comparison: normalized and profiled once.
type candidate struct {
	normalized []byte
	profile    profile
}

// group: the first line (the key matching compares against), its normalized
// form and profile, the size, and members only when they are printed.
type group struct {
	key, normalizedKey []byte
	profile            profile
	count              int
	longest            []byte
	longestAt          int
	matches            [][]byte
	scores             []float64
}

// Matcher groups near-duplicate lines.
type Matcher struct {
	opts       Settings
	groups     []group
	groupOf    map[string]hit // normalized line -> where it went (first-match only)
	keyLengths []uint32       // keyLengths[i] == len(groups[i].normalizedKey)
	scratch    *scratch
	workers    []*scratch // one per worker goroutine, kept across batches
}

// NewMatcher returns an empty matcher.
func NewMatcher(s Settings) *Matcher {
	return &Matcher{opts: s, groupOf: map[string]hit{}, scratch: newScratch()}
}

func isCSpace(c byte) bool {
	return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'
}

func isAlnum(c byte) bool {
	return c >= '0' && c <= '9' || c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z'
}

// normalize returns the string actually compared: lowercase (-i), digit runs
// as one 0 (-D), then sorted words (token-sort; with -I punctuation separates
// words) or non-alphanumerics deleted (-I). ASCII only, like <cctype>.
func normalize(line []byte, s *Settings) []byte {
	r := append([]byte(nil), line...)
	if s.CaseInsensitive {
		for i, c := range r {
			if c >= 'A' && c <= 'Z' {
				r[i] = c + 'a' - 'A'
			}
		}
	}
	if s.MaskDigits {
		masked := r[:0]
		for i, c := range r {
			digit := c >= '0' && c <= '9'
			if digit && i > 0 && r[i-1] >= '0' && r[i-1] <= '9' {
				continue
			}
			if digit {
				c = '0'
			}
			masked = append(masked, c) // never overtakes i, so r[i-1] is still unread
		}
		r = masked
	}
	if s.Method == TokenSort {
		if s.IgnoreNonAlnum {
			for i, c := range r {
				if !isAlnum(c) {
					r[i] = ' '
				}
			}
		}
		tokens := bytes.FieldsFunc(r, func(c rune) bool { return c < 0x80 && isCSpace(byte(c)) })
		slices.SortFunc(tokens, bytes.Compare)
		return bytes.Join(tokens, []byte{' '})
	}
	if s.IgnoreNonAlnum {
		kept := r[:0]
		for _, c := range r {
			if isAlnum(c) {
				kept = append(kept, c)
			}
		}
		r = kept
	}
	return r
}

func (m *Matcher) keepMembers() bool { return m.opts.ShowAll || m.opts.ShowScores }

// memoEnabled: a negative threshold matches nothing, not even an identical
// line, and --best can prefer a newer group, so neither may memoise.
func (m *Matcher) memoEnabled() bool { return m.opts.MaxDistance >= 0 && !m.opts.Best }

func (m *Matcher) prepare(line []byte) candidate {
	c := candidate{normalized: normalize(line, &m.opts)}
	if m.opts.Method == Cosine {
		c.profile = trigramProfile(c.normalized)
	}
	return c
}

func (m *Matcher) lookupMemo(normalized []byte) hit {
	if !m.memoEnabled() {
		return miss
	}
	if h, ok := m.groupOf[string(normalized)]; ok {
		return h
	}
	return miss
}

// lengthRulesOut: distance is at least the length difference, so skip
// pairs that can't match, with the f32 arithmetic of distanceTo.
func (m *Matcher) lengthRulesOut(n, keyLen uint32) bool {
	if m.opts.Method == Cosine {
		return false
	}
	diff := n - keyLen
	if keyLen > n {
		diff = keyLen - n
	}
	if m.opts.Method != Levenshtein {
		maxLen := max(n, keyLen)
		return maxLen > 0 && float32(diff)/float32(maxLen) > m.opts.MaxDistance
	}
	return float32(diff) > m.opts.MaxDistance
}

// distanceTo reports group i's distance when within the threshold, computed
// exactly as the C++ (f32 ratio, f64 budget).
func (m *Matcher) distanceTo(c *candidate, i int, s *scratch) (float64, bool) {
	d := m.opts.MaxDistance
	if !(d >= 0) {
		return 0, false
	}
	g := &m.groups[i]
	if m.opts.Method == Cosine {
		score := cosineDistance(&c.profile, &g.profile)
		return score, score <= float64(d)
	}
	maxLen := uint32(max(len(c.normalized), len(g.normalizedKey)))
	if m.opts.Method != Levenshtein {
		if maxLen == 0 {
			return 0, true
		}
		// one edit of slack so rounding can't make k too tight; the f32 test
		// has the final say
		budget := float64(d) * float64(maxLen)
		k := maxLen
		if budget < float64(maxLen) {
			k = uint32(budget) + 1
		}
		ratio := float32(boundedLevenshtein(c.normalized, g.normalizedKey, k, s)) / float32(maxLen)
		return float64(ratio), ratio <= d
	}
	k := maxLen
	if d < float32(maxLen) {
		k = uint32(d)
	}
	dist := boundedLevenshtein(c.normalized, g.normalizedKey, k, s)
	return float64(dist), float32(dist) <= d
}

// findMatch: without --best the first group in [from, to) within the
// threshold; with --best the closest, lowest index on ties.
func (m *Matcher) findMatch(c *candidate, from, to int, s *scratch) hit {
	n := uint32(len(c.normalized))
	best := miss
	for i := from; i < to; i++ {
		if m.lengthRulesOut(n, m.keyLengths[i]) {
			continue
		}
		score, ok := m.distanceTo(c, i, s)
		if !ok {
			continue
		}
		if !m.opts.Best {
			return hit{i, score}
		}
		if best.group == noGroup || score < best.score {
			best = hit{i, score}
		}
		if score == 0 {
			break // nothing beats an exact match
		}
	}
	return best
}

func (m *Matcher) place(line []byte, c candidate, h hit) {
	if m.memoEnabled() {
		if _, seen := m.groupOf[string(c.normalized)]; !seen {
			memo := h
			if h.group == noGroup {
				memo = hit{len(m.groups), 0}
			}
			m.groupOf[string(c.normalized)] = memo
		}
	}
	keep, longest := m.keepMembers(), m.opts.Rep == RepLongest
	if h.group != noGroup {
		g := &m.groups[h.group]
		if longest && len(line) > len(g.longest) {
			g.longest, g.longestAt = line, g.count
		}
		g.count++
		if keep {
			g.matches = append(g.matches, line)
			g.scores = append(g.scores, h.score)
		}
		return
	}
	g := group{key: line, normalizedKey: c.normalized, profile: c.profile, count: 1}
	if longest {
		g.longest = line
	}
	if keep {
		g.matches, g.scores = [][]byte{line}, []float64{0}
	}
	m.groups = append(m.groups, g)
	m.keyLengths = append(m.keyLengths, uint32(len(c.normalized)))
}

// Add groups one line; a line joins only one group, so counts sum to the
// number of lines. The matcher keeps `line`; don't reuse its memory.
func (m *Matcher) Add(line []byte) {
	c := m.prepare(line)
	h := m.lookupMemo(c.normalized)
	if h.group == noGroup {
		h = m.findMatch(&c, 0, len(m.groups), m.scratch)
	}
	m.place(line, c, h)
}

// AddBatch gives the same result as Add on each line in order: phase 1
// matches every line against the groups that existed before the batch, on
// up to Threads goroutines; phase 2, serial and in input order, checks the
// groups created earlier in the batch (all with higher indices).
func (m *Matcher) AddBatch(lines [][]byte) {
	frozen := len(m.groups)
	cands := make([]candidate, len(lines))
	hits := make([]hit, len(lines))
	phase1 := func(i int, s *scratch) {
		cands[i] = m.prepare(lines[i])
		if memo := m.lookupMemo(cands[i].normalized); memo.group != noGroup {
			hits[i] = memo
		} else {
			hits[i] = m.findMatch(&cands[i], 0, frozen, s)
		}
	}
	workers := min(m.opts.Threads, len(lines))
	if workers <= 1 {
		for i := range lines {
			phase1(i, m.scratch)
		}
	} else {
		for len(m.workers) < workers {
			m.workers = append(m.workers, newScratch())
		}
		var next atomic.Int64
		var wg sync.WaitGroup
		for w := range workers {
			wg.Add(1)
			go func(s *scratch) {
				defer wg.Done()
				for i := int(next.Add(1) - 1); i < len(lines); i = int(next.Add(1) - 1) {
					phase1(i, s)
				}
			}(m.workers[w])
		}
		wg.Wait()
	}

	for i, line := range lines {
		h := m.lookupMemo(cands[i].normalized) // an earlier line in this batch may have memoised it
		if h.group == noGroup {
			h = hits[i]
			if m.opts.Best {
				later := m.findMatch(&cands[i], frozen, len(m.groups), m.scratch)
				if later.group != noGroup && (h.group == noGroup || later.score < h.score) {
					h = later
				}
			} else if h.group == noGroup {
				h = m.findMatch(&cands[i], frozen, len(m.groups), m.scratch)
			}
		}
		m.place(line, cands[i], h)
	}
}

// PrintTo prints the groups sorted by representative bytes (stable: equal
// representatives keep creation order), as uniq would print sorted input.
func (m *Matcher) PrintTo(w io.Writer) error {
	type row struct {
		g     *group
		rep   []byte
		repAt int
	}
	rows := make([]row, len(m.groups))
	for i := range m.groups {
		g := &m.groups[i]
		rows[i] = row{g, g.key, 0}
		if m.opts.Rep == RepLongest {
			rows[i] = row{g, g.longest, g.longestAt}
		}
	}
	slices.SortStableFunc(rows, func(a, b row) int { return bytes.Compare(a.rep, b.rep) })
	all := m.keepMembers()
	var buf []byte
	for _, r := range rows {
		buf = buf[:0]
		if m.opts.ShowCounts {
			count := strconv.Itoa(r.g.count)
			for pad := len(count); pad < 7; pad++ {
				buf = append(buf, ' ')
			}
			buf = append(append(buf, count...), ' ')
		}
		buf = append(buf, r.rep...)
		if all {
			for j, member := range r.g.matches {
				if j == r.repAt {
					continue
				}
				buf = append(append(buf, '\t'), member...)
				if m.opts.ShowScores {
					buf = append(buf, " ("...)
					if m.opts.Method == Levenshtein {
						buf = strconv.AppendUint(buf, uint64(r.g.scores[j]), 10)
					} else {
						buf = strconv.AppendFloat(buf, r.g.scores[j], 'f', 3, 64)
					}
					buf = append(buf, ')')
				}
			}
		}
		buf = append(buf, '\n')
		if _, err := w.Write(buf); err != nil {
			return err
		}
	}
	return nil
}
