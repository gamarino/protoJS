#!/usr/bin/env python3
"""CLI check: 100,000 rejected-and-handled promises run in bounded memory.

What failed
-----------
Every `new Promise(executor)` registered the promise on the global object under
a fresh key (`__promise_cell_N__`) so that its resolve and reject functions could
find it, and nothing ever removed the key: each promise ever constructed stayed
reachable from the global for the rest of the process. A program that creates
and settles promises in a loop grew without bound.

Promises now keep their own state; resolve and reject reach the promise through
their own record, the unhandled-rejection list is cleared at the end of every
microtask checkpoint, and a settled promise is referenced by nothing the
runtime holds.

What this checks
----------------
The script creates ITERATIONS promises in batches of BATCH per macrotask; each
is rejected, and handled -- half by the constructor's reject function with a
catch attached later in the same turn, half through Promise.reject and an
async function's try/catch around an await. It prints how many rejections were
handled, and the run must finish with status 0, that exact output, and a peak
resident set below PEAK_LIMIT_MB under a heap ceiling of HEAP_LIMIT_CELLS
cells: the collector must reclaim the settled promises, because the ceiling is
far below what 100,000 retained promises need.

The child is killed as soon as its resident set exceeds KILL_LIMIT_MB, so a
regression fails this test instead of exhausting the machine.

Usage: promise_rejections_bounded_memory.py <path-to-protojs> <scratch-dir>
"""
import os
import subprocess
import sys
import threading
import time

ITERATIONS = 100_000
BATCH = 1_000
HEAP_LIMIT_CELLS = "1500000"   # 96 MB of cells
PEAK_LIMIT_MB = 512
KILL_LIMIT_MB = 1536
TIMEOUT_S = 900
EXPECTED = "promise-rejections: %d handled" % ITERATIONS

SCRIPT = """
var handled = 0;
async function viaAwait(i) {
    try { await Promise.reject(new Error('a' + i)); }
    catch (e) { handled++; }
}
function batch(start) {
    for (var i = start; i < start + %d; i++) {
        if (i %% 2 === 0) {
            var p = new Promise(function (resolve, reject) { reject(new Error('c' + i)); });
            p.catch(function () { handled++; });
        } else {
            viaAwait(i);
        }
    }
    if (start + %d < %d) setImmediate(function () { batch(start + %d); });
    else setImmediate(function () { console.log('promise-rejections: ' + handled + ' handled'); });
}
batch(0);
""" % (BATCH, BATCH, ITERATIONS, BATCH)


def rss_mb(pid):
    if sys.platform.startswith("linux"):
        try:
            with open("/proc/%d/status" % pid) as f:
                for line in f:
                    if line.startswith("VmRSS:"):
                        return int(line.split()[1]) / 1024.0
        except OSError:
            return 0.0
        return 0.0
    try:
        out = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)],
                             capture_output=True, text=True).stdout.strip()
        return int(out) / 1024.0 if out else 0.0
    except (OSError, ValueError):
        return 0.0


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1])
        return 2
    protojs, scratch = sys.argv[1], sys.argv[2]
    os.makedirs(scratch, exist_ok=True)
    script = os.path.join(scratch, "promise_rejections.js")
    with open(script, "w") as f:
        f.write(SCRIPT)

    env = dict(os.environ)
    env["PROTOCORE_HEAP_LIMIT_CELLS"] = HEAP_LIMIT_CELLS
    start = time.time()
    proc = subprocess.Popen([protojs, script], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, env=env)
    chunks = {"out": [], "err": []}

    def drain(stream, key):
        chunks[key].append(stream.read())

    threads = [threading.Thread(target=drain, args=(proc.stdout, "out")),
               threading.Thread(target=drain, args=(proc.stderr, "err"))]
    for t in threads:
        t.start()
    sampled_peak = 0.0
    killed_for = None
    while proc.poll() is None:
        cur = rss_mb(proc.pid)
        sampled_peak = max(sampled_peak, cur)
        if cur > KILL_LIMIT_MB:
            killed_for = "resident set %.0f MB exceeded the %d MB safety cap" % (
                cur, KILL_LIMIT_MB)
            proc.kill()
            break
        if time.time() - start > TIMEOUT_S:
            killed_for = "no result after %d s" % TIMEOUT_S
            proc.kill()
            break
        time.sleep(0.05)
    status = proc.wait()
    for t in threads:
        t.join()
    elapsed = time.time() - start
    out = b"".join(chunks["out"]).decode("utf-8", "replace").strip()
    err = b"".join(chunks["err"]).decode("utf-8", "replace").strip()

    peak = sampled_peak
    if not sys.platform.startswith("win"):
        import resource
        ru = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        peak = max(peak, ru / 1048576.0 if sys.platform == "darwin" else ru / 1024.0)

    print("promise rejections: %d promises, status %d, %.1f s, peak resident set %.0f MB"
          % (ITERATIONS, status, elapsed, peak))
    failed = False
    if killed_for:
        print("FAIL: killed: " + killed_for)
        failed = True
    elif status != 0:
        print("FAIL: exit status %d" % status)
        failed = True
    elif out != EXPECTED:
        print("FAIL: standard output was %r, expected %r" % (out, EXPECTED))
        failed = True
    elif peak >= PEAK_LIMIT_MB:
        print("FAIL: peak resident set %.0f MB, limit %d MB" % (peak, PEAK_LIMIT_MB))
        failed = True
    if failed:
        if err:
            print("standard error:\n" + err[-2000:])
        return 1
    print("PASS: %d rejected-and-handled promises in bounded memory" % ITERATIONS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
