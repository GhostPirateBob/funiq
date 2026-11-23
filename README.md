# funiq

`uniq`, but fuzzy. funiq reads lines (from a file or stdin) and folds together the ones that are *nearly* the same: typos, case and punctuation differences, or log lines that differ only in their timestamps and IDs. It prints one line per group, optionally with counts, every member, or how far each member is from its group.

It is a single static binary with no runtime dependencies on Windows, and builds with any C++11 compiler on Linux (x86_64 and arm64), MinGW and Cygwin. Matching runs on all CPU cores and gives the same output for any thread count; it is 50× to over 3,000× faster than the original funiq on the same input, with identical results (see [Performance](#performance)).

```
$ funiq -iI -d 4 -c ships.txt
      3 USS Defiant
      4 USS Enterprise
      3 USS Voyager
```

**Install:** download a static binary for Linux (x86_64, arm64) or Windows from [Releases](../../releases) (on macOS, build it), or build it: `make && make install` (details, platforms and tests in [BUILDING.md](BUILDING.md)).

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

On Windows, give funiq the file (`funiq -d -D -c error.log` or `< error.log`) where you can: on 240k log lines a file or `< file` took 254–259 ms, a pipe from a native program (e.g. BusyBox `cat`) 339 ms, and a pipe from Git Bash/MSYS tools (`cat`, `zcat`) 571 ms.

In PowerShell, avoid `Get-Content file | funiq` and pipes from PowerShell functions: PowerShell turns each line into a string and re-encodes it, which is 35–40× slower and changes non-UTF-8 bytes, so the output can differ. A pipe between two native programs (`gzip -dc x.gz | funiq`) keeps the bytes but PowerShell 7 still relays it, about 4× slower on 27 MB; `cmd /c "gzip -dc x.gz | funiq"` gives a real pipe (+30%).

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

## Performance

Compared with the original [mjfisheruk/funiq](https://github.com/mjfisheruk/funiq) (v0.3.0, its last commit `ae84605`, built with its own `-O2` flags), on 6,000 synthetic nginx/PHP log lines and the same flags. Both programs printed **byte-identical groups** in every case. funiq 1.2.0 is shown single-threaded (`-j 1`, the algorithmic gain) and on all cores. The 1.2.0 times are the mean of 3 runs.

| flags | groups | upstream | 1.2.0 `-j 1` | 1.2.0 all cores | speedup |
|---|---|---|---|---|---|
| **Linux arm64** (4 cores, g++ 13) | | | | | |
| `-d 3` | 3,671 | 306.3s | 0.243s | 0.087s | **1,259× / 3,537×** |
| `-iI -d 3` | 3,671 | 150.3s | 0.232s | 0.083s | **647× / 1,815×** |
| `-iI -m normalized-levenshtein -d 0.125` (`-d`) | 1,698 | 67.5s | 1.263s | 0.375s | **53× / 180×** |
| **Windows x86_64** (12 threads, MinGW g++ 14) | | | | | |
| `-d 3` | 3,671 | 303.3s | 0.351s | 0.157s | **864× / 1,932×** |
| `-iI -d 3` | 3,671 | 138.7s | 0.386s | 0.161s | **360× / 862×** |
| `-iI -m normalized-levenshtein -d 0.125` (`-d`) | 1,698 | 66.4s | 1.162s | 0.351s | **57× / 189×** |

The original compares every line with every group using a full edit-distance table. 1.2.0 skips most pairs on length alone, stops each comparison as soon as the threshold is out of reach, recognises repeated lines without comparing them, and spreads the work over all cores (see [ARCHITECTURE.md](ARCHITECTURE.md)). The gap widens with input size: on 24,000 lines 1.2.0 takes 1–3 seconds, where the original's quadratic cost would run into hours. With `-D` on logs, most lines become repeats and runs are faster still.

To reproduce:

```
python test/fixtures/gen_synthetic.py bench.log 5000      # 5,000 lines + 1,000 repeats
git clone https://github.com/mjfisheruk/funiq upstream
g++ -std=c++0x -O2 -Iupstream/lib upstream/src/funiq.cpp -o funiq-upstream
time ./funiq-upstream -d 3 bench.log > old.txt
time bin/funiq -d 3 bench.log > new.txt
cmp old.txt new.txt          # on Windows the original writes CRLF: tr -d '\r' < old.txt | cmp - new.txt
```

## Rust and Go ports

`src/rust` and `src/go` are ports of funiq for comparing languages: the same algorithm (bit-parallel Levenshtein, memo, two-phase threaded batches) with **byte-identical output**. `test/conformance.sh` checks each port against the C++ binary on 82 cases, on Linux, Windows and macOS in CI. The C++ binary is the release; the ports are not shipped.

On a quiet Linux arm64 VM (4 cores; [full results](bench/results/2026-11-24-linux-aarch64.md)), 24k or 240k synthetic log lines, output checked identical before timing:

| case | C++ (g++ 13) | Rust 1.90 | Go 1.25 |
|---|---|---|---|
| `-d 3`, 1 thread | 3.16 s | **2.95 s** | 3.27 s |
| `-d` (preset), 1 thread | **2.01 s** | 2.16 s | 2.27 s |
| `-d`, all cores | **568 ms** | 638 ms | 644 ms |
| `--best -d 3`, 1 thread | 4.16 s | **3.78 s** | 4.22 s |
| logs `-d -D`, 1 thread (240k) | **193 ms** | 223 ms | 310 ms |
| 27 MB of `-a` output | **308 ms** | 325 ms | 404 ms |
| peak memory, logs `-c` | 5.2 MB | **2.8 MB** | 8.4 MB |

On the distance kernels the three are within about 10–15% of each other (C++ leads the preset's bit-parallel path, Rust the plain Levenshtein one). Before 1.3 the C++ lost memo-heavy logs and large output by about 2× (a locale call per byte in `normalize`, a map node allocated on every memo hit, `ostream` output); 1.3 fixed those and now leads both. [Windows results](bench/results/2026-11-24-windows-x86_64.md) (MinGW and MSVC C++, Rust, Go) agree within the ±10–20% noise of a laptop. Profile-guided optimisation (GCC PGO) was tried and rejected: 4–6% faster on plain Levenshtein, cosine and per-line work, but 11–23% slower on the band kernel (the `-d` preset).

Run it yourself with `make bench` (needs hyperfine; `make build-cpp-msvc` first to include an MSVC build on Windows).

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
- [bench/results](bench/results): C++, Rust and Go benchmark results
- [ARCHITECTURE.md](ARCHITECTURE.md): how matching works and why it is fast
- [CHANGELOG.md](CHANGELOG.md): release notes
- [TODO.md](TODO.md): ideas and possible next steps
- [WIKI.md](WIKI.md): outline for a project wiki

## License

MIT, see [LICENSE](LICENSE). This applies to the code from v1.2.0 on; earlier commits in the history include code from earlier versions of funiq that was published without a licence. The bundled TCLAP argument parser is MIT and Catch2 (tests only) is BSL-1.0; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
