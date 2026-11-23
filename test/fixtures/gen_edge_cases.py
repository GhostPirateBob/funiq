"""Edge-case inputs for test/conformance.sh, written at run time.

Usage: python test/fixtures/gen_edge_cases.py OUTDIR
Writes OUTDIR/edge.txt (short, awkward lines) and OUTDIR/long.txt (lines of
70 KB and 3 MB). Generated rather than committed, so git line-ending
conversion can never touch the CRLF and 0x1A bytes they contain.
"""

from __future__ import annotations

import sys
from pathlib import Path


def one_edit(line: bytes, at: int) -> bytes:
    """`line` with the byte at `at` replaced by a different one."""
    return line[:at] + (b"Z" if line[at:at + 1] != b"Z" else b"Y") + line[at + 1:]


def edge_lines() -> list[bytes]:
    lines = [
        b"", b"", b"a", b"ab", b"abc", b"a", b"",       # empty and short (cosine's 1-2 byte grams)
        b"col1\tcol2\tcol3", b"col1\tcol2\tcol4",        # tabs
        b"nul\x00byte", b"nul\x00bytes",                  # NUL inside a line
        b"caf\xe9", b"caf\xc3\xa9", b"CAF\xc3\x89",       # Latin-1 and UTF-8 bytes >= 0x80
        b"ctrl\x1az", b"ctrl\x1az!",                      # 0x1A (Windows text-mode EOF)
        b"crlf line\r", b"crlf lime\r",                   # \r before \n (CRLF input)
        b"Hello, World!", b"HELLO WORLD", b"hello world",
        b"pid 123 exited", b"pid 98765 exited", b"pid exited",
        b"connection reset by peer", b"peer by reset connection", b"connection refused",
        b"user=bob,ip=10.0.0.1", b"ip=10.0.0.1 user=bob",
    ]
    # one substitution at lengths where 1/len lands on common thresholds, and
    # around the bit-parallel kernels' 64-byte limits
    for n in (8, 10, 16, 24, 30, 63, 64, 65, 100, 129):
        base = bytes(b"abcdefghijklmnopqrstuvwxyz0123456789"[i % 36] for i in range(n))
        lines += [base, one_edit(base, n // 2), one_edit(one_edit(base, 1), n - 2)]
    lines += [b"bba", b"baab", b"a", b"aab", b"aaaaa", b"baab"]  # equal representatives under --best --rep longest
    lines += [b"MiXeD CaSe", b"mixed case", b"MIXED-CASE"]
    return lines


def main() -> None:
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    # last line deliberately without its \n
    (out / "edge.txt").write_bytes(b"\n".join(edge_lines() + [b"no newline at the end"]))
    long_line = bytes(b"0123456789abcdef"[i % 16] for i in range(70_000))
    huge_line = b"x" * 3_000_000
    (out / "long.txt").write_bytes(b"\n".join([long_line, one_edit(long_line, 35_000), huge_line, b"abc", b"abd"]))


if __name__ == "__main__":
    main()
