# Changelog

Release notes for funiq, newest first. Versions follow [Semantic Versioning](https://semver.org/): a major bump means some output changed.

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
