#!/usr/bin/env python3
"""CLI check: a loop that creates 2,000,000 closures runs in bounded memory.

What failed
-----------
protoCore's collector starts a cycle only as the heap approaches a configured
ceiling, and protoJS configured none, so nothing a program discarded was ever
reclaimed: each closure cost about 20 KB that stayed allocated until exit, and a
1,000,000-iteration closure loop exhausted the machine's memory. protoJS now
sets protoST's default ceiling (10M cells = 640 MB, or a quarter of physical
memory if smaller; PROTOCORE_HEAP_LIMIT_CELLS overrides it).

A ceiling alone was not enough: every closure carried fn.prototype, whose
`constructor` points back at the closure, and protoCore never collects a cycle
among mutable objects (protoCore docs/MemoryModel.md section 7). Arrow
functions have no `prototype` (ECMA-262 15.3), and no longer get one. An
ordinary `function` has one, but it is now created only when something first
needs it (src/runtime/LazyPrototype.h), as QuickJS does, so a function that
never touches `prototype` forms no cycle either.

The loop creates a closure per iteration that captures the loop's own local --
the shape of a callback created per iteration: an arrow function by default,
an ordinary `function` expression with the `function` argument.

What this checks
----------------
The script prints the sum it computed, and the run must finish with status 0,
that exact output, and a peak resident set below PEAK_LIMIT_MB -- with the
default policy, so PROTOCORE_HEAP_LIMIT_CELLS is removed from the environment.

The run is also capped: the child is killed as soon as its resident set exceeds
KILL_LIMIT_MB, so a regression fails this test instead of exhausting the
machine. Peak memory is the kernel's figure where there is one (getrusage on
Linux and macOS, PeakWorkingSetSize on Windows), combined with the samples
taken while polling.

Usage: closure_loop_bounded_memory.py <path-to-protojs> <scratch-dir> [arrow|function]
"""
import os
import subprocess
import sys
import time

ITERATIONS = 2_000_000
KINDS = {"arrow": "(x) => x + i",
         "function": "function (x) { return x + i; }"}
EXPECTED = "closure-loop: %d closures, sum %d" % (
    ITERATIONS, ITERATIONS + ITERATIONS * (ITERATIONS - 1) // 2)
PEAK_LIMIT_MB = 1024     # the default ceiling is 640 MB of cells
KILL_LIMIT_MB = 2048     # safety cap for this machine, not the verdict
TIMEOUT_S = 900

SCRIPT = """
function run(n) {
    var sum = 0;
    for (var i = 0; i < n; i++) {
        var f = %s;
        sum += f(1);
    }
    return sum;
}
console.log('closure-loop: ' + %d + ' closures, sum ' + run(%d));
"""


def rss_mb_linux(pid):
    try:
        with open("/proc/%d/status" % pid) as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024.0
    except OSError:
        pass
    return 0.0


def rss_mb_ps(pid):
    try:
        out = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)],
                             capture_output=True, text=True).stdout.strip()
        return int(out) / 1024.0 if out else 0.0
    except (OSError, ValueError):
        return 0.0


def windows_memory_mb(handle):
    """(current, peak) working set of the process behind `handle`, in MB."""
    import ctypes
    from ctypes import wintypes

    class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD),
                    ("PageFaultCount", wintypes.DWORD),
                    ("PeakWorkingSetSize", ctypes.c_size_t),
                    ("WorkingSetSize", ctypes.c_size_t),
                    ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                    ("PagefileUsage", ctypes.c_size_t),
                    ("PeakPagefileUsage", ctypes.c_size_t)]

    counters = PROCESS_MEMORY_COUNTERS()
    counters.cb = ctypes.sizeof(counters)
    get_info = ctypes.windll.psapi.GetProcessMemoryInfo
    get_info.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESS_MEMORY_COUNTERS),
                         wintypes.DWORD]
    if not get_info(wintypes.HANDLE(int(handle)), ctypes.byref(counters),
                    counters.cb):
        return 0.0, 0.0
    return (counters.WorkingSetSize / 1048576.0,
            counters.PeakWorkingSetSize / 1048576.0)


def main():
    if len(sys.argv) not in (3, 4) or (len(sys.argv) == 4 and sys.argv[3] not in KINDS):
        print(__doc__.strip().splitlines()[-1])
        return 2
    protojs, scratch = sys.argv[1], sys.argv[2]
    kind = sys.argv[3] if len(sys.argv) == 4 else "arrow"
    os.makedirs(scratch, exist_ok=True)
    script = os.path.join(scratch, "closure_loop_%s.js" % kind)
    with open(script, "w") as f:
        f.write(SCRIPT % (KINDS[kind], ITERATIONS, ITERATIONS))

    env = dict(os.environ)
    env.pop("PROTOCORE_HEAP_LIMIT_CELLS", None)   # the default policy is under test

    is_windows = sys.platform.startswith("win")
    is_linux = sys.platform.startswith("linux")
    start = time.time()
    proc = subprocess.Popen([protojs, script], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, env=env)
    sampled_peak = 0.0
    killed_for = None
    # Drain the pipes on threads so a chatty child cannot block on a full pipe.
    import threading
    chunks = {"out": [], "err": []}

    def drain(stream, key):
        chunks[key].append(stream.read())

    threads = [threading.Thread(target=drain, args=(proc.stdout, "out")),
               threading.Thread(target=drain, args=(proc.stderr, "err"))]
    for t in threads:
        t.start()
    win_peak = 0.0
    while proc.poll() is None:
        if is_windows:
            cur, win_peak = windows_memory_mb(proc._handle)
        elif is_linux:
            cur = rss_mb_linux(proc.pid)
        else:
            cur = rss_mb_ps(proc.pid)
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

    kernel_peak = 0.0
    if is_windows:
        kernel_peak = max(win_peak, windows_memory_mb(proc._handle)[1])
    else:
        import resource
        ru = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        kernel_peak = ru / 1048576.0 if sys.platform == "darwin" else ru / 1024.0
    peak = max(sampled_peak, kernel_peak)

    print("closure loop (%s): %d iterations, status %d, %.1f s, peak resident set %.0f MB"
          % (kind, ITERATIONS, status, elapsed, peak))
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
            print("standard error:\n" + err)
        return 1
    print("PASS: %d %s closures in bounded memory" % (ITERATIONS, kind))
    return 0


if __name__ == "__main__":
    sys.exit(main())
