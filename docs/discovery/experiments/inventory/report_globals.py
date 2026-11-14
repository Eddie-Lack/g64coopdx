#!/usr/bin/env python3
"""Reader and writer counts for named globals, from all/events.json (access.py --targets '*' --out all).

Usage: python3 report_globals.py NAME[,NAME...] > out.md
Columns: reads, writes (incl. ++ and +=), address taken, distinct functions touching, distinct functions writing,
files touching. Static variables that share a name across files are merged (a limit of the name-based scan).
"""
import collections, json, os, sys
here = os.path.dirname(os.path.abspath(__file__))
ev = [e for e in json.load(open(os.path.join(here, "all/events.json"))) if e["kind"] != "def"]
by = collections.defaultdict(list)
for e in ev:
    by[e["var"]].append(e)
print("| Global | reads | writes | & | functions | writing fns | files |")
print("| --- | --- | --- | --- | --- | --- | --- |")
for n in sys.argv[1].split(","):
    xs = by.get(n, [])
    r = sum(1 for x in xs if x["kind"] in ("read", "write+read"))
    w = sum(1 for x in xs if x["kind"] in ("write", "write+read"))
    a = sum(1 for x in xs if x["kind"] == "addr")
    fs = {(x["file"], x["func"]) for x in xs}
    fw = {(x["file"], x["func"]) for x in xs if x["kind"] in ("write", "write+read")}
    fl = {x["file"] for x in xs}
    print("| `%s` | %d | %d | %d | %d | %d | %d |" % (n, r, w, a, len(fs), len(fw), len(fl)))
