#!/usr/bin/env python3
"""CLI check: 500,000 distinct computed property keys in bounded memory.

A long-running loop makes objects keyed by data -- `m['session-' + id + '-key']
= v` and `JSON.parse` of objects whose keys and string values are built at run
time -- reads them back (for-in, Object.keys, element reads) and drops them.
Every key is distinct, longer than six bytes (shorter ASCII strings are inline
values, not cells) and never named in the program text.

What failed
-----------
protoCore interns every heap-string attribute key (and JSON.parse interned
every string value as well) in its process-wide SymbolTable, whose entries are
never collected. The resident set therefore grew with the number of distinct
keys the program had ever used, about 480 bytes per key here, whatever the heap
ceiling (docs/MEMORY_SAFETY_PROPOSALS.md, section 2).

What this checks
----------------
Objects now keep keys that are not program vocabulary in a dictionary of
collectable strings (src/runtime/PropertyDictionary.h), and JSON.parse no
longer interns string values. Under a small heap ceiling the run must print the
expected checksum, and the resident set may grow by at most GROWTH_LIMIT_MB
between the first mark (KEYS_FIRST_MARK keys) and the end (KEYS keys). Before
the change the loop grew by 45.4 MB over its second 100,000 keys (476 bytes
per key; run with 200,000 keys, since 500,000 did not finish in 1,500 s). The
child is killed past KILL_LIMIT_MB so a regression cannot exhaust the machine.

Usage: dynamic_keys_bounded_memory.py <path-to-protojs> <scratch-dir> [keys]
"""
import json
import os
import subprocess
import sys
import threading
import time

KEYS = 500_000
KEYS_FIRST_MARK = 100_000
PER_OBJECT = 500
HEAP_LIMIT_CELLS = "1000000"
GROWTH_LIMIT_MB = 40
KILL_LIMIT_MB = 1500
TIMEOUT_S = 1500

SCRIPT = r"""
const TOTAL = Number(process.argv[2]);
const PER_OBJECT = %d;
let sum = 0, made = 0, i = 0;
const marks = [];
while (made < TOTAL) {
  // obj[k] = v with keys built at run time, read back by for-in.
  const m = {};
  for (let j = 0; j < PER_OBJECT / 2; j++, i++) m['session-' + i.toString(36) + '-key'] = i;
  for (const k in m) sum += m[k];
  // JSON.parse of an object with run-time keys and string values, read back
  // through Object.keys and element reads.
  let json = '{';
  for (let j = 0; j < PER_OBJECT / 2; j++, i++)
    json += (j ? ',' : '') + '"req-' + i.toString(36) + '-id":"v-' + i.toString(36) + '-value"';
  const o = JSON.parse(json + '}');
  for (const k of Object.keys(o)) sum += o[k].length;
  made += PER_OBJECT;
  if (made %% 50000 === 0) marks.push([made, process.memoryUsage().rss]);
}
console.log(JSON.stringify({ keys: made, sum: sum, marks: marks }));
""" % PER_OBJECT


def expected_sum(total):
    s, i, made = 0, 0, 0
    to36 = lambda n: format_base36(n)
    while made < total:
        for _ in range(PER_OBJECT // 2):
            s += i
            i += 1
        for _ in range(PER_OBJECT // 2):
            s += len("v-" + to36(i) + "-value")
            i += 1
        made += PER_OBJECT
    return s


def format_base36(n):
    digits = "0123456789abcdefghijklmnopqrstuvwxyz"
    if n == 0:
        return "0"
    out = ""
    while n:
        n, r = divmod(n, 36)
        out = digits[r] + out
    return out


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
    keys = int(sys.argv[3]) if len(sys.argv) == 4 else KEYS
    os.makedirs(scratch, exist_ok=True)
    script = os.path.join(scratch, "dynamic_keys_bounded_memory.js")
    with open(script, "w") as f:
        f.write(SCRIPT)
    env = dict(os.environ)
    env["PROTOCORE_HEAP_LIMIT_CELLS"] = HEAP_LIMIT_CELLS

    is_linux = sys.platform.startswith("linux")
    start = time.time()
    proc = subprocess.Popen([protojs, script, str(keys)], stdout=subprocess.PIPE,
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
    want = expected_sum(keys)
    if result["keys"] != keys or result["sum"] != want:
        print("FAIL: computed keys %s sum %s, expected keys %d sum %d"
              % (result["keys"], result["sum"], keys, want))
        return 1
    marks = {m[0]: m[1] / 1048576.0 for m in result["marks"]}
    first = marks.get(KEYS_FIRST_MARK)
    last = marks.get(keys)
    if first is None or last is None:
        print("FAIL: missing resident-set marks: %s" % sorted(marks))
        return 1
    growth = last - first
    print("exit 0, %.1f s, %d keys, RSS %.0f MB at %d keys, %.0f MB at %d keys "
          "(growth %.1f MB, %.0f bytes per key), sampled peak %.0f MB, heap limit %s cells"
          % (elapsed, keys, first, KEYS_FIRST_MARK, last, keys, growth,
             growth * 1048576 / (keys - KEYS_FIRST_MARK), sampled, HEAP_LIMIT_CELLS))
    if growth > GROWTH_LIMIT_MB:
        print("FAIL: the resident set grew by %.1f MB over %d further distinct keys "
              "(limit %d MB)" % (growth, keys - KEYS_FIRST_MARK, GROWTH_LIMIT_MB))
        return 1
    print("dynamic-keys-bounded-memory: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
