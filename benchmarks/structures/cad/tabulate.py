#!/usr/bin/env python3
"""Markdown summary of cad/run.py's JSON lines.  Usage: tabulate.py cad.jsonl"""
import json
import sys

recs = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
print("| Variant | Completes | Build | Sequential, per task (types 0-3) | N=1 | N=2 | N=4 | N=6 | N=12 | Peak RSS |")
print("|---------|-----------|------:|---------------------------------|----:|----:|----:|----:|-----:|---------:|")
workers = {}
for r in recs:
    if r.get("variant") == "node-workers" and r.get("completed"):
        res = r["results"][0]
        workers[res["n"]] = (res, r)
for r in recs:
    v = r.get("variant")
    if v == "node-workers":
        continue
    if not r.get("completed"):
        print("| %s | no: %s | | | | | | | | |" % (v, r.get("error", "")[:80]))
        continue
    seq = r.get("seq_task_ms", [])[:4]
    per = {x["n"]: x["median_ms"] for x in r.get("results", [])}
    cells = ["%s" % per.get(n, "-") + (" ms" if n in per else "") for n in (1, 2, 4, 6, 12)]
    print("| %s | yes | %.1f s | %s ms | %s | %.2f GB |" % (
        v, r["build_ms"] / 1000.0, " / ".join(str(x) for x in seq), " | ".join(cells),
        r["peak_rss_kb"] / 1048576.0))
if workers:
    any_r = next(iter(workers.values()))[1]
    cells = []
    for n in (1, 2, 4, 6, 12):
        if n in workers:
            res, _ = workers[n]
            cells.append("%d ms (post %d, clone %d, compute %d)" % (
                res["median_ms"], res["post_ms"], res["clone_ms"], res["compute_ms"]))
        else:
            cells.append("-")
    rss = " / ".join("%.1f" % (workers[n][1]["peak_rss_kb"] / 1048576.0) for n in sorted(workers))
    print("| node-workers | yes | %.1f s | %s ms | %s | %s GB |" % (
        any_r["build_ms"] / 1000.0, " / ".join(str(x) for x in any_r.get("seq_task_ms", [])[:4]),
        " | ".join(cells), rss))
