#!/usr/bin/env python3
"""Run the structure benchmarks on protoJS and Node.js, verify, tabulate.

For every workload and every N in the curve, each runtime runs:
  seq  -- tasks 0..N-1 one after another on the main thread;
  par  -- the same N tasks at once: protoJS with N Deferreds reading the
          shared data in place, Node.js with N worker_threads that each
          receive the data by postMessage (a structured clone).
Each configuration is a separate process that builds the data once and
repeats the timed phase REPS times; the median is reported.

Every run must print one JSON line with "ok": true, and the checksum of
task k must be the same in every runtime and mode (the reference is Node's
sequential run). A crash, a missing line, ok:false or a different checksum
stops the runner: a failure must never look like a fast run.

protoJS runs with an explicit heap ceiling (--heap-cells), under a systemd
memory cap (--memory-max), and reports its collector's cycle count.

Usage:
  run.py --protojs PATH [--node node] [--reps 3] [--ns 1,2,4,6,12]
         [--scale 1] [--heap-cells 40000000] [--memory-max 4G]
         [--only records,join,...] [--out results.jsonl]
"""
import argparse
import json
import os
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WORKLOADS = ["records", "join", "doctree", "wordfreq", "graph"]


def run(cmd, env_extra, memory_max, timeout=1800, may_fail=False):
    """Run one configuration.  A failure stops the runner, except where
    `may_fail` (Node.js's worker variant, whose structured clone can fail on
    deep object graphs): then a record with "completed": false is returned."""
    env = dict(os.environ)
    env.update({k: str(v) for k, v in env_extra.items()})
    full = cmd
    if memory_max:
        full = ["systemd-run", "--user", "--scope", "-q", "-p", "MemoryMax=" + memory_max] + cmd
    p = subprocess.run(full, env=env, capture_output=True, text=True, timeout=timeout, cwd=HERE)
    rec = None
    for line in p.stdout.splitlines():
        line = line.strip()
        if line.startswith("{") and '"bench"' in line:
            rec = json.loads(line)
    if p.returncode != 0 or rec is None or not rec.get("ok"):
        if may_fail and rec is None:
            err = (p.stdout + p.stderr).strip().splitlines()
            return {"bench": "structures", "completed": False, "exit": p.returncode,
                    "error": " | ".join(err[:2])[:300], **{k.lower(): v for k, v in env_extra.items()}}
        sys.stderr.write("FAILED: %s %s\nexit %s\nstdout: %s\nstderr: %s\n"
                         % (" ".join(cmd), env_extra, p.returncode, p.stdout[-2000:], p.stderr[-2000:]))
        sys.exit(1)
    rec["completed"] = True
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--protojs", required=True)
    ap.add_argument("--node", default="node")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--ns", default="1,2,4,6,12")
    ap.add_argument("--scale", type=int, default=1)
    ap.add_argument("--heap-cells", type=int, default=40000000)
    ap.add_argument("--memory-max", default="4G")
    ap.add_argument("--only", default=",".join(WORKLOADS))
    ap.add_argument("--out", default=os.path.join(HERE, "results.jsonl"))
    args = ap.parse_args()
    ns = [int(x) for x in args.ns.split(",")]
    nmax = max(ns)
    out = open(args.out, "a")

    for wl in args.only.split(","):
        base = {"WORKLOAD": wl, "SCALE": args.scale, "REPS": args.reps}
        ref = run([args.node, "node_bench.js"], dict(base, MODE="seq", N=nmax), None)
        ref_cs = ref["checksums"]
        out.write(json.dumps(ref) + "\n")
        recs = {}
        for n in ns:
            for runtime, cmd, extra, mem in (
                    ("node", [args.node, "node_bench.js"], {}, None),
                    ("protojs", [args.protojs, "protojs_bench.js"],
                     {"PROTOCORE_HEAP_LIMIT_CELLS": args.heap_cells}, args.memory_max)):
                for mode in ("seq", "par"):
                    r = run(cmd, dict(base, MODE=mode, N=n, **extra), mem,
                            may_fail=(runtime == "node" and mode == "par"))
                    if not r["completed"]:
                        r["runtime"] = runtime
                        out.write(json.dumps(r) + "\n")
                        recs[(runtime, mode, n)] = r
                        print("%-8s %-7s %-3s N=%-2d did NOT complete: %s" % (wl, runtime, mode, n, r["error"]),
                              flush=True)
                        continue
                    if r["checksums"] != ref_cs[:n]:
                        sys.stderr.write("FAILED: checksum mismatch %s %s N=%d: %s vs %s\n"
                                         % (runtime, mode, n, r["checksums"], ref_cs[:n]))
                        sys.exit(1)
                    out.write(json.dumps(r) + "\n")
                    out.flush()
                    recs[(runtime, mode, n)] = r
                    print("%-8s %-7s %-3s N=%-2d median %7d ms  build %5d ms  rss %5d MB%s" % (
                        wl, runtime, mode, n, r["median_ms"], r["build_ms"], r["peak_rss_kb"] // 1024,
                        ("  gc %d" % r["gc_cycles"]) if "gc_cycles" in r else
                        ("  post %s clone %s compute %s" % (r.get("post_ms"), r.get("clone_ms"), r.get("compute_ms"))
                         if mode == "par" else "")), flush=True)

    print("\nTables: python3 tabulate.py " + args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
