#!/usr/bin/env bash
# Benchmark funiq binaries (C++, Rust, Go, MSVC C++...) against each other on
# synthetic logs. Every binary's output is compared with the first one's
# before anything is timed; a difference stops the run.
# Writes bench/results/<date>-<os>-<arch>.md. Needs hyperfine and python3.
# Usage: bench/bench.sh [--quick] BIN...    (make bench passes the built ones)
set -euo pipefail

quick=0
if [ "${1:-}" = "--quick" ]; then quick=1; shift; fi
[ $# -ge 1 ] || { echo "usage: $0 [--quick] BIN..." >&2; exit 2; }
command -v hyperfine >/dev/null || { echo "bench: hyperfine not found" >&2; exit 1; }

DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$DIR")
DATA="$DIR/data"
mkdir -p "$DATA" "$DIR/results"
PY=$(command -v python3 || command -v python)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Native Windows programs (hyperfine, funiq) don't understand /d/... paths
# inside a command string, so hand them mixed C:/... paths there.
native() { if command -v cygpath >/dev/null; then cygpath -m "$1"; else printf '%s\n' "$1"; fi; }

bins=()
for b in "$@"; do bins+=("$(cd "$(dirname "$b")" && pwd)/$(basename "$b")"); done

case "$(uname -s)" in
	MINGW*|MSYS*|CYGWIN*) os=windows ;;
	Linux) os=linux ;;
	Darwin) os=macos ;;
	*) os=$(uname -s) ;;
esac
arch=$(uname -m)
threads=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo "?")
case $os in
	windows) cpu=$(pwsh.exe -NoLogo -NoProfile -Command "(Get-CimInstance Win32_Processor).Name" 2>/dev/null | tr -d '\r' || true) ;;
	linux) cpu=$(sed -n 's/^model name[^:]*: //p' /proc/cpuinfo | head -1) ;;
	macos) cpu=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || true) ;;
esac
cpu=$(printf "%s" "${cpu:-}" | sed "s/[[:space:]]*$//")
[ -n "$cpu" ] || cpu=$(sed -n 's/^\(CPU part\|Hardware\)[^:]*: //p' /proc/cpuinfo 2>/dev/null | head -1 || true)

# Inputs: N base lines + N/5 exact repeats each (gen_synthetic.py).
for n in 24000 240000; do
	[ $quick = 1 ] && [ $n = 240000 ] && continue
	[ -f "$DATA/$((n / 1000))k.log" ] || "$PY" "$ROOT/test/fixtures/gen_synthetic.py" "$DATA/$((n / 1000))k.log" "$n"
done

# label | funiq arguments | input
cases=(
	"Levenshtein -d 3, 1 thread|-d 3 -j 1|24k"
	"Levenshtein -d 3, all cores|-d 3|24k"
	"preset -d (normalized 0.125 -iI), 1 thread|-d -j 1|24k"
	"preset -d, all cores|-d|24k"
	"token-sort, 1 thread|-m token-sort -j 1|24k"
	"token-sort, all cores|-m token-sort|24k"
	"cosine -d 0.2, 1 thread|-m cosine -d 0.2 -j 1|24k"
	"cosine -d 0.2, all cores|-m cosine -d 0.2|24k"
	"--best -d 3, 1 thread|--best -d 3 -j 1|24k"
	"--best -d 3, all cores|--best -d 3|24k"
	"logs -d -D, 1 thread|-d -D -j 1|240k"
	"logs -d -D, all cores|-d -D|240k"
	"27 MB of -a output, 1 thread|-d -D -a -j 1|240k"
)
runs=10 warmup=2
[ $quick = 1 ] && runs=3 warmup=1

out="$DIR/results/$(date +%Y-%m-%d)-$os-$arch.md"
{
	echo "# funiq benchmark: $os $arch"
	echo
	echo "$(date '+%Y-%m-%d %H:%M %Z'). CPU: ${cpu:-unknown}, $threads threads. hyperfine, $warmup warmup + $runs runs per binary;"
	echo "every binary's output was checked identical to \`$(basename "${bins[0]}")\` before timing."
	echo "Inputs: test/fixtures/gen_synthetic.py (24k = 24,000 base lines + 4,800 repeats; 240k = 240,000 + 48,000)."
	echo
	echo '```'
	(cd "$ROOT" && make -s toolchain 2>&1) || true
	echo '```'
} > "$out"

for c in "${cases[@]}"; do
	IFS='|' read -r label args input <<< "$c"
	file="$DATA/$input.log"
	[ -f "$file" ] || continue
	# shellcheck disable=SC2086 # args is a list of arguments
	"${bins[0]}" $args "$file" > "$TMP/ref.out"
	for b in "${bins[@]:1}"; do
		# shellcheck disable=SC2086
		"$b" $args "$file" > "$TMP/cand.out"
		if ! cmp -s "$TMP/ref.out" "$TMP/cand.out"; then
			echo "bench: $(basename "$b") differs from $(basename "${bins[0]}") on: $args $input" >&2
			exit 1
		fi
	done
	cmds=()
	# hyperfine -N splits each command like a shell would: quote the paths
	for b in "${bins[@]}"; do cmds+=(-n "$(basename "$b" .exe)" "\"$(native "$b")\" $args \"$(native "$file")\""); done
	output_mode=null
	case $args in *" -a "*) output_mode=pipe ;; esac
	echo "bench: $label ($input)" >&2
	hyperfine -N --style none --warmup $warmup --runs $runs --output=$output_mode \
		--export-markdown "$TMP/case.md" "${cmds[@]}" > /dev/null
	{ echo; echo "## $label ($input, \`$args\`)"; echo; cat "$TMP/case.md"; } >> "$out"
done

# Peak memory of the counts-only log case.
if [ -f "$DATA/240k.log" ]; then
	{ echo; echo "## Peak memory: logs \`-d -D -c -j 1\` (240k)"; echo; echo "| binary | peak MB |"; echo "|---|---:|"; } >> "$out"
	for b in "${bins[@]}"; do
		# measured with /usr/bin/time outside Windows; '?' rather than aborting
		# (set -e) after all the timings when it isn't installed
		if [ "$os" != windows ] && [ ! -x /usr/bin/time ]; then
			echo "| $(basename "$b" .exe) | ? (no /usr/bin/time) |" >> "$out"
			continue
		fi
		case $os in
			windows) mb=$(pwsh.exe -NoLogo -NoProfile -File "$(native "$DIR/peakmem.ps1")" "$(native "$b")" "$(native "$DATA/240k.log")" " -d -D -c -j 1" | tr -d '\r') ;;
			linux) mb=$( { /usr/bin/time -f %M "$b" -d -D -c -j 1 "$DATA/240k.log" > /dev/null; } 2>&1 | tail -1 | awk '{printf "%.1f", $1 / 1024}') ;;
			macos) mb=$( { /usr/bin/time -l "$b" -d -D -c -j 1 "$DATA/240k.log" > /dev/null; } 2>&1 | awk '/maximum resident/ {printf "%.1f", $1 / 1048576}') ;;
			*) mb="?" ;;
		esac
		echo "| $(basename "$b" .exe) | $mb |" >> "$out"
	done
fi

echo "bench: wrote $out" >&2
