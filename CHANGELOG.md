# Changelog

Release notes for funiq, newest first. Versions follow [Semantic Versioning](https://semver.org/): a major bump means some output changed.

## [Unreleased]

### Added
- Rust and Go ports (`src/rust`, `src/go`) for comparing languages: the same algorithm with
  byte-identical output, checked by `test/conformance.sh` against the C++ binary (96 cases,
  including a seeded random corpus); `make bench` times them with hyperfine
  (`bench/results/`). The C++ binary stays the release.
- `make build-cpp-msvc` (Windows): the C++ built with Visual Studio's MSVC for comparison
- The makefile builds, tests and installs every language whose toolchain is present
  (`make build-<lang>`, `test-<lang>`, `install-<lang>`); sources moved to `src/cpp`

### Fixed
- An empty or space-led number for `-d`/`--distance` or `-j`/`--threads` is an argument error.
  TCLAP read `--distance=` as no value and kept the default 3, so `--distance= -m cosine`
  merged every line.
- With `--best --rep longest`, groups that print the same representative keep their creation
  order on every standard library (`std::sort` left it unspecified)
- Windows: a `0x1A` (Ctrl-Z) byte no longer ends the input. Text mode treated it as end of
  file, so everything after it was silently dropped, from files and stdin alike. Input is now
  read in binary mode; CRLF input behaves as before.
- An unknown option (`-x`, `--nope`) is an argument error instead of being opened as a file
  (`funiq: -x: No such file or directory`). After `--`, a leading dash is still a filename.
- `-j` with a negative count is an error; it used to wrap to 4294967295 and start up to 256
  threads per batch
- `--=value` is no longer split into `--` (end of options) and `value`
- A directory argument is an error; on Linux it used to print nothing and exit 0
- Windows: `2>NUL` no longer pre-reads the whole input file for an invisible progress bar

### Performance
- Levenshtein comparisons use Hyyrö's bit-parallel algorithm: a 64-bit word holds the whole
  band of 2k + 1 diagonals (k up to 31), or a whole DP column for a side of at most 64 bytes,
  so a step costs a dozen word operations however large k is. On 24k log lines: `--default`
  9.6s -> 2.3s single-threaded (1.8s -> 0.38s on 12 threads), `-d 3` 4.4s -> 2.6s,
  `-m token-sort` 3.8x, `-d 8` 4.0x. Wider budgets (e.g. `-m normalized-levenshtein -d 0.3` on
  long lines) still use the banded DP.
- Worker threads are started once and reused for every batch. Starting them per 256-line batch
  made log runs slower on all cores than on one: `-d -D` on 240k log lines, 12 threads,
  1.65s -> 0.36s (`-j 1`: 0.60s)
- Without `-a`/`--scores`, groups keep a count instead of every member line: `-d -D -c` on a
  27 MB log peaks at 5.5 MB instead of 46.5 MB
- Input is read and output written through 1 MiB buffers (libstdc++ uses 512 bytes on MinGW):
  diverse input 17-19% faster, 27 MB of `-a` output through a pipe 11% faster
- The progress bar no longer reads a file twice to count its lines; the percentage now comes
  from bytes read, also for `< file`. It redraws at most 10 times a second.

### Changed
- The `lib/funiq` headers mark their functions `inline`, so they can be included from more
  than one source file
- Removed TCLAP precompiled headers (`lib/tclap/*.h.gch`, 4.3 MB) carried over from the
  original repository; nothing used them

## [1.2.0] - 2026-11-13

### Added
- Colour output when writing to a terminal: `-c` counts by frequency (1 gray, 2–9 green,
  10–99 yellow, 100+ bold red), `-a` duplicates dim, `--scores` green/yellow, red errors,
  coloured progress bar. Piped output is unchanged.
- `--color auto|always|never`; `NO_COLOR` and `FORCE_COLOR` are honoured
  (`--color` > `FORCE_COLOR` > `NO_COLOR` > `TERM=dumb` > terminal check)
- `--opt=value` spelling for every option (`--color=never`, `--rep=longest`)
- A bare `-d` (not followed by a number) is the `--default` preset: `funiq -d log.txt`.
  `-d <number>` still sets the distance, so existing calls are unchanged.

### Fixed
- Since 1.0.0 a terminal run on fewer than 50 lines printed a progress bar with no final
  newline just before the results

### Changed
- The remaining code, tests and examples carried over from earlier versions are rewritten:
  the exact Levenshtein functions now wrap the bounded one, settings use member initialisers,
  argument parsing is table-driven, and the test suite and README use new examples (checked
  against an independent reference implementation). No output changes.

### CI and releases
- New CI workflow: warning-free lint (g++ and clang++), ShellCheck, and the full test suites
  on Linux x86_64, Linux arm64, Windows and macOS
- New release workflow: pushing a `v*` tag builds static Linux (x86_64, arm64), static Windows
  and universal macOS binaries, tests each, attests provenance and creates a draft release
- The terminal tests detect util-linux `script` and skip elsewhere instead of failing

### Docs
- README rewritten; build, test and install details moved to BUILDING.md; new ARCHITECTURE.md,
  TODO.md and WIKI.md
- README Performance section: measured against the original mjfisheruk/funiq on 6,000 synthetic
  log lines with identical output: 53–1,259× single-threaded and 180–3,537× on all cores
  (Linux arm64), 57–864× and 189–1,932× (Windows x86_64)
- `test/fixtures/gen_synthetic.py OUT N` generates benchmark input of any size (the default still
  reproduces the committed fixture)

### Licence
- funiq is now MIT-licensed (LICENSE). TCLAP's MIT licence (`lib/tclap/COPYING`, missing from
  the vendored copy until now) and Catch2's BSL-1.0 are included; release archives ship LICENSE
  and THIRD_PARTY_NOTICES.md

## [1.1.0] - 2026-11-13

### Added
- `-D/--mask-digits`: each run of digits compares as a single `0`, so log lines differing
  only in timestamps, PIDs or IPs become duplicates (20k synthetic nginx lines with
  `--default`: 13,024 groups → 109, 5.5s → 0.2s)
- `-m token-sort`: normalized Levenshtein on alphabetically sorted words (word order ignored;
  with `-I`, punctuation separates words)
- `-m cosine`: 1 − cosine similarity of exact character-trigram counts; order-tolerant.
  Default threshold 0.05 (deliberately tight: on access logs 0.2 merged different request
  paths because shared User-Agent strings dominate); not yet tuned on real data
- `--best`: join the closest group instead of the first within range (ties go to the older group)
- `--rep longest`: print each group as its longest member
- `--scores`: print each duplicate's distance from its group key (implies `-a`)
- All new modes give identical output for any `-j` thread count

### Changed
- README usage section is now the real `funiq -h` output, checked by the test suite

### Fixed
- Case folding and `-I` no longer pass negative `char` values to `tolower`/`isalnum`
  (undefined behaviour for UTF-8 bytes)

## [1.0.0] - 2026-11-13

Major version because some output changes (marked **breaking**). Changes since 0.4.2;
the 0.4.x releases (progress bar, `--default`, compact help) were not recorded here.

### Changed
- **breaking:** `-a` prints duplicates as written; it used to print their normalized form
- **breaking:** a line joins only the first group it matches, so `-c` counts now sum to the
  number of input lines (a line matching several groups used to be counted in all of them)
- **breaking:** `-m normalized-levenshtein` without `-d` defaults to 0.125 instead of 3
  (which matched everything)
- **breaking:** a missing input file is an error (`funiq: <file>: <reason>`, exit 1)
  instead of silent empty output
- Windows builds write LF line endings instead of CRLF

### Added
- `-j/--threads <n>` runs matching on several threads (default: all CPU cores); results are
  identical for any thread count
- `make toolchain` shows the compiler in use; `make install BINDIR=...` installs anywhere

### Performance
- Cutoff-bounded Levenshtein (length bound, common affix strip, Ukkonen band, early abort),
  memoised repeat lines, and threading. On 2k log lines, single-threaded vs 0.4.2: 34x
  (`--default`) to 460x (`-d 3`). On 20k lines, 12 threads vs the single-threaded optimised
  matcher: a further 3.9x to 9x. Output is unchanged.

### Fixed
- `funiq <(cmd)` printed nothing when stderr was a terminal (progress pre-count drained the pipe)
- `make` never rebuilt a stale binary; Windows builds needed MinGW DLLs; `make` failed under
  Cygwin; `make install` on Windows wrote to the shell's private `/usr/bin` without `.exe`
- Windows CI failed golden tests because checkout converted fixtures to CRLF
