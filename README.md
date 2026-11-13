# funiq

`uniq`, but fuzzy. funiq reads lines (from a file or stdin) and folds together the ones that are *nearly* the same: typos, case and punctuation differences, or log lines that differ only in their timestamps and IDs. It prints one line per group, optionally with counts, every member, or how far each member is from its group.

It is a single static binary with no runtime dependencies on Windows, and builds with any C++11 compiler on Linux (x86_64 and arm64), MinGW and Cygwin. Matching runs on all CPU cores and gives the same output for any thread count.

```
$ funiq -iI -d 4 -c ships.txt
      3 USS Defiant
      4 USS Enterprise
      3 USS Voyager
```

**Build and install:** `make && make install` (details, platforms and tests in [BUILDING.md](BUILDING.md)).

## Examples

`ships.txt`:

	USS Enterprise
	uss enterprize
	U.S.S. Enterprise
	Enterprize
	USS Voyager
	USS Voyagr
	uss-voyager
	USS Defiant
	USS Defient
	Defiant

Ignoring case (`-i`) and punctuation (`-I`), with the default threshold of 3 edits:

	$ funiq -iI ships.txt
	Enterprize
	USS Defiant
	USS Enterprise
	USS Voyager

"Enterprize" is still on its own: once case and punctuation are gone it is 4 edits away from "ussenterprise" (3 deletions and a substitution). Raise the threshold with `-d`:

	$ funiq -iI -d 4 ships.txt
	USS Defiant
	USS Enterprise
	USS Voyager

### Counts, members and distances

`-c` prefixes each group with its size, like `uniq -c`. `-a` prints every member, tab-separated after the group's representative. `--scores` adds each member's distance from the group (edits, or a 0.0–1.0 ratio for the normalized methods) and implies `-a`:

	$ funiq -iI -d 4 --scores ships.txt
	USS Defiant	USS Defient (1)	Defiant (3)
	USS Enterprise	uss enterprize (1)	U.S.S. Enterprise (0)	Enterprize (4)
	USS Voyager	USS Voyagr (1)	uss-voyager (0)

Output is sorted alphabetically by representative.

### Thresholds that scale with length

A fixed edit count is generous for short lines and strict for long ones. `-m normalized-levenshtein` divides the edits by the longer line's length, so `-d` becomes a fraction: 0.125 means "differs in at most about an eighth of its characters". That combination with `-iI` is the preset `--default`, which a bare `-d` also selects:

	$ funiq -d names.txt                    # same as: funiq -iI -m normalized-levenshtein -d 0.125
	$ funiq -d 0.2 -iI -m normalized-levenshtein names.txt    # -d with a number is the threshold

### Logs

Log lines that differ only in timestamps, PIDs, ports or IP addresses are usually the same event. `-D` (`--mask-digits`) compares every run of digits as a single `0`, so those lines become exact duplicates, which is also much faster because repeats are recognised without comparing anything:

	$ funiq -d -D -c error.log
	$ zcat access.log.gz | funiq -d -D -c | sort -rn | head

On 20,000 synthetic nginx lines `-D` turns 13,024 groups into 109, and 5.5s into 0.2s.

### Comparison methods

| `-m` | distance | default `-d` | good for |
|---|---|---|---|
| `levenshtein` (default) | edit count | 3 | short strings, a few typos |
| `normalized-levenshtein` | edits ÷ longer length | 0.125 | mixed lengths |
| `token-sort` | normalized, after sorting the words | 0.125 | the same words in another order |
| `cosine` | 1 − cosine similarity of character trigrams | 0.05 | reordered fields; tolerant, so keep `-d` small |

With `-m token-sort -I`, punctuation separates words: `user=bob,ip=1` has the words `1 bob ip user`.

### Groups and representatives

A line joins the **first** existing group within the threshold, and a group is printed as the line that started it. `--best` joins the **closest** group instead (ties go to the older group). `--rep longest` prints each group as its longest member, which is often the most informative one.

### Colour

When writing to a terminal, counts are coloured by frequency (1 gray, 2–9 green, 10–99 yellow, 100+ bold red), extra members are dim and `--scores` are green or yellow depending on how close they are. Piped output is never coloured. `--color always|never` overrides the check; `NO_COLOR` and `FORCE_COLOR` are honoured.

### Threads

`-j <n>` sets the number of worker threads (default: all CPU cores). The output is identical for any `-j`.

## Usage

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

## More

- [BUILDING.md](BUILDING.md): toolchains, make targets, installing, tests, CI
- [ARCHITECTURE.md](ARCHITECTURE.md): how matching works and why it is fast
- [CHANGELOG.md](CHANGELOG.md): release notes
- [TODO.md](TODO.md): ideas and possible next steps
- [WIKI.md](WIKI.md): outline for a project wiki
