"""Regenerate README.md's usage block from `funiq -h`.

test/cli_test.sh fails when the two drift apart; run this after changing the
help text: python test/update_readme_usage.py bin/funiq
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

readme = Path(__file__).resolve().parent.parent / "README.md"
binary = Path(sys.argv[1] if len(sys.argv) > 1 else "bin/funiq").resolve()
help_text = subprocess.run([str(binary), "-h"], capture_output=True, text=True, check=True).stdout
block = "```\n" + help_text.rstrip("\n") + "\n```\n"
text = readme.read_text(encoding="utf-8")
new, n = re.subn(r"(<!-- usage:start[^>]*-->\n)(.*?)(<!-- usage:end -->)",
                 lambda m: m.group(1) + block + m.group(3), text, flags=re.S)
if n != 1:
    sys.exit("README.md: usage markers not found exactly once")
readme.write_text(new, encoding="utf-8", newline="\n")
print("README.md usage block updated")
