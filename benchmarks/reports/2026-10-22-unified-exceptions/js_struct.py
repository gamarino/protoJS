import json, math, statistics as st, sys, os
O = os.path.join(os.path.dirname(os.path.abspath(__file__)), "js_out")
def load(v):
    d = {}
    for l in open(os.path.join(O, "structures_%s.jsonl" % v)):
        r = json.loads(l)
        if r.get("completed") and "median_ms" in r and "mode" in r:
            key = (r.get("workload"), r.get("runtime"), r["mode"], r.get("n"))
            d.setdefault(key, []).append(r["median_ms"])
    return {k: st.median(v) for k, v in d.items()}
a, b = load("after"), load("before")
ks = sorted(k for k in a if k[1] == "protojs" and k in b)
rs = [a[k] / b[k] for k in ks if b[k] > 0]
print("cells", len(rs), "geomean AFTER/BEFORE %.3f  min %.2f max %.2f" % (math.exp(sum(map(math.log, rs)) / len(rs)), min(rs), max(rs)))
for wl in sorted(set(k[0] for k in ks)):
    r = [a[k] / b[k] for k in ks if k[0] == wl]
    print("  %-9s geomean %.3f" % (wl, math.exp(sum(map(math.log, r)) / len(r))))
print("| workload | mode | N | AFTER ms | BEFORE ms | ratio | Node ms |")
for k in ks:
    nk = (k[0], "node", k[2], k[3])
    print("| %s | %s | %s | %d | %d | %.2f | %s |" % (k[0], k[2], k[3], a[k], b[k], a[k] / b[k], int(a.get(nk) or b.get(nk) or 0) if (nk in a or nk in b) else "-"))
