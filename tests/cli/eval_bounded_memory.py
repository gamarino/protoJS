#!/usr/bin/env python3
"""CLI check: 100,000 distinct eval() sources in bounded memory; a repeated one hits the cache.

A long-running loop evaluates sources that are all different -- direct evals
inside a function and at the top level, and indirect evals -- keeps a closure
made by every thousandth one, calls the kept closures at the end, and then
times an eval of one repeated source against evals of distinct sources.

What failed
-----------
JSContextWrapper::loadEvalModule kept every module an eval compiled for the
life of the process, each pinned with its own ProtoRootSet handle, and cached
every (caller, scope, source) forever. Under a heap ceiling of 1,000,000 cells
the 2026-10-25 master ran out of memory before 80,000 distinct evals (the
resident set grew by about 1.6 KB per eval), and the run slowed down as the
root set grew (100,000 iterations had not finished after 560 s).

What this checks
----------------
Eval modules now live in a bounded cache and while a closure, a suspended
activation or a running eval needs them (src/runtime/EvalModuleCache.h).
Under the small heap ceiling the run must print the expected checksums, the
resident set may grow by at most GROWTH_LIMIT_MB between the first mark
(EVALS_FIRST_MARK evals) and the end (EVALS evals), and an eval of a repeated
source must cost less than half of an eval of a distinct one (measured: about
4 us against 27 us, so a cache that stopped hitting fails clearly). The child
is killed past KILL_LIMIT_MB so a regression cannot exhaust the machine.

Usage: eval_bounded_memory.py <path-to-protojs> <scratch-dir> [evals]
"""
import json
import os
import subprocess
import sys
import threading
import time

EVALS = 100_000
EVALS_FIRST_MARK = 20_000
HEAP_LIMIT_CELLS = "1000000"
GROWTH_LIMIT_MB = 20
KILL_LIMIT_MB = 1500
TIMEOUT_S = 900
TIMED = 20_000

SCRIPT = r"""
const TOTAL = Number(process.argv[2]);
const TIMED = %d;
const kept = [];
let sum = 0;
const marks = [];
function direct(i) { return eval("i * 2 + " + i); }
for (let i = 0; i < TOTAL; i++) {
  if (i %% 3 === 0) sum += direct(i);
  else if (i %% 3 === 1) sum += eval("(" + i + " + 1)");
  else sum += (0, eval)("(" + i + " - 1)");
  if (i %% 1000 === 0) kept.push(eval("(function (x) { return x + " + i + "; })"));
  if ((i + 1) %% 10000 === 0) marks.push([i + 1, process.memoryUsage().rss]);
}
let keptSum = 0;
for (const f of kept) keptSum += f(1);
function repeated(n) { let s = 0; for (let i = 0; i < n; i++) s += eval("i * 2 + 1"); return s; }
function distinct(n) { let s = 0; for (let i = 0; i < n; i++) s += eval("i * 2 + 1 + " + (TOTAL + i) + " * 0"); return s; }
let t = Date.now();
const r = repeated(TIMED);
const repeatedMs = Date.now() - t;
t = Date.now();
const d = distinct(TIMED);
const distinctMs = Date.now() - t;
console.log(JSON.stringify({ evals: TOTAL, sum, keptSum, kept: kept.length, marks,
                             r, d, repeatedMs, distinctMs }));
""" % TIMED


def expected(total):
    s = 0
    for i in range(total):
        if i % 3 == 0:
            s += 3 * i
        elif i % 3 == 1:
            s += i + 1
        else:
            s += i - 1
    kept = list(range(0, total, 1000))
    return s, sum(k + 1 for k in kept), len(kept)


def rss_mb(pid):
    try:
        with open("/proc/%d/status" % pid) as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024.0
    except OSError:
        pass
    return 0.0


def main():
    if len(sys.argv) not in (3, 4):
        print(__doc__.strip().splitlines()[-1])
        return 2
    protojs, scratch = sys.argv[1], sys.argv[2]
    evals = int(sys.argv[3]) if len(sys.argv) == 4 else EVALS
    os.makedirs(scratch, exist_ok=True)
    script = os.path.join(scratch, "eval_bounded_memory.js")
    with open(script, "w") as f:
        f.write(SCRIPT)
    env = dict(os.environ)
    env["PROTOCORE_HEAP_LIMIT_CELLS"] = HEAP_LIMIT_CELLS

    is_linux = sys.platform.startswith("linux")
    start = time.time()
    proc = subprocess.Popen([protojs, script, str(evals)], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, env=env)
    chunks = {"out": [], "err": []}

    def drain(stream, key):
        chunks[key].append(stream.read())

    threads = [threading.Thread(target=drain, args=(proc.stdout, "out")),
               threading.Thread(target=drain, args=(proc.stderr, "err"))]
    for t in threads:
        t.start()
    killed = None
    sampled = 0.0
    while proc.poll() is None:
        if is_linux:
            cur = rss_mb(proc.pid)
            sampled = max(sampled, cur)
            if cur > KILL_LIMIT_MB:
                killed = "resident set %.0f MB exceeded the %d MB safety cap" % (cur, KILL_LIMIT_MB)
                proc.kill()
                break
        if time.time() - start > TIMEOUT_S:
            killed = "timed out after %d s" % TIMEOUT_S
            proc.kill()
            break
        time.sleep(0.05)
    proc.wait()
    for t in threads:
        t.join()
    out = b"".join(chunks["out"]).decode(errors="replace").strip()
    err = b"".join(chunks["err"]).decode(errors="replace").strip()
    elapsed = time.time() - start
    if killed:
        print("FAIL: " + killed)
        return 1
    result = None
    for line in out.splitlines():
        if line.startswith("{"):
            result = json.loads(line)
    if proc.returncode != 0 or result is None:
        print("FAIL: exit %s\nstdout: %s\nstderr: %s" % (proc.returncode, out[-2000:], err[-2000:]))
        return 1
    want_sum, want_kept_sum, want_kept = expected(evals)
    want_timed = TIMED * (TIMED - 1) + TIMED
    if (result["evals"] != evals or result["sum"] != want_sum
            or result["keptSum"] != want_kept_sum or result["kept"] != want_kept
            or result["r"] != want_timed or result["d"] != want_timed):
        print("FAIL: computed %s; expected sum %d, keptSum %d, kept %d, timed sums %d"
              % (json.dumps({k: result[k] for k in ("evals", "sum", "keptSum", "kept", "r", "d")}),
                 want_sum, want_kept_sum, want_kept, want_timed))
        return 1
    marks = {m[0]: m[1] / 1048576.0 for m in result["marks"]}
    first = marks.get(EVALS_FIRST_MARK)
    last = marks.get(evals)
    if first is None or last is None:
        print("FAIL: missing resident-set marks: %s" % sorted(marks))
        return 1
    growth = last - first
    print("exit 0, %.1f s, %d distinct evals, RSS %.0f MB at %d, %.0f MB at %d (growth %.1f MB), "
          "sampled peak %.0f MB, heap limit %s cells; %d evals of one source %d ms, "
          "%d of distinct sources %d ms"
          % (elapsed, evals, first, EVALS_FIRST_MARK, last, evals, growth, sampled,
             HEAP_LIMIT_CELLS, TIMED, result["repeatedMs"], TIMED, result["distinctMs"]))
    if growth > GROWTH_LIMIT_MB:
        print("FAIL: the resident set grew by %.1f MB over %d further distinct evals (limit %d MB)"
              % (growth, evals - EVALS_FIRST_MARK, GROWTH_LIMIT_MB))
        return 1
    if result["repeatedMs"] * 2 > result["distinctMs"]:
        print("FAIL: a repeated source is not cheaper than distinct ones: the eval cache does not hit")
        return 1
    print("eval-bounded-memory: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
