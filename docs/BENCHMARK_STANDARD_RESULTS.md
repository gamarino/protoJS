# Standard Benchmark Results: protoJS vs Node.js and protoJS vs QuickJS

Two comparisons are maintained for the same standard suite:

- **protoJS vs Node.js** — interpreter vs V8 (JIT); run with `run_standard_comparison.js`.
- **protoJS vs QuickJS** — interpreter vs interpreter; run with `run_standard_comparison_quickjs.js`.

**Suite:** `tests/benchmarks/standard/`
**Last measured:** 2026-02-18
**Last rebuilt:** 2026-04-26 (protoCore 1.1.0 with `-Wl,-Bsymbolic-functions`; intra-DSO PLT eliminated)
**Status:** Numbers below reflect the 2026-02-18 measurement.  Re-measurement progress (2026-04-26):
`Date.now()`, `performance.now()`, and `console.time()` / `console.timeEnd()` were missing
from the runtime and have now been reinstated as native bindings (see
`src/console.cpp::TimingAPIs` and the CHANGELOG entry).  However, `JSON.stringify` is still
undefined on the protoCore-side global — each script in this suite emits
`__BENCH_RESULT__<json>` via `JSON.stringify(result)` on the last line, so re-measurement is
still blocked until JSON is wired up.  A JS polyfill is non-trivial because user-defined
function arguments are not delivered to the callee in the current runtime
(`function f(a,b){return a+b}; f(3,4)` returns `NaN`), which crashes any recursive helper.
The cleanest fix is to plumb QuickJS's native `JSON.stringify` / `JSON.parse` through to the
protoCore global (or to fix the argument-binding regression so a JS polyfill works).

---

## 1. protoJS vs Node.js

**Runner:** `node tests/benchmarks/run_standard_comparison.js` (or `run_nodejs_comparison.js --standard`)

### Results (in-process median time, 5 runs)

| Benchmark       | protoJS (ms) | Node.js (ms) | Winner      |
|----------------|--------------|--------------|-------------|
| array_literal  | 6            | 3            | Node **2.00x** |
| control_flow   | 51           | 9            | Node **5.67x** |
| function_calls | 73           | 2            | Node **36.50x** |
| numeric_loop   | 37           | 1            | Node **37.00x** |
| object_property| 88           | 34           | Node **2.59x** |
| parallel_cpu   | 22           | 41           | **protoJS 1.86x** |
| string_concat  | 5            | 1            | Node **5.00x** |

- **Geometric mean:** Node.js **5.22x** faster than protoJS (in-process).
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

## 2. protoJS vs QuickJS

**Runner:** `node tests/benchmarks/run_standard_comparison_quickjs.js`

Interpreter-vs-interpreter comparison. QuickJS must be built first:

```bash
cd deps/quickjs && make qjs
```

Then from the protoJS project root:

```bash
node tests/benchmarks/run_standard_comparison_quickjs.js
```

### Results (in-process median time, 5 runs)

| Benchmark       | protoJS (ms) | QuickJS (ms) | Winner        |
|----------------|--------------|--------------|---------------|
| array_literal  | 6            | 5            | QuickJS **1.20x** |
| control_flow   | 60           | 44           | QuickJS **1.36x** |
| function_calls | 71           | 79           | **protoJS 1.11x** |
| numeric_loop   | 37           | 33           | QuickJS **1.12x** |
| object_property| 101          | 64           | QuickJS **1.58x** |
| parallel_cpu   | 22           | 630          | **protoJS 28.64x** |
| string_concat  | 5            | 5            | tie **1.00x** |

- **Geometric mean:** QuickJS **1.41x** faster than protoJS on single-thread; protoJS wins **2/7** (function_calls, parallel_cpu).
- **Output:** Script prints the table and writes `tests/benchmarks/results/standard_comparison_quickjs.json`.

For **interpretation and real-case server load impact**, see [BENCHMARK_STANDARD_ANALYSIS.md](BENCHMARK_STANDARD_ANALYSIS.md).

---

## How to run (both comparisons)

From the protoJS project root:

| Comparison        | Command |
|-------------------|--------|
| protoJS vs Node.js   | `node tests/benchmarks/run_standard_comparison.js` |
| protoJS vs QuickJS   | `node tests/benchmarks/run_standard_comparison_quickjs.js` (requires `deps/quickjs/qjs`) |

**Output files:**

- Node: `tests/benchmarks/results/standard_comparison.json`  
  Detailed analysis: `tests/benchmarks/results/standard_comparison_analysis.md`
- QuickJS: `tests/benchmarks/results/standard_comparison_quickjs.json`
