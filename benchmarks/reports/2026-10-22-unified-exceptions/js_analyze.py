#!/usr/bin/env python3
import json, os, statistics as st, math, sys, collections
M = os.path.dirname(os.path.abspath(__file__)); O = os.path.join(M, "js_out")
OLD = "/home/gamarino/Documentos/proyectos/protoJS/benchmarks/reports/2026-10-21-cpp-exceptions"
def load(prefix, d=O):
    r = collections.defaultdict(lambda: {"p": [], "n": []})
    for i in (1, 2, 3):
        f = os.path.join(d, "%s_r%d.json" % (prefix, i))
        if not os.path.exists(f): continue
        for x in json.load(open(f))["results"]:
            if x["protojs_time_ms"] is not None: r[x["name"] + ("" if x["benchmark"][:-3] == x["name"] else "(" + x["benchmark"][:-3] + ")")]["p"].append(x["protojs_time_ms"])
            if x["nodejs_time_ms"] is not None: r[x["name"] + ("" if x["benchmark"][:-3] == x["name"] else "(" + x["benchmark"][:-3] + ")")]["n"].append(x["nodejs_time_ms"])
    return r
b, a, old = load("before"), load("after"), load("base", OLD)
print("| Workload | AFTER | BEFORE | AFTER/BEFORE | Node | old base 8182924ea (10-21) |\n|---|---:|---:|---:|---:|---:|")
ratios = []; rows = []
for k in sorted(a, key=lambda k: -st.median(a[k]["p"])):
    pa, pb = st.median(a[k]["p"]), st.median(b[k]["p"])
    node = st.median(a[k]["n"] + b[k]["n"])
    o = st.median(old[k]["p"]) if k in old and old[k]["p"] else None
    r = pa / pb if pb else float("nan")
    if pb >= 10: ratios.append(r)
    print("| `%s` | %g | %g | %.3f%s | %g | %s |" % (k, pa, pb, r, "" if pb >= 10 else " (<10 ms)", node, "%g" % o if o is not None else "-"))
print("\ngeomean AFTER/BEFORE (BEFORE >= 10 ms, n=%d): %.3f" % (len(ratios), math.exp(sum(map(math.log, ratios)) / len(ratios))))
print("\nper-round raw (ms):")
for k in sorted(a): print(" ", k, "after", a[k]["p"], "before", b[k]["p"], "node", a[k]["n"] + b[k]["n"])
# probes
it = {"throw_primitive": 2e5, "throw_catch": 2e5, "native_typeerror": 2e5, "throw_across_calls": 1e5, "try_no_throw": 2e6,
      "finally_loop": 2e6, "empty_loop": 2e6, "method_call": 1e6, "gen_iter": 2e5, "async_await": 1e5, "fib": 1, "startup": 1}
P = {}
for l in open(os.path.join(O, "probes.txt")):
    n, v, c, t = l.split()[:4]; P[(n, v)] = (float(c), float(t))
print("\n| Probe | iters | AFTER cyc/it | BEFORE cyc/it | AFTER/BEFORE | Node cyc/it | AFTER ms | BEFORE ms |\n|---|---:|---:|---:|---:|---:|---:|---:|")
s = {v: P[("startup", v)][0] for v in ("before", "after", "node")}
print("startup cycles (whole process, empty script): before %.0f  after %.0f  node %.0f" % (s["before"], s["after"], s["node"]))
for n in it:
    if n == "startup": continue
    f = lambda v: (P[(n, v)][0] - s[v]) / it[n]
    print("| `%s` | %d | %s | %s | %.2fx | %s | %.0f | %.0f |" % (n, it[n], format(round(f("after")), ","), format(round(f("before")), ","), f("after") / f("before"),
          format(round(f("node")), ","), P[(n, "after")][1], P[(n, "before")][1]))
