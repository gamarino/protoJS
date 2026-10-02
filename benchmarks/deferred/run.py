#!/usr/bin/env python3
"""Run the Deferred benchmarks and print Markdown tables of the medians.

Every benchmark script prints one JSON line with what it computed and whether
that is right ("ok"). A run that prints no such line, prints ok:false or exits
non-zero is a failure, and the runner stops: a crash must not look like a fast
run. Peak memory is the maximum resident set size reported by /usr/bin/time.

Usage:
  run.py --protojs PATH [--protojs-master PATH] [--node PATH]
         [--reps 5] [--only cpu|shared|tasks] [--out results.jsonl]

The runs are sequential (one process at a time), interleaved by repetition so
that slow drift of the machine (temperature, frequency) spreads over every
configuration instead of landing on one.
"""
import argparse
import json
import os
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NS = [1, 2, 4, 6, 8, 12]


def run_one(cmd, env_extra, timeout=900):
    env = dict(os.environ)
    env.update({k: str(v) for k, v in env_extra.items()})
    full = ["/usr/bin/time", "-f", "RSS_KB=%M"] + cmd
    p = subprocess.run(full, env=env, capture_output=True, text=True, timeout=timeout)
    rss = None
    for line in p.stderr.splitlines():
        if line.startswith("RSS_KB="):
            rss = int(line.split("=", 1)[1])
    rec = None
    for line in p.stdout.splitlines():
        line = line.strip()
        if line.startswith("{") and '"bench"' in line:
            rec = json.loads(line)
    if p.returncode != 0 or rec is None or rec.get("ok") is not True:
        sys.stderr.write("FAILED RUN: %s %s\nexit=%d\nstdout:\n%s\nstderr:\n%s\n"
                         % (env_extra, " ".join(cmd), p.returncode, p.stdout[-2000:], p.stderr[-2000:]))
        sys.exit(1)
    rec["rss_kb"] = rss
    return rec


def configs(args):
    out = []
    pj, pm, node = args.protojs, args.protojs_master, args.node
    if args.only in (None, "cpu"):
        lim = {"LIMIT": args.limit}
        for n in [0] + NS:
            out.append(("cpu", "protojs", n, [pj, os.path.join(HERE, "cpu_primes.js")], dict(lim, N=n)))
            out.append(("cpu", "node", n, [node, os.path.join(HERE, "cpu_primes_node.js")], dict(lim, N=n)))
        if pm:
            for n in [0, 1, 4, 12]:
                out.append(("cpu", "protojs-master", n, [pm, os.path.join(HERE, "cpu_primes.js")], dict(lim, N=n)))
    if args.only in (None, "shared"):
        sz = {"SIZE": args.size, "PROTOCORE_HEAP_LIMIT_CELLS": args.heap_cells}
        for n in [0] + NS:
            out.append(("shared", "protojs", n, [pj, os.path.join(HERE, "shared_read.js")], dict(sz, N=n)))
        out.append(("shared", "node-seq", 0, [node, os.path.join(HERE, "shared_read_node.js")], dict(sz, N=0)))
        for n in NS:
            out.append(("shared", "node-clone", n, [node, os.path.join(HERE, "shared_read_node.js")],
                        dict(sz, N=n, VARIANT="clone")))
            out.append(("shared", "node-sab", n, [node, os.path.join(HERE, "shared_read_node.js")],
                        dict(sz, N=n, VARIANT="sab")))
        if pm:
            for n in [0, 4]:
                out.append(("shared", "protojs-master", n, [pm, os.path.join(HERE, "shared_read.js")], dict(sz, N=n)))
    if args.only in (None, "tasks"):
        mk = {"M": args.tasks, "K": 200}
        for v in ["deferred", "promise", "sync"]:
            out.append(("tasks", "protojs-" + v, 0, [pj, os.path.join(HERE, "small_tasks.js")], dict(mk, VARIANT=v)))
            if pm:
                out.append(("tasks", "protojs-master-" + v, 0, [pm, os.path.join(HERE, "small_tasks.js")],
                            dict(mk, VARIANT=v)))
        for v in ["pool", "promise", "sync"]:
            out.append(("tasks", "node-" + v, 0, [node, os.path.join(HERE, "small_tasks_node.js")], dict(mk, VARIANT=v)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--protojs", required=True)
    ap.add_argument("--protojs-master")
    ap.add_argument("--node", default="node")
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--only", choices=["cpu", "shared", "tasks"])
    ap.add_argument("--limit", type=int, default=300000)
    ap.add_argument("--size", type=int, default=500000)
    ap.add_argument("--heap-cells", type=int, default=40000000)
    ap.add_argument("--tasks", type=int, default=2000)
    ap.add_argument("--out", default=os.path.join(HERE, "results.jsonl"))
    args = ap.parse_args()

    cfgs = configs(args)
    results = {}
    with open(args.out, "w") as f:
        for rep in range(args.reps):
            for (bench, runtime, n, cmd, env) in cfgs:
                rec = run_one(cmd, env)
                rec.update({"group": bench, "label": runtime, "rep": rep})
                f.write(json.dumps(rec) + "\n")
                f.flush()
                results.setdefault((bench, runtime, n), []).append(rec)
                sys.stderr.write("rep %d %s %s n=%s ms=%s rss=%sMB\n"
                                 % (rep, bench, runtime, n, rec["ms"], (rec["rss_kb"] or 0) // 1024))

    def med(key, field):
        vals = [r[field] for r in results[key] if r.get(field) is not None]
        return statistics.median(vals) if vals else None

    print("| group | runtime | N | median ms | min ms | max ms | peak RSS MB (median) |")
    print("|---|---|---:|---:|---:|---:|---:|")
    for key in sorted(results, key=lambda k: (k[0], k[1], k[2])):
        ms = [r["ms"] for r in results[key]]
        rss = med(key, "rss_kb")
        print("| %s | %s | %d | %.0f | %d | %d | %s |" % (key[0], key[1], key[2], statistics.median(ms),
              min(ms), max(ms), "%.0f" % (rss / 1024) if rss else "-"))


if __name__ == "__main__":
    main()
