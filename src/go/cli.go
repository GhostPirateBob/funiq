package main

import (
	"fmt"
	"math"
	"runtime"
	"slices"
	"strconv"
	"strings"
)

type command int

const (
	cmdRun command = iota
	cmdHelp
	cmdVersion
)

var valueOptions = []string{"distance", "method", "threads", "rep", "color"}

// canonical names the option a token stands for ("-d", "--distance" ->
// "distance"), or "" if none.
func canonical(token string) string {
	switch token {
	case "-d", "--distance":
		return "distance"
	case "-m", "--method":
		return "method"
	case "-j", "--threads":
		return "threads"
	case "--rep":
		return "rep"
	case "--color":
		return "color"
	case "-i", "--case-insensitive":
		return "case-insensitive"
	case "-I", "--ignore-non-alpha-numeric":
		return "ignore-non-alpha-numeric"
	case "-D", "--mask-digits":
		return "mask-digits"
	case "-c", "--show-counts":
		return "show-counts"
	case "-a", "--show-all":
		return "show-all"
	case "--scores":
		return "scores"
	case "--best":
		return "best"
	case "--default":
		return "default"
	case "-h", "--help":
		return "help"
	case "--version":
		return "version"
	}
	return ""
}

// isCNumber reports whether C strtod would consume all of s (decimal or hex
// floats, inf, infinity, nan, after optional leading whitespace): it decides
// what a bare -d is followed by, exactly as the C++ binary does.
func isCNumber(s string) bool {
	t := strings.TrimLeft(s, " \t\n\v\f\r")
	if len(t) > 0 && (t[0] == '+' || t[0] == '-') {
		t = t[1:]
	}
	u := strings.ToLower(t)
	if u == "" {
		return false
	}
	if hex, ok := strings.CutPrefix(u, "0x"); ok {
		mantissa, exponent, hasExp := strings.Cut(hex, "p")
		digits := 0
		for _, c := range mantissa {
			switch {
			case c >= '0' && c <= '9', c >= 'a' && c <= 'f':
				digits++
			case c != '.':
				return false
			}
		}
		if digits == 0 || strings.Count(mantissa, ".") > 1 {
			return false
		}
		if hasExp {
			exponent = strings.TrimLeft(exponent, "+-")
			if exponent == "" || strings.Trim(exponent, "0123456789") != "" {
				return false
			}
		}
		return true
	}
	if inner, ok := strings.CutPrefix(u, "nan("); ok && strings.HasSuffix(inner, ")") {
		return strings.Trim(inner[:len(inner)-1], "abcdefghijklmnopqrstuvwxyz0123456789_") == ""
	}
	if strings.ContainsRune(u, '_') {
		return false // strtod stops at '_'; Go's parser would accept it
	}
	_, err := strconv.ParseFloat(u, 64) // takes inf, infinity, nan like strtod
	return err == nil
}

// rewrite applies the C++ binary's pre-parse rewrites: --name=value becomes
// --name value, and a bare -d not followed by a number becomes --default;
// nothing after -- is rewritten.
func rewrite(args []string) []string {
	tokens := make([]string, 0, len(args))
	literal := false
	for i, arg := range args {
		eq := strings.IndexByte(arg, '=')
		switch {
		case !literal && strings.HasPrefix(arg, "--") && eq > 2:
			tokens = append(tokens, arg[:eq], arg[eq+1:])
		case !literal && arg == "-d" && !(i+1 < len(args) && isCNumber(args[i+1])):
			tokens = append(tokens, "--default")
		default:
			literal = literal || arg == "--"
			tokens = append(tokens, arg)
		}
	}
	return tokens
}

// parseArgs parses the arguments after argv[0] with the C++ funiq's
// semantics (spec §3, Arguments). The filename is "" for stdin.
func parseArgs(args []string) (Settings, string, command, error) {
	tokens := rewrite(args)
	var (
		seen     []string
		switches []string
		distance *float32
		method   *Method
		rep      = RepFirst
		threads  int
		file     string
		hasFile  bool
		literal  bool
	)
	fail := func(format string, a ...any) (Settings, string, command, error) {
		return Settings{}, "", cmdRun, fmt.Errorf(format, a...)
	}
	for i := 0; i < len(tokens); {
		token := tokens[i]
		i++
		if literal || token == "-" || !strings.HasPrefix(token, "-") {
			if hasFile {
				return fail("Couldn't find match for argument %s", token)
			}
			file, hasFile = token, true
			continue
		}
		if token == "--" {
			literal = true
			continue
		}
		var names []string
		if name := canonical(token); name != "" {
			names = []string{name}
		} else if !strings.HasPrefix(token, "--") && len(token) > 2 {
			// combined short switches, e.g. -iIc (value options can't combine)
			for _, c := range token[1:] {
				name := canonical("-" + string(c))
				if name == "" || slices.Contains(valueOptions, name) {
					return fail("Couldn't find match for argument %s", token)
				}
				names = append(names, name)
			}
		} else {
			return fail("Couldn't find match for argument %s", token)
		}
		for _, name := range names {
			if slices.Contains(seen, name) {
				return fail("Argument already set! (%s)", token)
			}
			seen = append(seen, name)
			switch {
			case name == "help":
				return Settings{}, "", cmdHelp, nil
			case name == "version":
				return Settings{}, "", cmdVersion, nil
			case !slices.Contains(valueOptions, name):
				switches = append(switches, name)
				continue
			}
			if i >= len(tokens) {
				return fail("Missing a value for this argument! (%s)", token)
			}
			value := tokens[i]
			i++
			switch name {
			case "distance":
				// like istream >> float: correctly rounded straight to f32,
				// and no inf, nan, hex or digit separators
				v, err := strconv.ParseFloat(value, 32)
				if err != nil || math.IsInf(v, 0) || math.IsNaN(v) || strings.ContainsAny(value, "xX_") {
					return fail("Couldn't read argument value from string '%s' (-d)", value)
				}
				d := float32(v)
				distance = &d
			case "method":
				m, ok := map[string]Method{"levenshtein": Levenshtein, "normalized-levenshtein": NormalizedLevenshtein,
					"token-sort": TokenSort, "cosine": Cosine}[value]
				if !ok {
					return fail("Value '%s' does not meet constraint (-m)", value)
				}
				method = &m
			case "threads":
				// an int like the C++ (so 2^31 and up are errors)
				n, err := strconv.ParseInt(value, 10, 32)
				if err != nil || n < 0 {
					return fail("Value '%s' does not meet constraint: 0 or more (-j)", value)
				}
				threads = int(n)
			case "rep":
				switch value {
				case "first":
					rep = RepFirst
				case "longest":
					rep = RepLongest
				default:
					return fail("Value '%s' does not meet constraint: first|longest (--rep)", value)
				}
			case "color": // accepted for compatibility; the ports print no colour
				if !slices.Contains([]string{"auto", "always", "never"}, value) {
					return fail("Value '%s' does not meet constraint: auto|always|never (--color)", value)
				}
			}
		}
	}

	s := defaultSettings()
	// the preset first; explicit flags then refine it
	if slices.Contains(switches, "default") {
		s.CaseInsensitive, s.IgnoreNonAlnum, s.Method = true, true, NormalizedLevenshtein
	}
	for _, name := range switches {
		switch name {
		case "case-insensitive":
			s.CaseInsensitive = true
		case "ignore-non-alpha-numeric":
			s.IgnoreNonAlnum = true
		case "mask-digits":
			s.MaskDigits = true
		case "show-counts":
			s.ShowCounts = true
		case "show-all":
			s.ShowAll = true
		case "scores":
			s.ShowScores = true
		case "best":
			s.Best = true
		}
	}
	if method != nil {
		s.Method = *method
	}
	switch {
	case distance != nil:
		s.MaxDistance = *distance
	case s.Method == Levenshtein:
		s.MaxDistance = 3
	case s.Method == Cosine:
		s.MaxDistance = 0.05
	default:
		s.MaxDistance = 0.125
	}
	s.Rep = rep
	s.Threads = threads
	if threads == 0 {
		s.Threads = runtime.NumCPU()
	}
	return s, file, cmdRun, nil
}
