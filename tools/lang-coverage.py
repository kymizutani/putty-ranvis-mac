#!/usr/bin/env python3
"""Report how many of the Japanese language file's strings are ones the
Unix (GTK) build of this source actually contains.

  tools/lang-coverage.py [path/to/putty.lng]

Anything the Unix sources don't contain is either a Windows-only string
or something we should have translated and didn't - which is how the
missing Font page keys were found. Run it from anywhere; the paths come
from where this file is.
"""
import re, subprocess, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
lng = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    ROOT, "windows/lang/0411/putty.lng")

data = open(lng, "rb").read()
assert data[:2] == b"\xff\xfe"
text = data[2:].decode("utf-16-le")

section = None
keys = []
for line in text.replace("\r\n", "\n").split("\n"):
    line = line.strip()
    if not line or line.startswith(";"):
        continue
    m = re.match(r"^\[(.*)\]$", line)
    if m:
        section = m.group(1)
        continue
    if section != "Japanese" or "=" not in line:
        continue
    k, v = line.split("=", 1)
    k = re.sub(r"%([0-9A-Fa-f]{2})", lambda mo: chr(int(mo.group(1), 16)), k)
    if k.startswith("_"):
        continue
    keys.append((k, v))

unix_dirs = ["config.c", "settings.c", "logging.c", "dialog.c", "main.c",
             "unix", "ssh", "terminal", "utils", "charset", "proxy", "otherbackends"]
hit = miss = 0
missing = []
for k, v in keys:
    r = subprocess.run(["grep", "-rFql", "--", k] + unix_dirs,
                       cwd=ROOT,
                       capture_output=True, text=True)
    if r.returncode == 0:
        hit += 1
    else:
        miss += 1
        missing.append(k)

print(f"strings in {os.path.basename(lng)}: {len(keys)}")
print(f"  present in Unix/GTK sources : {hit}")
print(f"  Windows-only (not shown)    : {miss}")
print("\nexamples of Windows-only strings:")
for k in missing[:15]:
    print("   ", k)
