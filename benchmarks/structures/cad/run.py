#!/usr/bin/env python3
"""Run the CAD benchmark (see ../README.md) and verify the results.

Variants, each a separate process under a systemd memory cap:
  protojs        model built once, N Deferreds share it (protojs_cad.js);
  node-default   object model on the main thread, Node's default heap;
  node-big-heap  the same with --max-old-space-size=<--node-heap-mb>;
  node-workers   N worker_threads, the model posted (cloned) to each,
                 one process per N (node_cad.js VARIANT=workers);
  node-sab       the model encoded in SharedArrayBuffers (node_cad_sab.js).
A variant that exits non-zero, is killed by the cap or prints no JSON line
is recorded as not completing (with its exit status); a variant that
completes must report ok:true and the same per-task checksums as every other
variant, or the runner stops.

Usage: run.py --protojs PATH --parts N [--ns 1,2,4,6,12] [--reps 3]
              [--protojs-heap-cells C] [--protojs-mem 14G] [--node-mem 16G]
              [--node-heap-mb 16384] [--out cad.jsonl]
"""
import argparse
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def run(name, cmd, env_extra, mem, timeout=7200):
    env = dict(os.environ)
    env.update({k: str(v) for k, v in env_extra.items()})
    full = ["systemd-run", "--user", "--scope", "-q", "-p", "MemoryMax=" + mem, "-p", "MemorySwapMax=0"] + cmd
    t0 = time.time()
    try:
        # stderr carries progress lines; it is kept for the failure report.
        p = subprocess.run(full, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           text=True, timeout=timeout, cwd=HERE)
        rc, out, err = p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        rc, out, err = "timeout", "", ""
    wall = time.time() - t0
    rec = None
    for line in out.splitlines():
        if line.startswith("{") and '"bench"' in line:
            rec = json.loads(line)
    if rec is None:
        tail = (err or out).strip().splitlines()[-3:]
        return {"variant": name, "completed": False, "exit": rc, "wall_s": round(wall, 1),
                "memory_max": mem, "error": " | ".join(tail)[:500]}
    rec.update({"variant": name, "completed": True, "exit": rc, "wall_s": round(wall, 1), "memory_max": mem})
    return rec


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--protojs", required=True)
    ap.add_argument("--node", default="node")
    ap.add_argument("--parts", type=int, required=True)
    ap.add_argument("--ns", default="1,2,4,6,12")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--protojs-heap-cells", type=int, default=0)
    ap.add_argument("--protojs-mem", default="14G")
    ap.add_argument("--node-mem", default="16G")
    ap.add_argument("--node-heap-mb", type=int, default=16384)
    ap.add_argument("--only", default="protojs,node-default,node-big-heap,node-workers,node-sab")
    ap.add_argument("--out", default=os.path.join(HERE, "cad.jsonl"))
    args = ap.parse_args()
    ns = [int(x) for x in args.ns.split(",")]
    only = args.only.split(",")
    base = {"PARTS": args.parts, "NS": args.ns, "REPS": args.reps}
    out = open(args.out, "a")
    recs = []

    def record(r):
        out.write(json.dumps(r) + "\n")
        out.flush()
        recs.append(r)
        if r["completed"]:
            res = " ".join("N=%d:%dms" % (x["n"], x["median_ms"]) for x in r.get("results", []))
            print("%-14s ok=%s build %s ms, seq per task %s ms, peak RSS %d MB, %s%s" % (
                r["variant"], r["ok"], r["build_ms"], r.get("seq_task_ms"),
                r["peak_rss_kb"] // 1024, res,
                (" gc %s" % r["gc_cycles"]) if "gc_cycles" in r else ""), flush=True)
        else:
            print("%-14s did NOT complete: exit %s after %s s (cap %s): %s" % (
                r["variant"], r["exit"], r["wall_s"], r["memory_max"], r["error"]), flush=True)

    if "protojs" in only:
        extra = dict(base)
        if args.protojs_heap_cells:
            extra["PROTOCORE_HEAP_LIMIT_CELLS"] = args.protojs_heap_cells
        record(run("protojs", [args.protojs, "protojs_cad.js"], extra, args.protojs_mem))
    if "node-default" in only:
        record(run("node-default", [args.node, "node_cad.js"], dict(base, VARIANT="main"), args.node_mem))
    if "node-big-heap" in only:
        record(run("node-big-heap", [args.node, "--max-old-space-size=%d" % args.node_heap_mb, "node_cad.js"],
                   dict(base, VARIANT="main"), args.node_mem))
    if "node-workers" in only:
        for n in ns:
            record(run("node-workers", [args.node, "--max-old-space-size=%d" % args.node_heap_mb, "node_cad.js"],
                       dict(base, VARIANT="workers", NS=str(n)), args.node_mem))
    if "node-sab" in only:
        record(run("node-sab", [args.node, "--max-old-space-size=%d" % args.node_heap_mb, "node_cad_sab.js"],
                   dict(base), args.node_mem))

    # Every completed variant must agree on every task's checksum.
    ref = None
    for r in recs:
        if not r["completed"]:
            continue
        if not r.get("ok"):
            sys.stderr.write("FAILED: %s reported ok:false\n" % r["variant"])
            return 1
        cs = r["checksums"]
        if ref is None:
            ref = cs
        elif cs != ref[:len(cs)] and ref != cs[:len(ref)]:
            sys.stderr.write("FAILED: checksums differ: %s %s vs %s\n" % (r["variant"], cs, ref))
            return 1
    print("checksums agree across completed variants: %s" % (ref is not None))
    return 0


if __name__ == "__main__":
    sys.exit(main())
