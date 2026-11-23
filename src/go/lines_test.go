package main

import (
	"bytes"
	"runtime"
	"slices"
	"testing"
)

func allLines(t *testing.T, input []byte) [][]byte {
	t.Helper()
	r := newLineReader(bytes.NewReader(input))
	var out [][]byte
	for {
		line, ok, err := r.next()
		if err != nil {
			t.Fatal(err)
		}
		if !ok {
			return out
		}
		out = append(out, bytes.Clone(line)) // valid only until the next call
	}
}

func sameLines(t *testing.T, got [][]byte, want ...string) {
	t.Helper()
	if !slices.EqualFunc(got, want, func(g []byte, w string) bool { return string(g) == w }) {
		t.Fatalf("got %q, want %q", got, want)
	}
}

func TestEmptyLinesAndAnUnterminatedLastLine(t *testing.T) {
	sameLines(t, allLines(t, []byte("a\n\nb")), "a", "", "b")
}

func TestEmptyInputHasNoLines(t *testing.T) {
	sameLines(t, allLines(t, nil))
	sameLines(t, allLines(t, []byte("\n")), "")
}

func TestLinesLongerThanScannerAndTheBufferComeBackWhole(t *testing.T) {
	for _, n := range []int{70_000, 3_000_000} {
		input := append(bytes.Repeat([]byte("x"), n), "\nx"...)
		lines := allLines(t, input)
		if len(lines) != 2 || len(lines[0]) != n || string(lines[1]) != "x" {
			t.Fatalf("n=%d: %d lines", n, len(lines))
		}
	}
}

func TestCRLFPerPlatformAnd0x1AIsData(t *testing.T) {
	lines := allLines(t, []byte("a\r\nb\x1ac\n"))
	if runtime.GOOS == "windows" {
		sameLines(t, lines, "a", "b\x1ac")
	} else {
		sameLines(t, lines, "a\r", "b\x1ac")
	}
}

func TestNonUTF8AndNULBytesPassThrough(t *testing.T) {
	sameLines(t, allLines(t, []byte("caf\xe9\nn\x00l\n")), "caf\xe9", "n\x00l")
}
