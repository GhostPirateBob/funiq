package main

import "testing"

func mustRun(t *testing.T, args ...string) Settings {
	t.Helper()
	s, _, cmd, err := parseArgs(args)
	if err != nil || cmd != cmdRun {
		t.Fatalf("%q: want a run, got cmd=%v err=%v", args, cmd, err)
	}
	return s
}

func mustFile(t *testing.T, args ...string) string {
	t.Helper()
	_, file, cmd, err := parseArgs(args)
	if err != nil || cmd != cmdRun {
		t.Fatalf("%q: want a run, got cmd=%v err=%v", args, cmd, err)
	}
	return file
}

func TestBareDIsThePreset(t *testing.T) {
	s := mustRun(t, "-d")
	if s.MaxDistance != 0.125 || s.Method != NormalizedLevenshtein || !s.CaseInsensitive || !s.IgnoreNonAlnum {
		t.Fatalf("got %+v", s)
	}
}

func TestDWithANumberIsAThreshold(t *testing.T) {
	if s := mustRun(t, "-d", "0.125", "x"); s.MaxDistance != float32(0.125) || s.Method != Levenshtein {
		t.Fatalf("got %+v", s)
	}
}

func TestBareDThenAFilename(t *testing.T) {
	if f := mustFile(t, "-d", "log.txt"); f != "log.txt" {
		t.Fatalf("file %q", f)
	}
	if s := mustRun(t, "-d", "log.txt"); s.Method != NormalizedLevenshtein {
		t.Fatalf("got %+v", s)
	}
}

func TestDefaultThresholdDependsOnTheMethod(t *testing.T) {
	cases := []struct {
		args []string
		want float32
	}{{nil, 3}, {[]string{"-m", "cosine"}, 0.05}, {[]string{"-m", "token-sort"}, 0.125},
		{[]string{"-m", "normalized-levenshtein"}, 0.125}}
	for _, c := range cases {
		if got := mustRun(t, c.args...).MaxDistance; got != c.want {
			t.Errorf("%q: %v, want %v", c.args, got, c.want)
		}
	}
}

func TestAValueMayStartWithADash(t *testing.T) {
	if s := mustRun(t, "-d", "-1"); s.MaxDistance != -1 {
		t.Fatalf("got %v", s.MaxDistance)
	}
}

func TestLongOptionEqualsValue(t *testing.T) {
	if s := mustRun(t, "--rep=longest"); s.Rep != RepLongest {
		t.Fatal("--rep=longest")
	}
	if s := mustRun(t, "--distance=2.5"); s.MaxDistance != 2.5 {
		t.Fatal("--distance=2.5")
	}
}

func TestShortSwitchesCombine(t *testing.T) {
	if s := mustRun(t, "-iIc"); !s.CaseInsensitive || !s.IgnoreNonAlnum || !s.ShowCounts {
		t.Fatalf("got %+v", s)
	}
}

func TestExplicitFlagsRefineThePreset(t *testing.T) {
	if s := mustRun(t, "--default", "-m", "levenshtein"); s.Method != Levenshtein || s.MaxDistance != 3 {
		t.Fatalf("got %+v", s)
	}
}

func TestRejectsBadValuesAndUnknownOptions(t *testing.T) {
	for _, bad := range [][]string{{"-d", "inf"}, {"-d", "nan"}, {"-d", "0x10"}, {"--distance", "1_0"}, {"-x"}, {"--nope"},
		{"-j", "-1"}, {"-j", "1.5"}, {"--rep", "sideways"}, {"-m", "jaro"}, {"--color", "sometimes"},
		{"-iQ"}, {"-c", "-c"}, {"--=x"}, {"-m"}} {
		if _, _, _, err := parseArgs(bad); err == nil {
			t.Errorf("%q should be an error", bad)
		}
	}
}

func TestAfterDoubleDashALeadingDashIsAFilename(t *testing.T) {
	if f := mustFile(t, "--", "-dash.txt"); f != "-dash.txt" {
		t.Fatalf("file %q", f)
	}
}

func TestOneFilenameAtMost(t *testing.T) {
	if _, _, _, err := parseArgs([]string{"a.txt", "b.txt"}); err == nil {
		t.Fatal("two filenames should be an error")
	}
}

func TestHelpAndVersion(t *testing.T) {
	if _, _, cmd, err := parseArgs([]string{"-h"}); err != nil || cmd != cmdHelp {
		t.Fatal("-h")
	}
	if _, _, cmd, err := parseArgs([]string{"--version"}); err != nil || cmd != cmdVersion {
		t.Fatal("--version")
	}
}

func TestOutputAndMatchingFlags(t *testing.T) {
	s := mustRun(t, "-D", "-a", "--scores", "--best", "-j", "3", "--color", "never")
	if !s.MaskDigits || !s.ShowAll || !s.ShowScores || !s.Best || s.Threads != 3 {
		t.Fatalf("got %+v", s)
	}
}

func TestBareDBeforeANonNumberLikeStrtod(t *testing.T) {
	// strtod stops at '_', so "1_0" is not a number: -d is the preset and
	// "1_0" the filename, as in the C++ binary
	if f := mustFile(t, "-d", "1_0"); f != "1_0" {
		t.Fatalf("file %q", f)
	}
}

func TestThreadsAreA32BitIntLikeCpp(t *testing.T) {
	for _, bad := range [][]string{{"-j", "99999999999"}, {"-j", "2147483648"}} {
		if _, _, _, err := parseArgs(bad); err == nil {
			t.Errorf("%q should be an error", bad)
		}
	}
	if mustRun(t, "-j", "-0").Threads != mustRun(t, "-j", "0").Threads {
		t.Error("-j -0 should mean 0")
	}
}
