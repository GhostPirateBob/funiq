Funiq (fuzzy uniq) is a command line tool for performing fuzzy string matching against lists of words. To compile, clone the source and run make at the project's root; the funiq binary will be compiled to the bin/ directory.

# Examples

## Basic deduplication

Consider the file test.txt:

	The Fellowship of The Ring
	the fellowship ofteh reing
	The Flopship of the Rung
	Felloship of the Ring
	The Two Towers
	The Twin Towers
	the towers
	The Return of the King
	Return of the King
	Teh return of theking

Running funiq on this list and telling it to ignore casing (-i) and non-alphanumeric characters (-I)

	$ funiq -iI test.txt

Results in:

	Felloship of the Ring
	The Fellowship of The Ring
	The Flopship of the Rung
	The Return of the King
	The Two Towers

Which is ok, but it hasn't produced the desired results with The Fellowship of the Ring. Using the -d option, we can increase the threshold at which matches are considered (default is 3):

	$ funiq -iI -d 4 test.txt

	The Fellowship of The Ring
	The Return of the King
	The Two Towers

Which is what we were looking for.

## Normalized Levenshtein

The default Levenshtein method uses a fixed edit distance, which means short strings are matched more aggressively than long ones. The normalized method scales the distance to a 0.0–1.0 range based on string length, giving more consistent results across varying lengths.

A good general-purpose preset for catching typos and near-duplicates:

	$ funiq -iI -m normalized-levenshtein -d 0.125 names.txt

This treats strings as duplicates when they differ by roughly 12.5% or less of their length, which works well for both short and long strings. 0.125 is also the default threshold for the normalized method when `-d` is omitted (the absolute method defaults to 3 edits).

## Piping from stdin

Funiq reads from stdin when no filename is given, so it works naturally in pipelines:

	$ cat names.txt | funiq -iI

	$ grep -i error log.txt | funiq -iI -d 5 | sort

	$ history | awk '{$1=""; print $0}' | funiq -d 2

## Counting duplicates

Use `-c` to prefix each line with the number of matches found, similar to `uniq -c`:

	$ funiq -iI -d 4 -c test.txt

	      4 The Fellowship of The Ring
	      3 The Return of the King
	      3 The Two Towers

## Showing all duplicates

Use `-a` to show every matched string, not just the representative:

	$ funiq -iI -d 4 -a test.txt

	The Fellowship of The Ring	the fellowship ofteh reing	The Flopship of the Rung	Felloship of the Ring
	The Return of the King	Return of the King	Teh return of theking
	The Two Towers	The Twin Towers	the towers

Duplicates are shown tab-separated on the same line as their group. Add `--scores` to see how far each duplicate is from its group (edits, or a 0.0–1.0 ratio for the normalized methods); it implies `-a`:

	$ printf 'hello\nhallo\nhelp!\n' | funiq -d 2 --scores

	hello	hallo (1)	help! (2)

## Logs: masking digits

Log lines that differ only in timestamps, PIDs, ports or IP addresses are usually the same event. `-D` (`--mask-digits`) compares every run of digits as a single `0`, so those lines become exact duplicates. That is both more accurate and much faster, because repeated lines are recognised without any comparison:

	$ funiq --default -D -c error.log

On 20,000 synthetic nginx lines this turns 13,024 groups into 109, and 5.5s into 0.2s.

## Comparison methods

`-m` picks how two lines are compared:

| method | distance | good for |
|---|---|---|
| `levenshtein` (default) | edit count | short strings, fixed small typos |
| `normalized-levenshtein` | edits ÷ longer length | mixed lengths; `--default` uses 0.125 |
| `token-sort` | normalized, after sorting the words | the same words in a different order |
| `cosine` | 1 − cosine similarity of character trigrams | reordered fields; very tolerant, so keep `-d` small |

With `-m token-sort -I`, punctuation separates words (`user=bob,ip=1` has the words `1 bob ip user`).

## Choosing groups and representatives

By default a line joins the **first** existing group within range, and the group is printed as the first line that started it. `--best` joins the **closest** group instead (ties go to the older group). `--rep longest` prints each group as its longest member, which is often the most informative.

## Threads

Matching runs on all CPU cores by default; `-j <n>` sets the number of threads. The output is identical for any thread count.

# Usage

Funiq can read from a file or have its input piped from stdin.

<!-- usage:start (generated from `funiq -h`; test/cli_test.sh checks it) -->
```
funiq - Fuzzy unique line filtering (like uniq, but fuzzy)

Usage: funiq [options] [filename]

Options:
  -d <n>     Max distance threshold (default: 3 edits for levenshtein,
             0.125 for normalized-levenshtein and token-sort, 0.05 for cosine)
  -m <name>  Comparison method:
               levenshtein             edit distance (default)
               normalized-levenshtein  edits / longer length (0.0-1.0)
               token-sort              normalized, ignoring word order
               cosine                  1 - trigram cosine similarity (0.0-1.0)
  -i         Case-insensitive matching
  -I         Ignore non-alphanumeric characters (word separators for token-sort)
  -D         Mask digits: each run of digits compares as a single 0
  -c         Show duplicate counts (like uniq -c)
  -a         Show all duplicates, not just the representative
  --scores   Show each duplicate's distance from its group (implies -a)
  --best     Join the closest group instead of the first within range
  --rep <r>  Group representative: first (default) or longest
  --default  Shorthand for -iI -m normalized-levenshtein -d 0.125
             (a bare -d, not followed by a number, means the same)
  -j <n>     Worker threads (default: all CPU cores; output is identical)
  --color <when>  auto (default: when output is a terminal), always, never;
             NO_COLOR and FORCE_COLOR are honoured
  -h         Show this help
  --version  Show version

  If no filename is given, reads from stdin.

Examples:
  funiq names.txt                                        Basic deduplication
  cat names.txt | funiq -iI                              Pipe, ignore case & symbols
  funiq -iI -d 5 names.txt                               Higher edit distance
  funiq -iI -m normalized-levenshtein -d 0.125 file.txt  Length-independent matching
  funiq -iI -c names.txt                                 Count duplicates
  grep error log.txt | funiq -iI -d 5 | sort            Combine with other tools
  funiq --default -D -c error.log                        Logs: ignore PIDs, times, IPs
```
<!-- usage:end -->

# Installation

You will need the GNU compiler collection installed, along with the make build tool. Clone the project, and run

```
make
```

in the project directory. This will build the `funiq` binary to `bin/funiq` and then run the test suite. A test failure will not prevent the binary from being built.

## Installing to PATH

To install the compiled binary:

```
make install
```

This will automatically install to the first available location:

1. `~/.local/bin` — if it exists (no sudo needed)
2. `/usr/local/bin` — standard system-wide location (may need sudo)
3. `/usr/bin` — last resort (may need sudo)

If the install fails due to permissions, you'll be prompted to retry with `sudo`:

```
sudo make install
```

You can also override the install location with `PREFIX`:

```
make install PREFIX=$HOME/.local
sudo make install PREFIX=/usr
```

To remove it:

```
make uninstall
```

## Pre-built binaries

Pre-built Linux and Windows binaries are available from the [Actions](../../actions) tab. Select the latest successful workflow run and download the artifact for your platform.

# Build targets

| Command            | Description                                              |
|--------------------|----------------------------------------------------------|
| `make`             | Build the binary and run tests                           |
| `make build`       | Build the binary only                                    |
| `make test`        | Compile and run the test suite                           |
| `make install`     | Install the binary (auto-detects `~/.local/bin`, `/usr/local/bin`, or `/usr/bin`) |
| `make uninstall`   | Remove the installed binary                              |
| `make clean`       | Remove compiled binaries from `bin/`                     |
