#!/usr/bin/env python3
"""Self-report check: run every standard benchmark and every probe once with
each protojs binary and with Node, and compare the work each one reports
(every BENCH_RESULT field except timings; the CHECK line of the probes)."""
import json, os, subprocess, sys
M = os.path.dirname(os.path.abspath(__file__))
STD = os.path.join(M, "after/protoJS/tests/benchmarks/standard")
bins = {"before": sys.argv[1], "after": sys.argv[2], "node": "node"}
TIMEKEYS = {"time_ms", "times", "median_ms", "min_ms", "max_ms", "elapsed_ms", "total_ms", "samples"}
def run(b, f):
    p = subprocess.run([b, f], capture_output=True, text=True, timeout=300, cwd=os.path.dirname(f))
    return p.returncode, p.stdout
def bench_fields(out):
    for l in reversed(out.splitlines()):
        i = l.find("__BENCH_RESULT__")
        if i >= 0:
            d = json.loads(l[i + 16:])
            return {k: v for k, v in d.items() if k not in TIMEKEYS and not k.endswith("_ms")}
    return None
bad = 0
for f in sorted(os.listdir(STD)):
    if not f.endswith(".js") or "worker" in f: continue
    res = {k: run(b, os.path.join(STD, f)) for k, b in bins.items()}
    fl = {k: bench_fields(o) for k, (rc, o) in res.items()}
    ok = all(rc == 0 for rc, _ in res.values()) and fl["before"] is not None and fl["before"] == fl["after"] == fl["node"]
    bad += not ok
    print(("OK  " if ok else "BAD ") + f, json.dumps(fl["after"]) if ok else json.dumps({k: [res[k][0], fl[k]] for k in bins}))
P = os.path.join(M, "js_probes")
for f in sorted(os.listdir(P)):
    if not f.endswith(".js"): continue
    res = {k: run(b, os.path.join(P, f)) for k, b in bins.items()}
    chk = {k: [l for l in o.splitlines() if l.startswith("CHECK")] for k, (rc, o) in res.items()}
    ok = all(rc == 0 for rc, _ in res.values()) and chk["before"] and chk["before"] == chk["after"] == chk["node"]
    bad += not ok
    print(("OK  " if ok else "BAD ") + f, chk["after"] if ok else {k: [res[k][0], chk[k]] for k in bins})
print("VERIFY_BAD", bad)
