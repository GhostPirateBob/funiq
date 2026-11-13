# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).


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

## [0.3.0] - 2020-01-27

Note - we've adopted semantic versioning from this point forward.

### Added
- Ability to use Normalized Levenshtein Distance (suggested by [@ComedyTomedy](https://github.com/ComedyTomedy)) via the`--method` flag

## [0.2] - 2018-05-24

### Changed
- Made count behaviour compatible with GNU uniq. Flag for showing totals is now (`-c`), and the spacing between the totals column and the results column is now rendered with spaces instead of tabs. Contributed by [@sehrgut](https://github.com/sehrgut)

## [0.1] - 2014-03-17

Initial release
