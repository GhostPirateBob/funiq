# TODO

Ideas collected while building 1.x, roughly by value. Nothing here is committed to; measure before optimising (see ARCHITECTURE.md for what was tried).

## Should do

- **Tune the cosine default** (0.05) on real logs. At 0.2 it merged different request paths on access logs because shared User-Agent strings dominate the trigram vector.
- **Decide whether `-D` belongs in `--default`.** It is the biggest quality and speed lever for logs, but changes `--default` output, and 0.125 was tuned without it.
- **Delete merged branches** on GitHub.

## Nice to have

- `--input-order`: print groups in first-seen order instead of sorted (closer to `uniq` on unsorted input).
- `-I` variant that turns punctuation into spaces instead of deleting it, keeping word boundaries outside token-sort.
- Machine-readable output: TSV or JSON with group, count, members and scores.
- `--stats`: lines read, groups, memo hits, time.
- An ETA on the progress bar.
- A man page generated from the help text.
- A code coverage report in CI.
- Windows arm64 release binary (needs an aarch64 MinGW toolchain on the runner).

## Might do (performance)

The remaining cost on very diverse input is the banded DP for each (line, key) pair.

- **mbleven** for thresholds of 3 edits or fewer: try the at most 7 edit scripts directly instead of running a DP (rapidfuzz-cpp does this).
- **Bit-parallel Levenshtein** (Myers/Hyyrö) with a pattern table cached per key, for normalized thresholds where the band is wider.
- **q-gram count filter** before the DP for the normalized methods: if `ed(a, b) ≤ k`, they share at least `max(|a|,|b|) − q + 1 − kq` q-grams (counted with multiplicity), so most pairs can be rejected by a profile merge.
- A persistent thread pool instead of starting threads per batch.
- Revisit a length index with a k-way merge instead of sort, now that the per-pair cost is lower.

## Might do (matching)

- Order independence: an optional second pass that re-centres each group on its medoid, or assigns lines to the nearest final group.
- Weighted Levenshtein (cheaper digit-for-digit substitutions) as a softer alternative to `-D`.
- `-m indel` (insertions and deletions only) and `-m jaccard`.
- Unicode-aware distances (compare code points instead of UTF-8 bytes).
