// funiq-go: the Go port of funiq (fuzzy uniq), for benchmarking against the
// C++ original. Same algorithm, byte-identical output.
package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

const version = "1.2.0"

// programName is argv[0] without its directory, as the C++ binary prints it.
func programName() string {
	return filepath.Base(strings.ReplaceAll(os.Args[0], "\\", "/"))
}

func main() {
	_, _, cmd, err := parseArgs(os.Args[1:])
	switch {
	case err != nil:
		fmt.Fprintf(os.Stderr, "%s: %v\n", programName(), err)
		os.Exit(1)
	case cmd == cmdVersion:
		fmt.Printf("%s %s\n", programName(), version)
	case cmd == cmdHelp:
		fmt.Println("Usage: funiq-go [options] [filename]  (the options of funiq; see funiq -h)")
	}
}
