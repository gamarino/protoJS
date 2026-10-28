#!/usr/bin/env python3
# Own-attribute enumeration benchmark generator.
#
# Writes one self-reporting script per (operation, object size) into the
# directory given as the first argument (default: the current directory):
# Object.keys, for-in, JSON.stringify, object spread and Object.assign over
# an object with 4, 16 or 100 literal-keyed attributes. Literal keys keep the
# object in the named-attribute representation (a computed key would move it
# to the property dictionary, runtime/PropertyDictionary.h). Each script
# accumulates a checksum, compares it with the expected value, prints
# "OK op=... acc=..." and throws on a mismatch, so a crash or a wrong result
# never reads as a fast run.
#
# Example (cycles, three runs):
#   python3 benchmarks/own-attributes/generate.py /tmp/oa
#   perf stat -r 3 -e cycles,instructions build/protojs /tmp/oa/keys_16.js
import os
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(OUT, exist_ok=True)
SIZES = [4, 16, 100]
VISITS = 1_000_000  # attribute visits per run (iterations = VISITS / size)
OPS = {
  "keys":      ("acc += Object.keys(o).length;",               lambda n: n),
  "forin":     ("for (const k in o) acc += 1;",                 lambda n: n),
  "stringify": ("acc += JSON.stringify(o).length;",             None),
  "spread":    ("const c = {...o}; acc += c.k{last};",          lambda n: n - 1),
  "assign":    ("const c = Object.assign({}, o); acc += c.k{last};", lambda n: n - 1),
}
for n in SIZES:
    lit = "{" + ", ".join(f"k{i}: {i}" for i in range(n)) + "}"
    json_len = len("{" + ",".join(f'"k{i}":{i}' for i in range(n)) + "}")
    iters = VISITS // n
    for op, (stmt, per) in OPS.items():
        body = stmt.replace("{last}", str(n - 1))
        per_iter = json_len if op == "stringify" else per(n)
        expected = per_iter * iters
        src = f"""// {op} over an object with {n} literal-keyed attributes ({iters} iterations).
const o = {lit};
const iters = {iters};
let acc = 0;
for (let i = 0; i < iters; i++) {{ {body} }}
const expected = {expected};
if (acc !== expected) {{ console.log("FAIL {op} n={n} acc=" + acc + " expected=" + expected); throw new Error("benchmark result mismatch"); }}
console.log("OK op={op} n={n} iters=" + iters + " acc=" + acc);
"""
        open(os.path.join(OUT, f"{op}_{n}.js"), "w").write(src)
open(os.path.join(OUT, "startup.js"), "w").write('console.log("OK op=startup");\n')
