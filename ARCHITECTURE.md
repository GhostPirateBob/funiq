# Architecture

funiq groups near-duplicate lines. It reads every line, assigns each to a group, then prints the groups sorted. This document follows one line through that pipeline and explains the choices that make it correct and fast.

## Files

| file | role |
|---|---|
| `src/funiq.cpp` | CLI: argument rewriting and parsing, colour decisions, input batching, progress bar, exit codes |
| `lib/funiq/Settings.h` | every option that shapes matching and output, with plain-run defaults |
| `lib/funiq/Matcher.h` | normalisation, grouping (serial and threaded), output |
| `lib/funiq/similarity.h` | distance functions: bounded Levenshtein, trigram profiles, cosine distance |
| `lib/funiq/Color.h` | colour decision (`color::want`) and SGR styling (`color::paint`) |
| `lib/tclap/`, `lib/catch/` | vendored TCLAP (argument parsing) and Catch2 v2 (tests) |
| `test/` | unit tests, CLI tests, golden fixtures (see BUILDING.md) |

## Pipeline

```
argv ─► rewrite (--opt=value, bare -d) ─► TCLAP ─► Settings
input ─► 256-line batches ─► Matcher::addBatch ─► groups ─► Matcher::show ─► stdout
                               │
            per line: normalize ─► memo? ─► scan groups (length check ─► distance)
```

### 1. Arguments (`parseCommandLine`)

Before TCLAP sees `argv`, two rewrites happen: `--opt=value` is split into `--opt value` (TCLAP only takes the latter), and a bare `-d` *not* followed by a number becomes `--default`. That keeps `-d 0.125` meaning "threshold 0.125" for existing scripts while `funiq -d log.txt` selects the preset. Options then map onto `Settings`. The preset is applied first, explicit flags refine it, and the default threshold depends on the method (3 edits; 0.125 for the normalized methods; 0.05 for cosine).

### 2. Normalisation (`Matcher::normalize`)

Each line is reduced to the string that is actually compared, in this order: lowercase (`-i`), collapse every run of digits to `0` (`-D`), then either sort its words (`-m token-sort`; with `-I` punctuation separates words) or delete non-alphanumerics (`-I`). Characters go through `unsigned char` before `<cctype>`, because negative `char` values (UTF-8 bytes) are undefined behaviour there. For `-m cosine` the normalised line also gets a trigram profile (see below). Together these form a `Candidate`, built once per line.

### 3. Grouping (`Matcher::add`)

A group is the line that started it (its *key*) plus every line that joined it. Each line:

1. **Memo:** if an identical normalised line was seen before, it goes to the same group with no comparison at all. That is safe for first-match grouping: groups created later always have higher indices, so a repeat's first match can never change. It is off under `--best` (a newer group can be closer) and for negative or NaN thresholds (nothing matches, not even identical lines).
2. **Scan:** otherwise compare against each group's key in creation order. Without `--best`, the first key within the threshold wins; with `--best`, the closest wins, ties going to the lower index, and an exact match stops the scan.
3. **Place:** join that group, recording the distance for `--scores`, or start a new group.

So grouping is greedy and order-dependent, like a single pass of leader clustering. Members are compared with the key, not with each other.

### 4. Distances (`similarity.h`)

**Levenshtein family.** `boundedLevenshtein(a, b, k)` returns the exact distance when it is at most `k`, and `k + 1` otherwise. Almost every comparison is a non-match against a far-away key, so rejecting early is what matters. In order:

1. **length bound:** at least `|len(a) − len(b)|` edits are needed;
2. **affix strip:** a common prefix and suffix never change the distance;
3. **Ukkonen band:** only cells with `|i − j| ≤ k` can stay within `k`;
4. **early abort:** once a whole row exceeds `k`, it can only grow.

The two DP rows live in a caller-provided scratch buffer, one per thread, so the hot loop never allocates. `Matcher::distanceTo` turns the threshold into a budget `k`. For the normalized methods it adds one edit of slack, and the original float comparison `dist / maxLen <= d` still decides. Results are therefore identical to computing the full distance. A cheaper length-only check (`lengthRulesOut`) runs first over a contiguous array of key lengths. The exact `levenshteinDistance` is simply the bounded version with a budget no pair can exceed.

**Cosine.** `trigramProfile` packs every 3-byte window exactly into 24 bits, so there is no hashing and there are no collisions. Strings shorter than 3 characters become one tagged gram. The profile is a sorted vector of (gram, count) pairs plus its norm, built once per key, so each comparison is a linear merge. There is no length bound for cosine.

### 5. Threads (`Matcher::addBatch`)

Input is read in batches of 256 lines. Each batch runs in two phases:

1. **Parallel, read-only:** every line is normalised and compared against the groups that existed before the batch (the "frozen" groups), on up to `-j` threads.
2. **Serial, in input order:** a line then checks only the groups created earlier in the same batch. All of those have higher indices than any frozen group, so a first-match hit from phase 1 is already the overall answer. Under `--best`, a new group wins only if it is strictly closer, which preserves the serial tie-break.

The output is therefore byte-identical for any thread count, and the unit tests check that in every mode. Batches of 256 keep the serial phase short when most lines start new groups, while giving the threads enough work per round.

### 6. Output (`Matcher::show`)

Each group is printed as its representative: the key, or its longest member with `--rep longest`. Groups are sorted alphabetically by representative. Rows are sorted through pointers, so group indices (which the memo relies on) never move. `-c` prints the size right-aligned in 7 columns, like `uniq -c`. `-a` and `--scores` append the other members, tab-separated.

### 7. Colour (`Color.h`, `funiq.cpp`)

Colour is decided once per stream. In order: `--color always|never`, then `FORCE_COLOR` (`0`/`false` turn it off, anything else on), then `NO_COLOR`, then `TERM=dumb`, then whether the stream is a terminal. On Windows "terminal" means `GetConsoleMode` succeeds (`_isatty` is also true for `NUL`), and VT processing is switched on and restored at exit. Styles close per attribute (SGR 22 for bold/dim, 39 for colour) and wrap already-padded fields, so stripping the codes gives the plain output byte for byte, which a unit test checks.

## Invariants and the tests that guard them

| invariant | guarded by |
|---|---|
| optimised distances equal the textbook definition | random cross-check against a full-matrix reference (unit) |
| output does not change between optimisations | golden outputs (CLI) |
| thread count never changes output | thread-equivalence test in every mode (unit) plus `-j` golden runs (CLI) |
| colour never changes content | strip-and-compare (unit and CLI) |
| README examples and usage are true | CLI tests run the examples and diff the usage block against `funiq -h` |

## Performance notes

Measured on synthetic nginx lines, with identical output at every step:

| change | effect |
|---|---|
| bounded Levenshtein and memo (vs the original full DP) | 34× (`--default`) to 460× (`-d 3`) on 2k lines |
| band-only DP initialisation, inline length check | 1.2–1.3× |
| threads (12 cores) | a further 3.9–9× on 20k lines |
| `-D` on logs | 13,024 → 109 groups, 5.5s → 0.2s (repeats hit the memo) |

Tried and rejected: a length-bucket index. Gathering and sorting candidates cost about as much as the length checks it skipped (0.8–1.2×). Also avoided: `thread_local` scratch buffers. In static MinGW builds their destructors ran after the emulated-TLS block was freed, corrupting the heap at thread exit. Ideas not yet tried are in TODO.md.
