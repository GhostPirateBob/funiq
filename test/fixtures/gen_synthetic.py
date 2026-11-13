"""Deterministic synthetic log fixture for funiq golden tests.

Only TEST-NET IPs (RFC 5737) and example.com hosts, so it is safe to commit
to a public repo. Mixes near-duplicates (varying digits), distinct request
shapes, exact repeats and varied lengths.

Usage: python test/fixtures/gen_synthetic.py OUT [N]
N base lines (default 500, which reproduces test/fixtures/synthetic.log)
plus N/5 exact repeats. Larger N makes benchmark input.
"""

from __future__ import annotations

import random
import sys

random.seed(1337)
NETS = ["192.0.2", "198.51.100", "203.0.113"]
PATHS = ["/", "/wp-login.php", "/xmlrpc.php", "/.env", "/admin/config.php",
         "/api/v1/users", "/static/app.js", "/favicon.ico", "/robots.txt",
         "/cgi-bin/luci/;stok=/locale", "/vendor/phpunit/src/Util/PHP/eval-stdin.php"]
UAS = ["Mozilla/5.0 (X11; Linux x86_64)", "curl/8.5.0", "python-requests/2.31",
       "Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15"]
PHP = ["PHP Warning:  Undefined array key \"{k}\" in /var/www/example.com/{f}.php on line {n}",
       "PHP Fatal error:  Uncaught TypeError: count(): Argument #1 must be of type Countable in /var/www/example.com/{f}.php:{n}",
       "[error] {n}#{n}: *{m} open() \"/var/www/example.com{p}\" failed (2: No such file or directory)"]

base = int(sys.argv[2]) if len(sys.argv) > 2 else 500
lines: list[str] = []
for _ in range(base):
    r = random.random()
    if r < 0.6:
        ip = f"{random.choice(NETS)}.{random.randint(1, 254)}"
        lines.append(f'{ip} - - [13/Nov/2026:{random.randint(0,23):02}:{random.randint(0,59):02}:{random.randint(0,59):02} +0800] '
                     f'"GET {random.choice(PATHS)} HTTP/1.1" {random.choice([200, 301, 404])} {random.randint(100, 9999)} '
                     f'"-" "{random.choice(UAS)}"')
    else:
        lines.append(random.choice(PHP).format(k=random.choice(["id", "user", "page"]),
                                               f=random.choice(["index", "lib/db", "admin/save"]),
                                               n=random.randint(1, 999), m=random.randint(1, 99999),
                                               p=random.choice(PATHS)))
# exact repeats, scattered
for _ in range(base // 5):
    lines.insert(random.randrange(len(lines)), random.choice(lines))

with open(sys.argv[1], "w", encoding="utf-8", newline="\n") as out:
    out.write("\n".join(lines) + "\n")
