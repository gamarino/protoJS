# Standard Benchmark Results: protoJS vs Node.js

**Last run:** 2026-02-14  
**Suite:** `tests/benchmarks/standard/`  
**Runner:** `node tests/benchmarks/run_standard_comparison.js` or `run_nodejs_comparison.js --standard`

---

## Results (in-process median time, 5 runs)

| Benchmark       | protoJS (ms) | Node.js (ms) | Node faster |
|----------------|--------------|--------------|-------------|
| array_literal  | 8            | 3            | **2.67x**   |
| control_flow   | 64           | 8            | **8x**      |
| function_calls | 88           | 1            | **88x**     |
| numeric_loop   | 45           | 1            | **45x**     |
| object_property| 107          | 45           | **2.38x**   |
| parallel_cpu   | 258          | 8            | **32.25x**  |
| string_concat  | 6            | 2            | **3x**      |

- **Geometric mean:** Node.js **11x** faster than protoJS.
- **All 7 benchmarks** completed successfully on both engines.

---

## Interpretation

- **Fair comparison:** Same self-contained scripts in both engines; median of 5 runs; in-process time only (no wall-clock or startup noise).
- **Engine model:** Node uses V8 (JIT); protoJS is interpreted. Large gaps on CPU-bound and call-heavy benchmarks are expected; smaller gaps on object/array workloads show relative strength of protoJS's object path.
- **parallel_cpu:** Intended to run in parallel on protoJS (workers) vs sequential on Node to expose multithreading advantage. Currently runs **sequential on both** until worker message delivery (postMessage → main-thread 'message' event) is fully verified; Worker EventEmitter wiring (`events.EventEmitter`, `.on`, `.emit`) and cross-context message serialization (worker→main JSON) are in place.

### Per-benchmark notes

| Benchmark       | Notes |
|----------------|-------|
| array_literal   | Smaller gap; array growth is relatively efficient in protoJS. |
| control_flow   | Branch-heavy code benefits from JIT. |
| function_calls | Call overhead much higher in interpreter (decode, dispatch, frame setup). |
| numeric_loop   | Pure CPU loop; JIT vs interpreter explains large gap. |
| object_property| Closest ratio; property access is a relative strength for protoJS. |
| parallel_cpu   | 4×2e6 iterations; will show protoJS advantage when run in parallel. |
| string_concat  | V8 optimizes string handling; protoJS does more work per concat. |

---

## How to run

From the protoJS project root:

```bash
node tests/benchmarks/run_standard_comparison.js
```

JSON report: `tests/benchmarks/results/standard_comparison.json`  
Detailed analysis: `tests/benchmarks/results/standard_comparison_analysis.md`
