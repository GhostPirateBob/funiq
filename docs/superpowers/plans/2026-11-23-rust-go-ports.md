# funiq Rust and Go ports Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rust and Go ports of funiq that produce byte-identical output to the C++ binary, built from one makefile on Windows, Linux and macOS, plus a benchmark harness comparing the three.

**Architecture:** First the C++ side is prepared: tie-breaks become deterministic, the layout moves to `src/cpp` / `src/rust` / `src/go`, and a differential harness (`test/conformance.sh`) treats the C++ binary as the oracle. Each port then reimplements the same algorithm in four units (CLI, distances, matcher, line I/O) and must pass conformance. `bench/bench.sh` verifies identical output, then times all binaries with hyperfine.

**Tech Stack:** GNU make ≥ 4, C++11 (g++/clang++/MinGW), Rust edition 2024 with `rayon` 1.x (local rustc 1.100, msvc on Windows), Go ≥ 1.22 (local 1.27.4), bash, python3, hyperfine 2.x.

**Spec:** `docs/superpowers/specs/2026-11-23-rust-go-ports-design.md`. §3 is the behaviour contract, §4 the language traps.

## Global Constraints

- Start after PRs #11, #12 and #13 are merged to `master`.
- Binaries: `bin/funiq`, `bin/funiq-rs`, `bin/funiq-go` (with `$(EXE)` = `.exe` on Windows).
- Rust dependencies: `rayon` only. Go: standard library only. Commit `Cargo.lock`.
- Rust `[profile.release]`: `lto = "fat"`, `codegen-units = 1`, `panic = "abort"`; `src/rust/.cargo/config.toml` sets `-C target-feature=+crt-static` for `x86_64-pc-windows-msvc`. Go builds with `CGO_ENABLED=0 go build -trimpath`.
- No `-march=native` / `target-cpu=native`.
- Version string `1.2.0` in all three; `--version` prints `<argv0 basename> 1.2.0`.
- The release workflow builds and tests C++ only.
- Algorithm constants copied from C++: batch size 256, I/O buffers 1 MiB, band kernel for `1 <= k <= 31` when `k <= len(short)`, column kernel when `len(short) <= 64`, DP otherwise.
- Never commit anything from the local branch `backup/2025-07-it-compiles-domains-logs-LOCAL-ONLY`.

## Review Focus

1. **Non-UTF-8 and NUL bytes in lines** (Latin-1 logs, binary junk): output must carry the same bytes. Rust on a Windows *console* would error on such writes, so it writes lossily there (spec §4). Covered by `edge.txt` (Task 3) and a manual console check (Task 7).
2. **Lines over 64 KB and over 1 MiB, and a last line without `\n`:** Go's `Scanner` would fail, a naive buffer would split lines. Covered by `long.txt` (Task 3) and the line-reader unit tests (Tasks 7, 11).
3. **CRLF and 0x1A input:** Windows drops one trailing `\r`, other platforms keep it, and `0x1A` is data everywhere. Covered by `edge.txt` (Task 3) and the line-reader unit tests (Tasks 7, 11).
4. **`funiq … | head -1`:** the program must exit quietly, with no panic or "broken pipe" text on stderr. Covered by the broken-pipe check in `conformance.sh` (Task 3).
5. **Threshold edges** (`-d 0`, `-d -1`, `-d 2.5`, `-d 40`, normalized ratios exactly at the threshold, `-d inf` rejected): covered by `conformance-flags.txt` and `edge.txt` (Task 3) and the parse tests (Tasks 4, 8).

---

### Task 1: C++ ties between equal representatives follow group creation order

**Files:**
- Modify: `lib/funiq/Matcher.h` (the `std::sort` in `Matcher::show`)
- Test: `test/test.cpp`

**Interfaces:**
- Produces: the ordering rule of spec §3 "Output" (ties in creation order), which conformance relies on.

- [ ] **Step 1: Write the test** (input found by fuzzing the 1.2.0+ binary):

```cpp
TEST_CASE( "rows with the same representative keep group creation order", "[matcher]" ) {
	Settings settings;
	settings.maxDistance = 2;
	settings.bestMatch = true;
	settings.representative = RepLongest;
	settings.showTotals = true;
	settings.showAllMatches = true;
	Matcher matcher(settings);
	for(const char* line : {"bba", "baab", "a", "aab", "aaaaa", "baab"}) matcher.add(line);
	REQUIRE(showLines(matcher) == std::vector<std::string>{
		"      1 aaaaa", "      3 baab\tbba\ta", "      2 baab\taab" });
}
```

Add a 40-group variant: repeat the 6-line block with a distinct prefix per block (`"p1-"`, `"p2-"`, …) so the sort runs on more than 16 rows (libstdc++ insertion-sorts below 16 elements, which is why the small case already passes). Assert that the rows of each tied pair keep the order above.

- [ ] **Step 2: Run** `make test`. The small case passes; the 40-group variant fails or passes depending on the sort. Either way, Step 3 makes the order a guarantee rather than luck.
- [ ] **Step 3: Replace `std::sort` with `std::stable_sort`** in `Matcher::show`. `rows` is built in group order, so stability means creation order. Update the comment.
- [ ] **Step 4: Run** `make all`. Expected: all unit and CLI tests pass and the golden files are unchanged.
- [ ] **Step 5: Commit** `Matcher: rows with equal representatives keep creation order`.

### Task 2: Layout `src/cpp|rust|go` and a multi-language makefile

**Files:**
- Move (`git mv`): `src/funiq.cpp` → `src/cpp/funiq.cpp`; `lib/funiq/*.h` → `src/cpp/funiq/`; `test/test.cpp` → `src/cpp/test/test.cpp`
- Modify: `makefile`, `.github/workflows/ci.yml` (lint paths), `.github/workflows/publish.yml` (C++ targets), `.gitignore` (`src/rust/target/`, `bench/data/`), `BUILDING.md`, `ARCHITECTURE.md`, `README.md` (paths only)

**Interfaces:**
- Produces make targets used by every later task:

| target | does |
|---|---|
| `all` (default) | `build` then `test` |
| `build` / `test` | `build-<lang>` / `test-<lang>` for each lang in `LANGS` |
| `build-cpp`, `test-cpp` | today's C++ build; unit tests plus `test/cli_test.sh bin/funiq$(EXE)` |
| `build-rust`, `test-rust`, `build-go`, `test-go` | defined as stubs that fail with "not ported yet" until Tasks 4 and 8 |
| `install`, `uninstall` | every built binary into `$(BINDIR)`; `install-<lang>` for one |
| `clean` | remove `bin/funiq*`, `cargo clean` when cargo exists |
| `toolchain` | print CXX/target/LDFLAGS plus `cargo --version`, `rustc --version`, `go version` when present |
| `bench` | `bash bench/bench.sh` with the built binaries (Task 12) |

`LANGS := cpp` plus `rust` if `$(CARGO) --version` succeeds and `src/rust/Cargo.toml` exists, plus `go` if `$(GO) version` succeeds and `src/go/go.mod` exists (`CARGO ?= cargo`, `GO ?= go`). Keep all existing compiler, `-static`, `PREFIX`/`BINDIR` logic. Includes become `-Isrc/cpp -Ilib`.

- [ ] **Step 1: Move the files and update the makefile.** C++ targets keep today's recipes with the new paths, and HEADERS becomes `$(wildcard src/cpp/funiq/*.h)`.
- [ ] **Step 2: Run** `make toolchain && make all`. Expected: unit tests pass, `cli: 52 passed, 0 failed` (2 skipped on Windows), and `LANGS` is `cpp` (no `Cargo.toml` or `go.mod` yet).
- [ ] **Step 3: Update `ci.yml`**: lint compiles `src/cpp/funiq.cpp` and `src/cpp/test/test.cpp` with `-Isrc/cpp -Ilib`, and the first-party filter regex becomes `^(src/cpp)/`. Update `publish.yml`: `$MAKE "${args[@]}" build-cpp test-cpp`.
- [ ] **Step 4: Update the docs.** Run `rg -n "src/funiq.cpp|lib/funiq|test/test.cpp"`. Expected: no hits outside CHANGELOG history.
- [ ] **Step 5: Commit** `Layout: src/cpp, multi-language makefile`, push, and check CI is green on all 4 runners.

### Task 3: Differential conformance harness

**Files:**
- Create: `test/fixtures/gen_edge_cases.py`, `test/conformance.sh`, `test/conformance-flags.txt`
- Modify: `makefile` (`test-cpp` also runs `bash test/conformance.sh bin/funiq$(EXE) bin/funiq$(EXE)` as a smoke test of the harness itself)

**Interfaces:**
- Produces: `bash test/conformance.sh REF CANDIDATE`. It prints `PASS`/`FAIL <case>` and ends with `conformance: N passed, M failed`; the exit status is 0 only when M = 0. Tasks 7, 11 and 12 rely on it.
- `python3 test/fixtures/gen_edge_cases.py OUTDIR` writes `OUTDIR/edge.txt` and `OUTDIR/long.txt` at run time (never committed, so git can't convert CRLF).

- [ ] **Step 1: Write `gen_edge_cases.py`** (binary-safe, deterministic, `newline=""`, written as bytes). `edge.txt` contains:
  - empty lines; `a`, `ab`, `abc` (cosine's short-string grams);
  - a line with tabs; a line with a NUL byte; `caf\xe9` and `caf\xc3\xa9`; a line containing `0x1A`;
  - `x\r\n` and `y\r\n` (CRLF);
  - `Hello, World!` / `HELLO WORLD`; `pid 123 exited` / `pid 98765 exited`; `connection reset by peer` / `peer by reset connection`;
  - pairs one substitution apart at lengths 8, 10, 16, 24, 30, 64, 65 and 100, so ratios land exactly on thresholds;
  - the Task 1 tie input;
  - a final line without `\n`.

  `long.txt`: a 70,000-byte line, the same line with one substitution, a 3,000,000-byte line, `abc`, `abd`, and no final newline.
- [ ] **Step 2: Write `conformance-flags.txt`**, one flag set per line:

```
-d 3
-d 0
-d -1
-d 2.5
-d 40 -c
-iI
-iI -d 4 -c
-d
-d -D -c
-d -c -a
-d --scores
-m normalized-levenshtein -d 0.125 -c
-m normalized-levenshtein -d 0.1 --scores
-m normalized-levenshtein -d 0.3 -c
-m normalized-levenshtein -d 1 -c
-m token-sort
-m token-sort -I -c
-m token-sort -iID --scores
-m cosine
-m cosine -d 0.2 -c
-m cosine -d 0.3 -iID -a
--best -d 2 -c
--best -d 2 --rep longest -c -a
--rep longest -c
--rep longest -d -a
-D -d 0 -c
-i -c
-I -a
-d 3 -j 1
-d 3 -j 3
-d -j 2 --scores
--color=never -c
--rep=longest -c
```

- [ ] **Step 3: Write `conformance.sh`.** For each of `test/fixtures/synthetic.log` and `edge.txt` crossed with each flag line, compare the two binaries' stdout bytes (`cmp`) and exit codes. Add these cases:
  - `long.txt` with `-d 3`, `-d`, `-d 0 -c`, `-a -d 2`;
  - stdin (`<`) and pipe (`cat |`) variants of `-d -c` on `synthetic.log`;
  - a missing file (both exit non-zero, and the candidate's stderr contains the file name);
  - `-d inf`, `-d nan`, `--rep sideways` and `-x` (both exit non-zero);
  - `--version` (second field equal);
  - broken pipe: `CAND -d 3 synthetic.log | head -1` leaves the candidate's stderr empty.
- [ ] **Step 4: Run** `bash test/conformance.sh bin/funiq.exe bin/funiq.exe`. Expected: `conformance: N passed, 0 failed`.
- [ ] **Step 5: Prove it catches differences.** Make a candidate wrapper `#!/usr/bin/env bash\nexec bin/funiq.exe -i "$@"`. Expected: `M failed` with M > 0 (cases where `-i` changes the output).
- [ ] **Step 6: Wire it into `test-cpp`, run** `make all` (green), **commit** `test: differential conformance harness against the C++ binary`.

### Task 4: Rust crate and CLI

**Files:**
- Create: `src/rust/Cargo.toml` (package `funiq-rs`, bin `funiq-rs`, edition 2024, dependency `rayon = "1"`, release profile per Global Constraints), `src/rust/.cargo/config.toml`, `src/rust/src/main.rs`, `src/rust/src/settings.rs`, `src/rust/src/cli.rs`
- Modify: `makefile` (`build-rust`: `cargo build --release --manifest-path src/rust/Cargo.toml` then copy `target/release/funiq-rs$(EXE)` to `bin/`; `test-rust`: `cargo test --release --manifest-path src/rust/Cargo.toml` then `bash test/conformance.sh bin/funiq$(EXE) bin/funiq-rs$(EXE)`)

**Interfaces:**
- Produces (`settings.rs`): `pub enum Method { Levenshtein, NormalizedLevenshtein, TokenSort, Cosine }`, `pub enum Rep { First, Longest }`, and `pub struct Settings { pub method: Method, pub max_distance: f32, pub case_insensitive: bool, pub ignore_non_alnum: bool, pub mask_digits: bool, pub best: bool, pub threads: usize, pub show_counts: bool, pub show_all: bool, pub show_scores: bool, pub rep: Rep }`. `Default` = plain `funiq` (Levenshtein, 3.0, `threads` 1, everything else off/First).
- Produces (`cli.rs`): `pub enum Command { Run { settings: Settings, file: Option<std::path::PathBuf> }, Help, Version }` and `pub fn parse_args(args: Vec<std::ffi::OsString>) -> Result<Command, String>` (args without argv[0]; `threads` resolved to `available_parallelism` when `-j` is 0 or absent).

- [ ] **Step 1: Write the tests** in `cli.rs` (`#[cfg(test)]`), one `#[test]` each:

```rust
assert_eq!(run(&["-d"]).max_distance, 0.125);                 // bare -d = preset
assert_eq!(run(&["-d"]).method, Method::NormalizedLevenshtein);
assert!(run(&["-d"]).case_insensitive && run(&["-d"]).ignore_non_alnum);
assert_eq!(run(&["-d", "0.125", "x"]).max_distance, 0.125f32); // -d <number> is a threshold
assert_eq!(file(&["-d", "log.txt"]), Some("log.txt".into()));  // bare -d then a filename
assert_eq!(run(&["-m", "cosine"]).max_distance, 0.05);
assert_eq!(run(&["-m", "token-sort"]).max_distance, 0.125);
assert_eq!(run(&["-d", "-1"]).max_distance, -1.0);             // value may start with '-'
assert_eq!(run(&["--rep=longest"]).rep, Rep::Longest);         // --name=value
let s = run(&["-iIc"]); assert!(s.case_insensitive && s.ignore_non_alnum && s.show_counts);
assert_eq!(run(&["--default", "-m", "levenshtein"]).method, Method::Levenshtein); // explicit refines preset
assert!(parse(&["-d", "inf"]).is_err() && parse(&["-d", "nan"]).is_err());
assert!(parse(&["-x"]).is_err() && parse(&["--nope"]).is_err() && parse(&["-j", "-1"]).is_err());
assert_eq!(file(&["--", "-dash.txt"]), Some("-dash.txt".into()));
assert!(parse(&["a.txt", "b.txt"]).is_err());
```

(`run`, `file` and `parse` are small test helpers over `parse_args`.)
- [ ] **Step 2: Run** `cargo test --manifest-path src/rust/Cargo.toml`. Expected: FAIL (unresolved `parse_args`).
- [ ] **Step 3: Implement `parse_args`** with a hand-rolled parser that follows spec §3 "Arguments" (clap's error texts and Go's `flag` semantics don't fit). The bare-`-d` check accepts what C `strtod` accepts fully: decimal and exponent forms, `inf`/`infinity`/`nan` (any case) and `0x` hex. The value parse is `str::parse::<f32>()` and rejects non-finite values. Keep `main.rs` minimal: parse, then print `funiq-rs 1.2.0` / a one-line usage / `funiq-rs: <message>` with exit 1.
- [ ] **Step 4: Run** the tests again. Expected: PASS.
- [ ] **Step 5: Commit** `rust: crate, settings and CLI parsing`.

### Task 5: Rust distances

**Files:**
- Create: `src/rust/src/similarity.rs`

**Interfaces:**
- Produces: `pub struct Scratch` (DP `rows: Vec<u32>`, `peq: [u64; 256]`, `window: [(isize, u64); 256]` initialised to `(FAR_PAST, 0)` with `FAR_PAST = isize::MIN / 4`), `impl Scratch { pub fn new() -> Self }`;
  `pub fn bounded_levenshtein(a: &[u8], b: &[u8], k: u32, s: &mut Scratch) -> u32`;
  `pub fn levenshtein_word(text: &[u8], pattern: &[u8], k: u32, s: &mut Scratch) -> u32` (1 ≤ pattern ≤ 64);
  `pub fn levenshtein_band(s1: &[u8], s2: &[u8], k: u32, s: &mut Scratch) -> u32` (s1 longer, `k <= s2.len()`, `s1.len() - s2.len() <= k`, `2k+1 <= 64`);
  `pub struct Profile { pub grams: Vec<(u32, u32)>, pub norm: f64 }`, `pub fn trigram_profile(s: &[u8]) -> Profile`, `pub fn cosine_distance(a: &Profile, b: &Profile) -> f64`.
  All return "exact if ≤ k, else k + 1" and leave `Scratch`'s tables as they found them.

- [ ] **Step 1: Write the tests.** Port `src/cpp/test/test.cpp`'s `[levenshtein]` and cosine cases with the same inputs and expected values. Use a plain full-matrix `reference_levenshtein` and the same random generators: 20k close/far pairs per kernel on its own domain (3- and 36-letter alphabets), 6k dispatcher pairs up to 200 bytes with `k < 46` sharing one `Scratch`, and 20k short pairs with `k < 13`. Use any fixed-seed PRNG (e.g. a 20-line xorshift, so no extra dependency). The C++ seeds don't carry over and don't need to.
- [ ] **Step 2: Run** `cargo test`. Expected: FAIL (unresolved items).
- [ ] **Step 3: Implement**, translating `src/cpp/funiq/similarity.h` line by line: the same length bound, affix strip, dispatch order (band if `k >= 1 && k <= m && 2k+1 <= 64`, else word if `m <= 64`, else DP), kernel steps, early exits and resets. `wrapping_add` is not needed: every counter is bounded by the string length.
- [ ] **Step 4: Run** `cargo test --release`. Expected: PASS.
- [ ] **Step 5: Commit** `rust: bounded Levenshtein (band, column, DP) and trigram cosine`.

### Task 6: Rust matcher

**Files:**
- Create: `src/rust/src/matcher.rs`

**Interfaces:**
- Consumes: `Settings` (Task 4), `similarity::*` (Task 5).
- Produces: `pub fn normalize(line: &[u8], s: &Settings) -> Vec<u8>`; `pub struct Matcher`; `impl Matcher { pub fn new(settings: Settings) -> Matcher; pub fn add(&mut self, line: Vec<u8>); pub fn add_batch(&mut self, lines: Vec<Vec<u8>>); pub fn write_to<W: std::io::Write>(&self, out: &mut W) -> std::io::Result<()> }`. `new` builds one `rayon::ThreadPool` with `settings.threads` threads when that is above 1.

- [ ] **Step 1: Write the tests.** Port every `[matcher]`, `[tier3]` and `[color]`-free case of `test.cpp` with the same inputs and expected rows (ships at d=3 and 4; normalized threshold scaling; repeated lines; members as written; first-group-only; `--mask-digits`; token-sort with and without `-I`; cosine vs normalized; `--rep longest`; `--scores` both methods; `--best`; the Task 1 tie). Also port the thread-equivalence test (`add` vs `add_batch` with random chunk sizes at threads 1, 2, 3 and 8, all 10 setting rows) and the counts-only vs `-a` equivalence test.
- [ ] **Step 2: Run** `cargo test`. Expected: FAIL.
- [ ] **Step 3: Implement**, mirroring `Matcher.h`:
  - groups: `key`, `normalized_key`, `profile`, `count`, `longest`, `longest_at`, and `matches`/`scores` kept only when `show_all || show_scores`;
  - memo: `HashMap<Vec<u8>, Hit>`;
  - `key_lengths: Vec<u32>`;
  - `add_batch` phase 1 runs `pool.install(|| lines.par_iter().zip(out.par_iter_mut()).for_each_init(Scratch::new, …))` against the frozen groups; phase 2 is serial, exactly as in C++;
  - `write_to` stable-sorts rows bytewise.

  Score text: `format!("{}", dist)` for levenshtein, `format!("{:.3}", score)` otherwise.
- [ ] **Step 4: Run** `cargo test --release`. Expected: PASS.
- [ ] **Step 5: Commit** `rust: matcher (memo, first-match/--best, two-phase batches, output)`.

### Task 7: Rust line I/O, wiring, conformance

**Files:**
- Create: `src/rust/src/lines.rs`
- Modify: `src/rust/src/main.rs`

**Interfaces:**
- Produces: `pub struct LineReader<R: std::io::Read>`; `impl<R: Read> LineReader<R> { pub fn new(inner: R) -> Self /* 1 MiB BufReader */; pub fn next_line(&mut self, line: &mut Vec<u8>) -> std::io::Result<bool> }`.

- [ ] **Step 1: Write the tests** in `lines.rs`, reading from `std::io::Cursor`:
  - `b"a\n\nb"` → `a`, empty, `b`, then `false`;
  - `b""` → `false`;
  - a 3,000,000-byte line followed by `\nx` comes back whole;
  - `b"a\r\nb\x1ac\n"` → on Windows (`cfg!(windows)`) `a`, `b\x1ac`; elsewhere `a\r`, `b\x1ac`.
- [ ] **Step 2: Run** `cargo test`. Expected: FAIL.
- [ ] **Step 3: Implement `next_line`** with `read_until(b'\n')`, stripping the `\n` and, on Windows, one `\r`. Wire `main.rs`:
  - read from the file or stdin through `LineReader` in batches of 256;
  - write through a 1 MiB `BufWriter` around `stdout().lock()`;
  - a missing file prints `funiq-rs: <name>: <reason>` and exits 1;
  - `ErrorKind::BrokenPipe` on write or flush exits 0 with nothing on stderr;
  - on Windows, when `std::io::stdout().is_terminal()`, write `String::from_utf8_lossy` output.
- [ ] **Step 4: Run** `make build-rust test-rust`. Expected: `cargo test` passes and `conformance: N passed, 0 failed`.
- [ ] **Step 5: Manual check** (Review Focus 1). In a Windows terminal, `bin\funiq-rs.exe <edge.txt>` prints the lines, with replacement characters for the Latin-1 byte, and doesn't error.
- [ ] **Step 6: Commit** `rust: line reader, main wiring; passes conformance`, push, and check CI green on all 4 runners.

### Task 8: Go module and CLI

**Files:**
- Create: `src/go/go.mod` (`module github.com/GhostPirateBob/funiq/src/go`, `go 1.22`), `src/go/main.go`, `src/go/settings.go`, `src/go/cli.go`, `src/go/cli_test.go`
- Modify: `makefile` (`build-go`: `cd src/go && CGO_ENABLED=0 $(GO) build -trimpath -o ../../bin/funiq-go$(EXE) .`; `test-go`: `cd src/go && $(GO) test ./...` then conformance against `bin/funiq-go$(EXE)`)

**Interfaces:**
- Produces: `type Method int` (`Levenshtein`, `NormalizedLevenshtein`, `TokenSort`, `Cosine`), `type Rep int` (`RepFirst`, `RepLongest`), `type Settings struct { Method Method; MaxDistance float32; CaseInsensitive, IgnoreNonAlnum, MaskDigits, Best bool; Threads int; ShowCounts, ShowAll, ShowScores bool; Rep Rep }`; `type command int` (`cmdRun`, `cmdHelp`, `cmdVersion`); `func parseArgs(args []string) (Settings, string, command, error)` (filename `""` = stdin; `Threads` resolved to `runtime.NumCPU()` for 0 or absent).

- [ ] **Step 1: Write `cli_test.go`** with the same 15 assertions as Task 4 Step 1 in Go form (table-driven).
- [ ] **Step 2: Run** `go test ./...` in `src/go`. Expected: FAIL (undefined `parseArgs`).
- [ ] **Step 3: Implement** a hand-rolled parser (the `flag` package stops at the first positional and can't combine `-iIc`). Threshold: `strconv.ParseFloat(v, 32)`, rejecting `inf`/`nan` (any case), hex and underscores, then `float32(...)`.
- [ ] **Step 4: Run** `go test ./...`. Expected: PASS.
- [ ] **Step 5: Commit** `go: module, settings and CLI parsing`.

### Task 9: Go distances

**Files:**
- Create: `src/go/similarity.go`, `src/go/similarity_test.go`

**Interfaces:**
- Produces: `type scratch struct { rows []uint32; peq [256]uint64; window [256]struct{ at int; bits uint64 } }`, `func newScratch() *scratch` (window entries `at = math.MinInt / 4`); `func boundedLevenshtein(a, b []byte, k uint32, s *scratch) uint32`; `func levenshteinWord(text, pattern []byte, k uint32, s *scratch) uint32`; `func levenshteinBand(s1, s2 []byte, k uint32, s *scratch) uint32`; `type profile struct { grams []gram; norm float64 }` with `type gram struct{ key, count uint32 }`; `func trigramProfile(s []byte) profile`; `func cosineDistance(a, b *profile) float64`. Same domains and contract as Task 5.

- [ ] **Step 1: Write the tests**: the same cases and random cross-checks as Task 5 Step 1 (`math/rand/v2` with a fixed seed).
- [ ] **Step 2: Run** `go test ./...`. Expected: FAIL.
- [ ] **Step 3: Implement** as a line-by-line translation of `similarity.h`. Re-slice inputs to their length before the hot loops (`text = text[:n]`) so bounds checks are hoisted.
- [ ] **Step 4: Run** `go test ./...`. Expected: PASS.
- [ ] **Step 5: Commit** `go: bounded Levenshtein (band, column, DP) and trigram cosine`.

### Task 10: Go matcher

**Files:**
- Create: `src/go/matcher.go`, `src/go/matcher_test.go`

**Interfaces:**
- Consumes: `Settings` (Task 8), the distance functions (Task 9).
- Produces: `func normalize(line []byte, s *Settings) []byte`; `type Matcher struct{...}`; `func NewMatcher(s Settings) *Matcher`; `func (m *Matcher) Add(line []byte)`; `func (m *Matcher) AddBatch(lines [][]byte)`; `func (m *Matcher) WriteTo(w io.Writer) error`.

- [ ] **Step 1: Write the tests**: the same ported cases as Task 6 Step 1.
- [ ] **Step 2: Run** `go test ./...`. Expected: FAIL.
- [ ] **Step 3: Implement**, mirroring `Matcher.h`:
  - memo `map[string]hit`;
  - phase 1 starts `min(Threads, len(lines))` goroutines per batch, each with its own `*scratch` from a per-Matcher slice indexed by worker number, taking lines from an `atomic.Int64`, joined with a `sync.WaitGroup`;
  - phase 2 is serial;
  - rows sorted with `slices.SortStableFunc` and `bytes.Compare`/`strings.Compare`;
  - scores via `strconv.FormatFloat(score, 'f', 3, 64)`.
- [ ] **Step 4: Run** `go test ./...`. Expected: PASS.
- [ ] **Step 5: Commit** `go: matcher (memo, first-match/--best, two-phase batches, output)`.

### Task 11: Go line I/O, wiring, conformance

**Files:**
- Create: `src/go/lines.go`, `src/go/lines_test.go`
- Modify: `src/go/main.go`

**Interfaces:**
- Produces: `type lineReader struct{ r *bufio.Reader; buf []byte }`; `func newLineReader(r io.Reader) *lineReader` (1 MiB); `func (l *lineReader) next() (line []byte, ok bool, err error)` (the returned slice is valid until the next call).

- [ ] **Step 1: Write the tests**: the same four cases as Task 7 Step 1 (`runtime.GOOS == "windows"` for the CRLF branch), plus a 70,000-byte line (bigger than `Scanner`'s 64 KB limit).
- [ ] **Step 2: Run** `go test ./...`. Expected: FAIL.
- [ ] **Step 3: Implement** with `ReadSlice('\n')`, appending to `buf` while it returns `bufio.ErrBufferFull`. Wire `main.go`: batches of 256 lines (each copied, since the reader reuses its buffer), output through a 1 MiB `bufio.Writer`, and a missing file printing `funiq-go: <name>: <reason>` with exit 1.
- [ ] **Step 4: Run** `make build-go test-go`. Expected: `conformance: N passed, 0 failed`.
- [ ] **Step 5: Commit** `go: line reader, main wiring; passes conformance`, push, and check CI green on all 4 runners.

### Task 12: Benchmark harness and first results

**Files:**
- Create: `bench/bench.sh`, `bench/peakmem.ps1` (from the session's scratch version: arguments as one string with the filename first, so pwsh doesn't bind `-d`), `bench/results/.gitkeep`
- Modify: `makefile` (`bench`), `README.md` (a short "Ports" section linking the latest results file)

**Interfaces:**
- Consumes: the built binaries and `test/conformance.sh` (Tasks 3, 7, 11).
- Produces: `bash bench/bench.sh [--quick] BIN...`, which writes `bench/results/<YYYY-MM-DD>-<os>-<arch>.md`.

- [ ] **Step 1: Write `bench.sh`.** It:
  - generates `bench/data/{6k,24k,240k}.log` with `gen_synthetic.py` when missing;
  - for every case in spec §5 and every binary, compares the output with the first binary (`cmp`) and stops on any difference;
  - runs `hyperfine -N --warmup 2 --runs 10 --export-markdown` per case with `-L bin` over the binaries;
  - records peak memory for the `-d -D -c` 240k case;
  - writes a header with `make toolchain` output, CPU model and thread count;
  - `--quick` runs only 24k at 3 runs.
- [ ] **Step 2: Run** `bash bench/bench.sh --quick bin/funiq.exe bin/funiq.exe`. Expected: the results file has every table and no "differs" line.
- [ ] **Step 3: Run the full benchmark** `make bench` on the Windows laptop, then on oracle-vm (stream the tree with `tar | ssh` into a literal `/tmp/funiq-bench` path, as in BUILDING.md). Commit both results files.
- [ ] **Step 4: Commit** `bench: harness and first C++/Rust/Go results`.

---

After Task 12, the round-2 options (spec §6 question 2) get their own plan.
