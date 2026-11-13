# Wiki outline

A plan for a GitHub wiki, one section per page. Each page lists what it should cover and where the material already lives, so it can be written by expanding these notes.

## Home

What funiq is in two sentences, the one-line example (`funiq -iI -d 4 -c ships.txt`), and links to the pages below. Source: README intro.

## Installing

Pre-built binaries (once releases exist), `make install`, Windows `BINDIR` usage, verifying with `funiq --version`. Source: BUILDING.md (Installing).

## Getting started

- Reading files and stdin
- `-c`, `-a` and `--scores`
- Why output is sorted
- Choosing `-i`/`-I`

Walk through the starship example step by step. Source: README examples.

## Recipes

- **Deduplicating error logs:** `funiq -d -D -c error.log | sort -rn`, with before/after counts.
- **nginx access logs:** masking IPs and timestamps; why cosine merges by User-Agent.
- **Shell history:** `history | cut -c8- | funiq -d 2`.
- **Names and titles with typos:** normalized vs absolute thresholds.
- **Word-order noise:** `-m token-sort -I`.
- **Combining with `sort`, `bat` and `uniq`** in pipelines.

## Choosing a method and threshold

The methods table, how each distance is computed, and a tuning procedure:

1. run with `--scores` on a sample;
2. look at the distances of true duplicates versus distinct lines;
3. pick `-d` between them.

Why 0.125 is the preset. How `-D` changes the picture. Source: README "Comparison methods", ARCHITECTURE.md.

## Grouping semantics

- First match vs `--best`, and representatives (`--rep`)
- Why results depend on input order (greedy leader clustering)
- That members are compared with the key, not with each other
- Worked examples where order matters

Source: ARCHITECTURE.md §3.

## Performance

The measured history (34–460×, threads, `-D`), how to benchmark with `-j 1` against all cores, what dominates the cost on diverse input, and what was tried and rejected. Source: ARCHITECTURE.md (Performance notes), CHANGELOG.

## Colour and terminals

What is coloured and why, `--color`, `NO_COLOR`/`FORCE_COLOR` precedence, Windows console notes. Source: ARCHITECTURE.md §7.

## Internals

The pipeline diagram, bounded Levenshtein (length bound, affix strip, band, early abort), trigram profiles, the two-phase threaded batch, and the invariants table. Source: ARCHITECTURE.md.

## Building and contributing

Toolchains, tests (unit, CLI, golden outputs, when a golden file may be regenerated), the README usage check, CI. Source: BUILDING.md.

## FAQ

- Why is a line I expected to match on its own? (threshold, normalisation; check with `--scores`)
- Why does `-j` not change the output?
- Why doesn't piped output have colour?
- Why does `-d 0.125` mean a threshold but `-d log.txt` mean the preset?
- Does funiq handle UTF-8? (bytes, not code points; see TODO.md)

## Changelog

Link to CHANGELOG.md.
