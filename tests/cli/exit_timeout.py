#!/usr/bin/env python3
"""CLI check: protojs waits for pending work without a time limit by default,
and PROTOJS_EXIT_TIMEOUT_SECONDS sets one.

The script starts a Deferred that computes for about three seconds and
prints its result when it settles. By default the result must be printed
(protojs used to stop waiting after a fixed 180 seconds, which ended long
computations and listening servers). With PROTOJS_EXIT_TIMEOUT_SECONDS=1 the
loop stops early: the warning is printed and the result is not.

Usage: exit_timeout.py <path-to-protojs> <scratch-dir>
"""
import os
import subprocess
import sys

SCRIPT = """
const start = Date.now();
new Deferred(() => {
    let n = 0;
    while (Date.now() - start < 3000) n++;
    return n > 0;
}).then((ok) => console.log("deferred settled: " + ok));
"""


def run(protojs, script, extra):
    env = dict(os.environ)
    env.pop("PROTOJS_EXIT_TIMEOUT_SECONDS", None)
    env.update(extra)
    return subprocess.run([protojs, script], capture_output=True, text=True, env=env, timeout=120)


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1])
        return 2
    protojs, scratch = sys.argv[1], sys.argv[2]
    os.makedirs(scratch, exist_ok=True)
    script = os.path.join(scratch, "exit_timeout.js")
    with open(script, "w") as f:
        f.write(SCRIPT)
    p = run(protojs, script, {})
    if p.returncode != 0 or "deferred settled: true" not in p.stdout or "timeout" in p.stderr:
        print("FAIL (default): exit %d\nstdout: %s\nstderr: %s" % (p.returncode, p.stdout, p.stderr))
        return 1
    p = run(protojs, script, {"PROTOJS_EXIT_TIMEOUT_SECONDS": "1"})
    if "Event loop timeout reached" not in p.stderr or "deferred settled" in p.stdout:
        print("FAIL (limit 1 s): exit %d\nstdout: %s\nstderr: %s" % (p.returncode, p.stdout, p.stderr))
        return 1
    print("exit-timeout: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
