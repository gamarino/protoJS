# Standard Benchmark Results: protoJS vs Node.js

**Last run:** 2026-02-18  
**Suite:** `tests/benchmarks/standard/`  
**Runner:** `node tests/benchmarks/run_standard_comparison.js` or `run_nodejs_comparison.js --standard`

---

## Results (in-process median time, 5 runs)

| Benchmark       | protoJS (ms) | Node.js (ms) | Node faster |
|----------------|--------------|--------------|-------------|
| numeric_loop   | 34           | 1            | **34x**     |
| function_calls | 61           | 1            | **61x**     |
| parallel_cpu   | 249          | 6            | **41.5x**   |
| control_flow   | 44           | 4            | **11x**     |
| string_concat  | 6            | 1            | **6x**      |
| array_literal  | 5            | 2            | **2.5x**    |
| object_property| 76           | 33           | **2.3x**    |

- **Geometric mean:** Node.js **11.8x** faster than protoJS.
- **All 7 benchmarks** completed successfully on both engines.

---

## Interpretation

- **Fair comparison:** Same self-contained scripts in both engines; median of 5 runs; in-process time only (no wall-clock or startup noise).
- **Engine model:** Node uses V8 (JIT); protoJS is interpreted. Large gaps on CPU-bound and call-heavy benchmarks are expected; smaller gaps on object/array workloads show relative strength of protoJS’s object path.
- **parallel_cpu:** Intended to run in parallel on protoJS (Deferred/workers) vs sequential on Node to expose multithreading advantage. Currently runs sequential on both until Deferred serialization or Worker message API is available; see `tests/benchmarks/standard/README.md`.

### Per-benchmark notes

| Benchmark       | Notes |
|----------------|-------|
| numeric_loop   | Pure CPU loop; JIT vs interpreter explains large gap. |
| function_calls | Call overhead much higher in interpreter (decode, dispatch, frame setup). |
| parallel_cpu   | 4×2e6 iterations; will show protoJS advantage when run in parallel. |
| control_flow   | Branch-heavy code benefits from JIT. |
| string_concat  | V8 optimizes string handling; protoJS does more work per concat. |
| array_literal  | Smaller gap; array growth is relatively efficient in protoJS. |
| object_property| Closest ratio; property access is a relative strength for protoJS. |

---

## How to run

From the protoJS project root:

```bash
node tests/benchmarks/run_standard_comparison.js
```

JSON report: `tests/benchmarks/results/standard_comparison.json`  
Detailed analysis: `tests/benchmarks/results/standard_comparison_analysis.md`
