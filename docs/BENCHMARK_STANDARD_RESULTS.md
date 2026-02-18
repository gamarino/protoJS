# Standard Benchmark Results: protoJS vs Node.js

**Last run:** 2026-02-14  
**Suite:** `tests/benchmarks/standard/`  
**Runner:** `node tests/benchmarks/run_standard_comparison.js` or `run_nodejs_comparison.js --standard`

---

## Results (in-process median time, 5 runs)

| Benchmark       | protoJS (ms) | Node.js (ms) | Node faster |
|----------------|--------------|--------------|-------------|
| array_literal  | 13           | 4            | **3.25x**   |
| control_flow   | 82           | 9            | **9.11x**   |
| function_calls | 113          | 2            | **56.50x**  |
| numeric_loop   | 72           | 2            | **36x**     |
| object_property| 130          | 54           | **2.41x**   |
| parallel_cpu   | 125          | 9            | **13.89x**  |
| string_concat  | 8            | 2            | **4x**      |

- **Geometric mean:** Node.js **9.70x** faster than protoJS.
- **All 7 benchmarks** completed successfully on both engines.

---

## Interpretation

- **Fair comparison:** Same self-contained scripts in both engines; median of 5 runs; in-process time only (no wall-clock or startup noise).
- **Engine model:** Node uses V8 (JIT); protoJS is interpreted. Large gaps on CPU-bound and call-heavy benchmarks are expected; smaller gaps on object/array workloads show relative strength of protoJS's object path.
- **parallel_cpu:** Runs in **parallel on protoJS** (Deferred + CPU thread pool; function source evaluated in worker threads) vs sequential on Node. Same total CPU work (4×2e6 iterations); protoJS wall time is the median over 5 rounds of 4 parallel tasks.

### Per-benchmark notes

| Benchmark       | Notes |
|----------------|-------|
| array_literal   | Smaller gap; array growth is relatively efficient in protoJS. |
| control_flow   | Branch-heavy code benefits from JIT. |
| function_calls | Call overhead much higher in interpreter (decode, dispatch, frame setup). |
| numeric_loop   | Pure CPU loop; JIT vs interpreter explains large gap. |
| object_property| Closest ratio; property access is a relative strength for protoJS. |
| parallel_cpu   | 4×2e6 iterations; protoJS runs 4 tasks in parallel via Deferred (multithreading). |
| string_concat  | V8 optimizes string handling; protoJS does more work per concat. |

---

## How to run

From the protoJS project root:

```bash
node tests/benchmarks/run_standard_comparison.js
```

JSON report: `tests/benchmarks/results/standard_comparison.json`  
Detailed analysis: `tests/benchmarks/results/standard_comparison_analysis.md`
