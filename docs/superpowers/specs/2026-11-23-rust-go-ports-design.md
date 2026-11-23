# funiq Rust and Go ports: design

Status: draft for review (2026-11-23). Applies to funiq after PRs #11–#13 (binary input, thread pool, counts-only storage, 1 MiB I/O, bit-parallel Levenshtein).

## 1. Goal

Measure how C++, Rust and Go perform on the **same algorithm** with **byte-identical output**, on Windows, Linux and macOS. A second, separately reported round lets each language use its idiomatic optimisations.

Non-goals for the ports (first round): colour output, the progress bar, TCLAP's exact error wording and quirks (repeated-option errors, the ignore-rest behaviour after `--`), help text, release artifacts. The C++ binary stays the released product.

## 2. Decisions

| topic | decision | why |
|---|---|---|
| layout | `src/cpp/` (funiq.cpp + `funiq/*.h` + `test/test.cpp`), `src/rust/` (cargo crate), `src/go/` (module); vendored `lib/tclap`, `lib/catch` unchanged; cross-language tests in `test/`, benchmarks in `bench/` | language-specific code under `src/<lang>`, shared tests and benchmarks outside (Bob's layout) |
| binaries | `bin/funiq` (C++), `bin/funiq-rs`, `bin/funiq-go` (+ `.exe` on Windows) | side by side for hyperfine; `funiq` stays the C++ release |
| oracle | the C++ binary: `test/conformance.sh REF CANDIDATE` diffs the two over a corpus × flag matrix | no new golden files; any behaviour C++ has, the ports must have |
| algorithm parity | port exactly: normalisation, memo, first-match / `--best`, 256-line two-phase batches, length pre-check, bounded Levenshtein (band kernel for k 1–31, column kernel for a short side ≤ 64, banded DP otherwise), trigram cosine, counts-only storage, 1 MiB I/O | otherwise the benchmark compares algorithms, not languages |
| dependencies | Rust: `rayon` only. Go: standard library only | rayon is how Rust shares borrowed data across a persistent pool without `unsafe`; goroutines need no pool |
| threads | C++: WorkerPool. Rust: one `rayon::ThreadPool` built once with `-j` threads, `par_iter` + `for_each_init` for per-thread `Scratch`. Go: a goroutine per worker per batch + `sync.WaitGroup` + atomic index | each language's idiomatic cheap-dispatch mechanism; results must not depend on `-j` |
| build flags | C++ `-O2` (as released). Rust `--release`, `lto = "fat"`, `codegen-units = 1`, `panic = "abort"`, static CRT on windows-msvc. Go `CGO_ENABLED=0 go build -trimpath`. No `-march=native` / `target-cpu=native` anywhere | release-style builds, same baseline ISA |
| make | one GNU makefile: `make` = build + test every language whose toolchain is found; `build-<lang>`, `test-<lang>`, `install[-<lang>]`, `uninstall`, `clean`, `bench`, `toolchain` | Bob's requirement; machines without cargo/go still build C++ |
| CI | lint + C++ as now; new jobs build the ports and run conformance on all four runners | conformance is the contract |
| releases | `publish.yml` calls the C++ targets explicitly | releases must not depend on port toolchains |
| versions | ports print `<argv0 basename> 1.2.0` like C++; conformance checks the second field | one version string |

## 3. Behaviour contract (must match C++ byte for byte)

**Input.** Bytes, not text. A line ends at `\n`; a last line without `\n` still counts; an empty input has no lines. On Windows one trailing `\r` is removed from each line; elsewhere it stays (`0x1A` is ordinary data everywhere). Lines of any length (tests use 70 KB and 3 MB).

**Arguments.** `-d/--distance <f32>`, `-m/--method levenshtein|normalized-levenshtein|token-sort|cosine`, `-i`, `-I`, `-D`, `-c`, `-a`, `--scores`, `--best`, `--rep first|longest`, `--default`, `-j/--threads <int ≥ 0>` (0 = all cores), `--color auto|always|never` (accepted; ports print no colour), `-h`, `--version`, optional filename (stdin otherwise). Short switches combine (`-iIc`); value options take the next argument even if it starts with `-`. Before parsing: `--name=value` → `--name value` when the name is non-empty; a bare `-d` not followed by something C `strtod` fully accepts becomes `--default`; nothing after `--` is rewritten. `--default` applies first (`-i -I`, normalized-levenshtein), explicit flags refine it. Default threshold when `-d` is absent: 3 (levenshtein), 0.05 (cosine), 0.125 (the other two). `-d` is parsed **directly to f32** with correct rounding; `inf`, `nan` and hex are errors. Errors: exit 1 with a message on stderr; a missing file's message contains its name.

**Normalisation** (ASCII only, as C `<cctype>` in the C locale): `-i` lowercases `A–Z` only; `-D` turns each run of `0–9` into one `0`; token-sort: with `-I` every non-`[A-Za-z0-9]` byte becomes a space, split on the six C whitespace bytes (space `\t \n \v \f \r`), sort tokens bytewise, join with one space; otherwise `-I` deletes every non-`[A-Za-z0-9]` byte (bytes ≥ 0x80 included).

**Matching.** Groups in creation order; a line joins the first group whose key is within the threshold (`--best`: the smallest distance, lower index on ties, stop at 0). Memo: an identical normalized line goes to the same group without comparing, enabled only when threshold ≥ 0 and not `--best`. Distance arithmetic exactly as `Matcher.h`:
- levenshtein: `k = d >= maxLen ? maxLen : trunc(d)`; match iff `dist <= d` (compared as f32);
- normalized / token-sort: both empty → 0; `budget = f64(d) * maxLen`; `k = budget >= maxLen ? maxLen : trunc(budget) + 1`; `ratio = f32(dist) / f32(maxLen)`; match iff `ratio <= d`; score = `f64(ratio)`;
- length pre-check: levenshtein `|Δlen| > d`; normalized `maxLen > 0 && f32(Δlen) / f32(maxLen) > d`; none for cosine;
- cosine: trigrams packed as `b0<<16 | b1<<8 | b2`; strings of length 1–2 are one gram `1<<24 | len<<16 | b0<<8 | (b1 or 0)`; empty → no grams; counts merged in sorted order, accumulated in f64; `distance = 1 − dot / (normA * normB)`, floored at 0; both empty → 0, one empty → 1; match iff `distance <= f64(d)`;
- negative threshold: nothing matches, not even identical lines.

**Output.** Rows sorted bytewise (unsigned) by representative, **ties in group-creation order** (Task 1 makes C++ do this; today `std::sort` leaves it unspecified). Representative: the key, or with `--rep longest` the first strictly longest member. `-c`: count right-aligned in 7 columns plus one space. `-a`/`--scores`: other members in insertion order, each after a tab; `--scores` appends ` (<score>)`, an integer for levenshtein and otherwise the f64 score with 3 decimals, correctly rounded, ties to even (C++/MinGW, Rust `{:.3}` and Go `%.3f` agree, checked on 0.0625 → `0.062` and 0.1875 → `0.188`). LF only. Exit 0.

## 4. Language traps (each one breaks byte identity or the benchmark)

| | Rust | Go |
|---|---|---|
| reading lines | `BufRead::lines()` rejects non-UTF-8 and strips `\r\n`: use `read_until(b'\n')` on a 1 MiB `BufReader` | `bufio.Scanner` strips `\r` and fails on lines over 64 KB: use `bufio.Reader.ReadSlice` with `ErrBufferFull` accumulation |
| writing | `stdout` is line-buffered: wrap `stdout().lock()` in a 1 MiB `BufWriter`; a Windows console rejects non-UTF-8 writes, so write lossily when stdout is a console | `os.Stdout` is unbuffered: 1 MiB `bufio.Writer` |
| broken pipe | SIGPIPE is ignored, so writes return `BrokenPipe`: exit quietly, no panic text | the runtime exits on SIGPIPE like C (nothing to do) |
| strings | `Vec<u8>` / `&[u8]` everywhere, ASCII-only helpers (`to_ascii_lowercase`, `is_ascii_alphanumeric`) | `[]byte` / `string` as bytes; never `strings.Fields`, `unicode.*`, `strings.ToLower` (Unicode-aware) |
| floats | parse `-d` with `str::parse::<f32>()` (direct, correctly rounded) and reject `inf`/`nan`; keep the f32/f64 split of §3 | `strconv.ParseFloat(s, 32)` then `float32(...)`, never via a float64 parse; reject `inf`/`nan`/hex/underscores. FMA fusion is allowed on arm64, but every multiply-add in §3 adds exact integers, so results don't change; wrap any new one in an explicit `float64(...)` |
| hashing | std `HashMap` (SipHash) in round 1, a faster hasher only in round 2, reported separately | built-in map; `m[string(b)]` lookups don't allocate |
| sorting | `sort_by` (stable) over rows | `slices.SortStableFunc` |

## 5. Benchmark method

- Inputs from `test/fixtures/gen_synthetic.py` at 6k, 24k and 240k base lines into `bench/data/` (ignored by git).
- Cases: `-d 3`, `-d` (preset), `-d -D`, `-m token-sort`, `-m cosine -d 0.2`, `--best -d 3`, each at `-j 1` and all cores, plus `-a` output-heavy at 240k.
- Before timing, every binary's output is compared with C++ (`cmp`); any difference stops the run.
- `hyperfine -N --warmup 2 --runs 10 -L bin <binaries>` per case (no shell: on Windows a bare `bash` resolves to WSL). Report mean ± σ in a Markdown table.
- Peak memory: `/usr/bin/time` (Linux `-f %M`, macOS `-l`), `bench/peakmem.ps1` on Windows.
- Each run writes `bench/results/<date>-<os>-<arch>.md` with the tables and toolchain versions (compiler versions, CPU, thread count). Same session for all binaries: this laptop drifts ±20% between sessions.
- Machines: the Windows laptop (12 threads), oracle-vm (Linux arm64, 4 cores), optionally WSL2 x86_64. CI timings are not used.

## 6. Open questions for Bob

1. CRLF: keep the per-platform rule (Windows drops `\r`), or drop one trailing `\r` everywhere (C++ change, output change on Linux for CRLF files)?
2. Round 2 (per-language optimisations): which are allowed? Candidates: Rust FxHash / target-cpu, Go PGO / GOGC, C++ `-O3`.
3. Should the ports ever ship as release assets?
