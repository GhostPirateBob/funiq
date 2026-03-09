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

This treats strings as duplicates when they differ by roughly 12.5% or less of their length, which works well for both short and long strings.

## Piping from stdin

Funiq reads from stdin when no filename is given, so it works naturally in pipelines:

	$ cat names.txt | funiq -iI

	$ grep -i error log.txt | funiq -iI -d 5 | sort

	$ history | awk '{$1=""; print $0}' | funiq -d 2

## Counting duplicates

Use `-c` to prefix each line with the number of matches found, similar to `uniq -c`:

	$ funiq -iI -c test.txt

	      4 Felloship of the Ring
	      3 The Return of the King
	      3 The Two Towers

## Showing all duplicates

Use `-a` to show every matched string, not just the representative:

	$ funiq -iI -d 4 -a test.txt

	The Fellowship of The Ring	the fellowship ofteh reing	The Flopship of the Rung	Felloship of the Ring
	The Return of the King	Return of the King	Teh return of theking
	The Two Towers	The Twin Towers	the towers

Duplicates are shown tab-separated on the same line as their group.

# Usage

Funiq can read from a file or have its input piped from stdin.

    USAGE:

       funiq  [-I] [-c] [-a] [-i] [-d <number>] [-m <levenshtein
              |normalized-levenshtein>] [--] [--version] [-h] <filename>


    Where:

       -I,  --ignore-non-alpha-numeric
         When active, non-alphanumeric characters do not contribute to edit
         distance.

       -c,  --show-counts
         Precede each output line with the count of the number of times the
         line occurred in the input, followed by a single space.

       -a,  --show-all
         Will show all found duplicates

       -i,  --case-insensitive
         When active, case differences do not contribute to distance between
         strings.

       -d <number>,  --distance <number>
         Maximum distance threshold between two strings to be considered
         duplicates.

         For the default Levenshtein comparison method, it is the maximum edit
         distance allowed for two strings to be considered duplicates.

         For the Normalized Levenshtein comparison method, it is a number
         between 0.0 and 1.0 representing 0% and 100% similarity respectively.

       -m <levenshtein|normalized-levenshtein>,  --method <levenshtein
          |normalized-levenshtein>
         The method used to compare similarity of strings. Defaults to
         'levenshtein'

       --,  --ignore_rest
         Ignores the rest of the labeled arguments following this flag.

       --version
         Displays version information and exits.

       -h,  --help
         Displays usage information and exits.

       <filename>
         File to read. If omitted will read from stdin.

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
