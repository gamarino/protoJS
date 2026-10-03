#!/usr/bin/env python3
"""Markdown tables from run.py's JSON lines (the last record of each
configuration wins).  Usage: tabulate.py results.jsonl"""
import json
import sys

recs = {}
for line in open(sys.argv[1]):
    r = json.loads(line)
    if r.get("bench") != "structures":
        continue
    key = (r.get("workload"), r.get("runtime"), r.get("mode"), int(r.get("n", 0)))
    recs[key] = r

workloads = []
for (wl, _, _, _) in recs:
    if wl not in workloads:
        workloads.append(wl)
for wl in workloads:
    ns = sorted({k[3] for k in recs if k[0] == wl})
    print("\n### %s\n" % wl)
    print("| N | protoJS seq | protoJS Deferred | speedup | GC | RSS | Node seq | Node workers | speedup | post / clone / compute | RSS |")
    print("|--:|--:|--:|--:|--:|--:|--:|--:|--:|--|--:|")
    for n in ns:
        ps, pp = recs.get((wl, "protojs", "seq", n)), recs.get((wl, "protojs", "par", n))
        nd, nw = recs.get((wl, "node", "seq", n)), recs.get((wl, "node", "par", n))
        if not (ps and pp and nd and nw):
            continue
        cells = ["%d" % n, "%d ms" % ps["median_ms"], "%d ms" % pp["median_ms"],
                 "%.2f" % (ps["median_ms"] / max(pp["median_ms"], 1)), "%d" % pp["gc_cycles"],
                 "%d MB" % (pp["peak_rss_kb"] // 1024), "%d ms" % nd["median_ms"]]
        if nw.get("completed", True):
            cells += ["%d ms" % nw["median_ms"], "%.2f" % (nd["median_ms"] / max(nw["median_ms"], 1)),
                      "%s / %s / %s ms" % (nw["post_ms"], nw["clone_ms"], nw["compute_ms"]),
                      "%d MB" % (nw["peak_rss_kb"] // 1024)]
        else:
            err = nw["error"].replace("FAILED: ", "").split("|")[0].strip()
            cells += ["fails (%s)" % err[:60], "-", "-", "-"]
        print("| " + " | ".join(cells) + " |")
