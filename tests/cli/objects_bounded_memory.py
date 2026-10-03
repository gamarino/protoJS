#!/usr/bin/env python3
"""CLI check: 200,000 live objects fit a small heap ceiling and resident set.

Each object is a five-field literal with a string built per object and a
double: {id, name: "n" + (i % 100), x: i * 0.5, y: i, tag: "t"}, kept in an
array. The script verifies an aggregate over all of them and prints it.

What failed
-----------
Building such an object published a new snapshot of it into protoCore's
mutable table for every field, and integral numbers from built-ins were boxed
doubles. That garbage is reclaimed only when the heap nears its ceiling, so
the smallest ceiling under which this script completed was 12.5 cells per
object (2.5 million cells); it is 9.0 now that object literals are built
immutable and published once
(benchmarks/reports/2026-10-02-memory-per-object.md).

What this checks
----------------
Under PROTOCORE_HEAP_LIMIT_CELLS = HEAP_LIMIT_CELLS (11 cells per object;
the previous build ran out of memory there, exit 3) the run must exit 0 with the expected output, and its peak resident set must stay
below PEAK_LIMIT_MB (about 2 KB per object, the runtime itself included). The
child is killed past KILL_LIMIT_MB so a regression cannot exhaust the machine.

Usage: objects_bounded_memory.py <path-to-protojs> <scratch-dir>
"""
import os
import subprocess
import sys
import time

N = 200_000
HEAP_LIMIT_CELLS = str(N * 11)
PEAK_LIMIT_MB = 400
KILL_LIMIT_MB = 1500
TIMEOUT_S = 600

SCRIPT = """
const N = %d;
const keep = [];
for (let i = 0; i < N; i++) keep.push({ id: i, name: "n" + (i %% 100), x: i * 0.5, y: i, tag: "t" });
let sumId = 0, sumX = 0, names = 0;
for (const o of keep) { sumId += o.id + o.y; sumX += o.x; if (o.name === "n7") names++; }
console.log("objects " + keep.length + " sumId " + sumId + " sumX " + sumX + " n7 " + names);
""" % N

EXPECTED = "objects %d sumId %d sumX %s n7 %d" % (
    N, N * (N - 1), format(N * (N - 1) / 4, ".0f"), N // 100)


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
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1])
        return 2
    protojs, scratch = sys.argv[1], sys.argv[2]
    os.makedirs(scratch, exist_ok=True)
    script = os.path.join(scratch, "objects_bounded_memory.js")
    with open(script, "w") as f:
        f.write(SCRIPT)
    env = dict(os.environ)
    env["PROTOCORE_HEAP_LIMIT_CELLS"] = HEAP_LIMIT_CELLS

    is_linux = sys.platform.startswith("linux")
    start = time.time()
    proc = subprocess.Popen([protojs, script], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, env=env)
    sampled = 0.0
    killed = None
    import threading
    chunks = {"out": [], "err": []}

    def drain(stream, key):
        chunks[key].append(stream.read())

    threads = [threading.Thread(target=drain, args=(proc.stdout, "out")),
               threading.Thread(target=drain, args=(proc.stderr, "err"))]
    for t in threads:
        t.start()
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
    peak = sampled
    try:
        import resource
        peak = max(peak, resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
                   / (1048576.0 if sys.platform == "darwin" else 1024.0))
    except ImportError:
        pass
    print("exit %s, %.1f s, peak RSS %.0f MB (%.0f bytes per object), heap limit %s cells"
          % (proc.returncode, time.time() - start, peak, peak * 1048576 / N, HEAP_LIMIT_CELLS))
    if killed:
        print("FAIL: " + killed)
        return 1
    if proc.returncode != 0 or out.splitlines()[-1:] != [EXPECTED]:
        print("FAIL: expected exit 0 and %r\nstdout: %s\nstderr: %s" % (EXPECTED, out, err[-2000:]))
        return 1
    if peak > PEAK_LIMIT_MB:
        print("FAIL: peak resident set %.0f MB above %d MB" % (peak, PEAK_LIMIT_MB))
        return 1
    print("objects-bounded-memory: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
