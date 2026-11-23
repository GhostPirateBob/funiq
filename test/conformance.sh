#!/usr/bin/env bash
# Differential conformance: CANDIDATE must behave exactly like REF (the C++
# funiq) on a corpus of inputs crossed with test/conformance-flags.txt: the
# same stdout bytes and the same exit code. Used for the Rust and Go ports.
# Usage: test/conformance.sh path/to/funiq path/to/candidate
set -u
REF=${1:?usage: $0 REF CANDIDATE}
CAND=${2:?usage: $0 REF CANDIDATE}
# Colour is decided from these; the caller's environment must not leak in.
unset FORCE_COLOR NO_COLOR

DIR=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PY=$(command -v python3 || command -v python)
"$PY" "$DIR/fixtures/gen_edge_cases.py" "$TMP" || { echo "conformance: cannot generate inputs"; exit 1; }
SYNTH="$DIR/fixtures/synthetic.log"

pass=0
fail=0
ok() { pass=$((pass + 1)); echo "PASS  $1"; }
ko() { fail=$((fail + 1)); echo "FAIL  $1"; [ -n "${2:-}" ] && echo "      $2"; }

# same NAME INPUT ARGS...: identical stdout and exit code with INPUT as the
# file argument.
same() {
	local name=$1 input=$2; shift 2
	"$REF" "$@" "$input" > "$TMP/ref.out" 2>/dev/null; local rr=$?
	"$CAND" "$@" "$input" > "$TMP/cand.out" 2>/dev/null; local cr=$?
	if [ $rr -eq $cr ] && cmp -s "$TMP/ref.out" "$TMP/cand.out"; then ok "$name"
	else ko "$name" "exit $rr vs $cr; $(cmp "$TMP/ref.out" "$TMP/cand.out" 2>&1 | head -1)"; fi
}

# --- the flag matrix over the synthetic log and the edge cases --------------
while read -r flags; do
	[ -z "$flags" ] && continue
	# shellcheck disable=SC2086 # each line is a list of arguments
	same "synthetic.log $flags" "$SYNTH" $flags
	# shellcheck disable=SC2086
	same "edge.txt $flags" "$TMP/edge.txt" $flags
done < "$DIR/conformance-flags.txt"

# --- 70 KB and 3 MB lines: a subset, the rest would only cost time ----------
for flags in "-d 3" "-d" "-d 0 -c" "-a -d 2"; do
	# shellcheck disable=SC2086
	same "long.txt $flags" "$TMP/long.txt" $flags
done

# --- stdin: redirect and pipe ------------------------------------------------
"$REF" -d -c < "$SYNTH" > "$TMP/ref.out"
"$CAND" -d -c < "$SYNTH" > "$TMP/cand.out"
cmp -s "$TMP/ref.out" "$TMP/cand.out" && ok "stdin < synthetic.log -d -c" || ko "stdin < synthetic.log -d -c"
cat "$SYNTH" | "$CAND" -d -c > "$TMP/cand.out"
cmp -s "$TMP/ref.out" "$TMP/cand.out" && ok "pipe synthetic.log -d -c" || ko "pipe synthetic.log -d -c"

# --- errors ------------------------------------------------------------------
name="missing file: non-zero exit, message names the file"
err=$("$CAND" "$TMP/does-not-exist.txt" 2>&1 >/dev/null); rc=$?
if [ $rc -ne 0 ] && printf '%s' "$err" | grep -q 'does-not-exist.txt'; then ok "$name"; else ko "$name" "rc=$rc stderr='$err'"; fi
for bad in "-d inf" "-d nan" "--rep sideways" "-x"; do
	# shellcheck disable=SC2086
	"$REF" $bad "$SYNTH" >/dev/null 2>&1; rr=$?
	# shellcheck disable=SC2086
	"$CAND" $bad "$SYNTH" >/dev/null 2>&1; cr=$?
	if [ $rr -ne 0 ] && [ $cr -ne 0 ]; then ok "rejects $bad"; else ko "rejects $bad" "exit $rr vs $cr"; fi
done

# --- version -------------------------------------------------------------------
rv=$("$REF" --version | awk '{print $2}'); cv=$("$CAND" --version | awk '{print $2}')
[ "$rv" = "$cv" ] && ok "--version $cv" || ko "--version" "'$rv' vs '$cv'"

# --- broken pipe: quiet exit -------------------------------------------------
# 3 MB of output into head -1, which exits after the first line.
name="broken pipe into head -1 leaves stderr empty"
"$CAND" -d 0 "$TMP/long.txt" 2> "$TMP/pipe.err" | head -1 > /dev/null
if [ ! -s "$TMP/pipe.err" ]; then ok "$name"; else ko "$name" "stderr: $(head -c 200 "$TMP/pipe.err")"; fi

echo "conformance: $pass passed, $fail failed"
[ $fail -eq 0 ]
