// funiq-go: the Go port of funiq (fuzzy uniq), for benchmarking against the
// C++ original. Same algorithm, byte-identical output.
package main

import (
	"bufio"
	"bytes"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"syscall"
)

const version = "1.3.1"

// batchSize: lines per batch, as in the C++; each batch's scan runs on all
// threads.
const batchSize = 1024

// programName is argv[0] without its directory, as the C++ binary prints it.
func programName() string {
	return filepath.Base(strings.ReplaceAll(os.Args[0], "\\", "/"))
}

// describe uses the C library's wording for the common cases, as the C++
// prints them.
func describe(err error) string {
	switch {
	case errors.Is(err, fs.ErrNotExist):
		return "No such file or directory"
	case errors.Is(err, fs.ErrPermission):
		return "Permission denied"
	}
	var pathErr *fs.PathError
	if errors.As(err, &pathErr) {
		return pathErr.Err.Error()
	}
	return err.Error()
}

// brokenPipe: the reader of stdout has gone (funiq-go ... | head). On Unix
// the runtime already exits on SIGPIPE; Windows reports it as an error.
func brokenPipe(err error) bool {
	var errno syscall.Errno
	if !errors.As(err, &errno) {
		return false
	}
	// ERROR_BROKEN_PIPE (109) and ERROR_NO_DATA (232) on Windows
	return errno == syscall.EPIPE || runtime.GOOS == "windows" && (errno == 109 || errno == 232)
}

func fail(what string, err error) {
	fmt.Fprintf(os.Stderr, "%s: %s: %s\n", programName(), what, describe(err))
	os.Exit(1)
}

func main() {
	settings, file, cmd, err := parseArgs(os.Args[1:])
	switch {
	case err != nil:
		fmt.Fprintf(os.Stderr, "%s: %v\n", programName(), err)
		os.Exit(1)
	case cmd == cmdVersion:
		fmt.Printf("%s %s\n", programName(), version)
		return
	case cmd == cmdHelp:
		fmt.Println("Usage: funiq-go [options] [filename]  (the options of funiq; see funiq -h)")
		return
	}

	name, input := "stdin", io.Reader(os.Stdin)
	if file != "" {
		f, err := os.Open(file)
		if err != nil {
			fail(file, err)
		}
		defer f.Close()
		name, input = file, f
	}

	m := NewMatcher(settings)
	reader := newLineReader(input)
	batch := make([][]byte, 0, batchSize)
	for {
		line, ok, err := reader.next()
		if err != nil {
			fail(name, err) // e.g. a directory
		}
		if !ok {
			break
		}
		batch = append(batch, bytes.Clone(line)) // the reader reuses its buffer
		if len(batch) == batchSize {
			m.AddBatch(batch)
			batch = make([][]byte, 0, batchSize)
		}
	}
	if len(batch) > 0 {
		m.AddBatch(batch)
	}

	out := bufio.NewWriterSize(os.Stdout, 1<<20)
	err = m.PrintTo(out)
	if err == nil {
		err = out.Flush()
	}
	if err != nil && !brokenPipe(err) {
		fail("stdout", err)
	}
}
