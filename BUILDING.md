# Building funiq

funiq is one C++11 source file (`src/funiq.cpp`) plus headers in `lib/funiq/`. The only dependencies are vendored headers: TCLAP for argument parsing (`lib/tclap/`) and Catch2 v2 for unit tests (`lib/catch/`). You need a C++11 compiler, GNU make and, for the CLI tests, bash.

```
make            # build bin/funiq and run every test
make install    # copy it onto your PATH
```

## Supported toolchains

| platform | compiler | notes |
|---|---|---|
| Linux x86_64, arm64 | g++ (any C++11) | verified on Ubuntu 24.04 x86_64 and aarch64 |
| Windows | MinGW-w64 (winlibs, scoop `mingw-winlibs`/`gcc`, MSYS2) | linked `-static`: the `.exe` imports only Windows' own DLLs (see Runtime dependencies) |
| Windows | Cygwin g++ | works; the binary needs `cygwin1.dll`, `cygstdc++-6.dll`, `cyggcc_s-seh-1.dll` |

Any GNU make works: Linux make, scoop/avr-gcc make, `mingw32-make`, Cygwin make. On Windows the recipes run under the `sh` from Git for Windows, MSYS2 or Cygwin, so one of those must be on PATH.

### Which compiler is used

`make toolchain` prints the compiler, its target triple and the link flags. The choice is:

1. `make CXX=...` or a `CXX` environment variable, if set;
2. on Windows, `x86_64-w64-mingw32-g++` if it exists (so a native standalone `.exe` is the default even when a Cygwin `g++` comes first on PATH);
3. otherwise `g++`.

`-static` is added only when the compiler's target is MinGW (checked with `$(CXX) -dumpmachine`, not the OS: Cygwin also sets `OS=Windows_NT`, and a static libstdc++ link fails there). `-static-libstdc++ -static-libgcc` alone is not enough with winlibs' posix-thread GCC: the binary would still need `libwinpthread-1.dll`.

### Runtime dependencies

- **Windows (MinGW build):** none beyond Windows itself. The `.exe` imports `KERNEL32.dll` plus one of Windows' C runtimes, depending on how the MinGW toolchain was built:
  - **msvcrt** toolchains (e.g. winlibs' `msvcrt` builds) import `msvcrt.dll`, present on every Windows version;
  - **UCRT** toolchains (e.g. scoop `mingw-winlibs`, MSYS2 UCRT64, and the GitHub runner that builds the release `.exe`) import the Universal CRT's `api-ms-win-crt-*.dll`, built into Windows 10 and 11 (older Windows needs update KB2999226).

  `make toolchain` shows which one you have (the target and compiler), and `objdump -p bin/funiq.exe | grep "DLL Name"` lists the imports.
- **Linux:** `libstdc++.so.6`, `libgcc_s.so.1`, `libc.so.6`, `libm.so.6`, all present on any glibc distro with a C++ runtime. A binary built on a newer distro can fail on an older one with a `GLIBCXX_...` version error. The release binaries avoid this by linking fully static (`make LDFLAGS=-static`).

## Make targets

| target | does |
|---|---|
| `make` / `make all` | build `bin/funiq` and the tests, run both test suites |
| `make build` | build `bin/funiq` only |
| `make test` | build and run the unit tests, then the CLI tests against `bin/funiq` |
| `make toolchain` | show compiler, target, flags |
| `make install` | install the binary (see below) |
| `make uninstall` | remove it again |
| `make clean` | delete the built binaries |

Targets depend on `src/funiq.cpp` and `lib/funiq/*.h`, so `make` rebuilds exactly when a source changed.

## Installing

`make install` copies `bin/funiq` (`funiq.exe` on Windows) to `$(BINDIR)`:

- **Linux:** `~/.local/bin` if it exists, else `/usr/local/bin`, else `/usr/bin` (`sudo make install` if needed).
- **Windows:** `~/.local/bin` (from `HOME`, else `USERPROFILE`, with forward slashes).

Overrides: `make install PREFIX=/opt/funiq` (binary goes to `PREFIX/bin`), `make install BINDIR=C:/PROGRAMS/__PATH` (exact directory), and `DESTDIR=` for staging a package. `make uninstall` takes the same variables.

## Tests

`make test` runs two suites; both must pass.

### Unit tests: `test/test.cpp`

Catch2, built as `bin/funiq_test`. They cover:

- the distance functions, cross-checked on 20,000 random string pairs against a deliberately plain full-matrix reference implementation;
- grouping semantics (first match, `--best`, memoised repeats, normalisation, `--mask-digits`, token-sort, cosine, representatives, scores);
- **thread equivalence:** `addBatch` with 1, 2, 3 and 8 threads and random batch sizes must give byte-identical output to serial `add()` in every mode;
- colour: the `NO_COLOR`/`FORCE_COLOR` precedence table, and coloured output being exactly the plain output plus SGR codes.

Run a subset with Catch's tag filter, e.g. `bin/funiq_test "[levenshtein]"`.

### CLI tests: `test/cli_test.sh`

`bash test/cli_test.sh bin/funiq` runs the real binary: exit codes, raw output bytes (no CRLF on Windows), argument parsing (`--opt=value`, bare `-d`), the README examples, colour decisions, and:

- **Golden outputs** in `test/fixtures/golden-*.txt`: funiq's output on `test/fixtures/synthetic.log` for several flag combinations, rerun with different `-j` values. Any optimisation must reproduce them byte for byte. Only regenerate a golden file when an output change is intended, and say so in the commit.
- **Terminal behaviour** (colour by default, no progress bar for small inputs, FIFO input with a progress bar) via `script(1)`; these are skipped where `script` is unavailable, such as on Windows.

The fixture is synthetic (TEST-NET IP addresses, example.com hosts) and regenerated deterministically with `python test/fixtures/gen_synthetic.py test/fixtures/synthetic.log`. `.gitattributes` keeps fixtures byte-exact: Windows checkouts would otherwise convert them to CRLF.

The README's usage section is generated from `funiq -h`, and a CLI test fails if they differ. After changing the help text run `python test/update_readme_usage.py bin/funiq`.

### Testing on other machines

The suites only need the source tree, so they can be streamed anywhere. For example, a throwaway build in `/tmp` on a Linux box:

```
tar cf - src lib test makefile README.md | ssh host 'rm -rf /tmp/funiq-ci && mkdir /tmp/funiq-ci && cd /tmp/funiq-ci && tar xf - && make; cd / && rm -rf /tmp/funiq-ci'
```

Use literal paths in such one-liners: a variable that ends up empty turns `cd $DIR` into `cd ~`.

## Continuous integration

`.github/workflows/ci.yml` runs on every push to `master` and every pull request:

- **lint:** first-party code must compile without warnings under g++ and clang++ (`-Wall -Wextra`, plus `-Wpedantic` for the tool itself); ShellCheck on the CLI tests; the Python helpers must compile.
- **test:** `make all` (unit and CLI suites) and an install round trip on Linux x86_64, Linux arm64, Windows (MinGW, static) and macOS. On macOS and Windows the terminal tests are skipped, because they need util-linux `script`.

## Releases

`.github/workflows/publish.yml` runs when a `v*` tag is pushed:

1. Bump the version in `src/funiq.cpp` (the `TCLAP::CmdLine` line) and add a `## [X.Y.Z]` section to CHANGELOG.md, then merge to `master`.
2. `git tag -s vX.Y.Z -m vX.Y.Z && git push origin vX.Y.Z`
3. Each platform builds, reruns the full test suites against the release binary, checks the tag matches `funiq --version`, checks the binary is standalone, and packages it:

   | archive | built as |
   |---|---|
   | `funiq-X.Y.Z-linux-x86_64.tar.gz` | fully static (`LDFLAGS=-static`) |
   | `funiq-X.Y.Z-linux-arm64.tar.gz` | fully static |
   | `funiq-X.Y.Z-windows-x86_64.zip` | static MinGW |
   | `funiq-X.Y.Z-macos-universal.tar.gz` | arm64 and x86_64 in one binary |

4. The release job writes `SHA256SUMS`, attests build provenance for every archive, and creates a **draft** release whose notes are the CHANGELOG section.
5. Check the draft, then publish it: `gh release edit vX.Y.Z --draft=false`.

Verify a download with `gh attestation verify <archive> --repo GhostPirateBob/funiq` and `sha256sum -c SHA256SUMS --ignore-missing`.

A local equivalent of the Linux release build is `make LDFLAGS=-static all`.
