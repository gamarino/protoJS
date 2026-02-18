# Standard Benchmark Results: protoJS vs Node.js

**Last run:** 2026-02-14  
**Suite:** `tests/benchmarks/standard/`  
**Runner:** `node tests/benchmarks/run_standard_comparison.js` or `run_nodejs_comparison.js --standard`

---

## Results (in-process median time, 5 runs)

| Benchmark       | protoJS (ms) | Node.js (ms) | Winner      |
|----------------|--------------|--------------|-------------|
| array_literal  | 10           | 3            | Node **3.33x** |
| control_flow   | 64           | 5            | Node **12.80x** |
| function_calls | 81           | 1            | Node **81x**   |
| numeric_loop   | 45           | 1            | Node **45x**   |
| object_property| 124          | 36           | Node **3.44x** |
| parallel_cpu   | 22           | 41           | **protoJS 1.86x** |
| string_concat  | 5            | 1            | Node **5x**    |

- **Geometric mean:** Node.js **7.58x** faster than protoJS (in-process).
- **protoJS wins:** 1/7 (parallel_cpu). All 7 benchmarks completed on both engines.

---

## Interpretation

- **Fair comparison:** Same self-contained scripts in both engines; median of 5 runs; in-process time only (no wall-clock or startup noise).
- **Engine model:** Node uses V8 (JIT); protoJS is interpreted. Large gaps on CPU-bound and call-heavy benchmarks are expected; smaller gaps on object/array workloads show relative strength of protoJS's object path.
- **parallel_cpu:** Workload is an **LCG (linear congruential) loop** — data-dependent, no closed form, not trivially optimizable by JIT (fair comparison). Same work in JS and native worker. protoJS runs 4 tasks in parallel (protoCore.runInThread or Deferred); Node runs the same work sequentially. Median wall time over 5 rounds; protoJS wins when parallelism outweighs interpreter vs JIT.

### Per-benchmark notes

| Benchmark       | Notes |
|----------------|-------|
| array_literal   | Smaller gap; array growth is relatively efficient in protoJS. |
| control_flow   | Branch-heavy code benefits from JIT. |
| function_calls | Call overhead much higher in interpreter (decode, dispatch, frame setup). |
| numeric_loop   | Pure CPU loop; JIT vs interpreter explains large gap. |
| object_property| Closest ratio; property access is a relative strength for protoJS. |
| parallel_cpu   | LCG workload (4×2e6 iter); protoJS parallel (protoCore threads or Deferred), Node sequential. protoJS wins 1.86x with balanced, JIT-resistant work. |
| string_concat  | V8 optimizes string handling; protoJS does more work per concat. |

---

## How to run

From the protoJS project root:

```bash
node tests/benchmarks/run_standard_comparison.js
```

JSON report: `tests/benchmarks/results/standard_comparison.json`  
Detailed analysis: `tests/benchmarks/results/standard_comparison_analysis.md`
