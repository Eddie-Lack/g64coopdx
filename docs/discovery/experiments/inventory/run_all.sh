#!/bin/sh
# Reruns the scans and the frame-limit probe. Needs clang, python3 3.8+, and the weemitt checkout at $REPO (default
# /home/user/weemitt).
# The gdb probe (probe.gdb) also needs gdb, a built build/headless/weemitt and a ROM in $ROM; it prints only numbers.
# This script doesn't run probe2.gdb or probe3.gdb.
set -e
H=$(cd "$(dirname "$0")" && pwd)
REPO=${REPO:-/home/user/weemitt}
mkdir -p "$H/all"
python3 "$H/access.py" --repo "$REPO" --out "$H"                    # events.json: hand, ball and friends
python3 "$H/access.py" --repo "$REPO" --out "$H/all" --targets '*'  # all/events.json: every global
python3 "$H/report.py" "$H/events.json" > "$H/tables.md"
python3 "$H/report_globals.py" "$1" > "$H/globals.md" 2>/dev/null || true
if [ -n "$ROM" ]; then
  mkdir -p "$H/probe"
  for L in 0 8 9 10 11 12 13 14 15 16 17 18 20 21 22 23 25 26 27 28 30 31 32 33 35 36 37 38 39; do
    gdb -q -batch -x "$H/probe.gdb" --args "$REPO/build/headless/weemitt" --rom "$ROM" --headless 600 --level $L 2>&1 | grep '^FRAME' > "$H/probe/L$L.txt"
  done
  python3 "$H/probe_sum.py" > "$H/probe_table.md"
fi
