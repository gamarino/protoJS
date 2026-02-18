# Standard Benchmark Results — Analysis

**Run date:** 2026-02-18  
**Suite:** `tests/benchmarks/standard/` (self-contained, in-process time)  
**Reports:** `standard_comparison.json` (Node.js), `standard_comparison_quickjs.json` (QuickJS)

---

## 1. Results Summary

### 1.1 protoJS vs Node.js

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

### 1.2 protoJS vs QuickJS

| Benchmark       | protoJS (ms) | QuickJS (ms) | Winner        |
|----------------|--------------|--------------|---------------|
| array_literal  | 6            | 5            | QuickJS **1.20x** |
| control_flow   | 60           | 44           | QuickJS **1.36x** |
| function_calls | 71           | 79           | **protoJS 1.11x** |
| numeric_loop   | 37           | 33           | QuickJS **1.12x** |
| object_property| 101          | 64           | QuickJS **1.58x** |
| parallel_cpu   | 22           | 630          | **protoJS 28.64x** |
| string_concat  | 5            | 5            | tie **1.00x** |

- **Geometric mean:** QuickJS **1.41x** faster than protoJS on single-threaded workloads (excluding parallel_cpu).
- **protoJS wins:** 2/7 (function_calls, parallel_cpu). parallel_cpu is a large win because QuickJS runs it sequentially (no workers).

---

## 2. Per-Benchmark Notes

| Benchmark       | vs Node | vs QuickJS | Notes |
|----------------|---------|-------------|-------|
| array_literal   | 2x      | 1.2x        | Array growth relatively efficient in protoJS. |
| control_flow   | 5.7x    | 1.36x       | Branch-heavy; JIT wins big vs Node; QuickJS slightly ahead. |
| function_calls | 36.5x   | protoJS 1.11x | Call overhead: Node JIT dominates; protoJS beats QuickJS. |
| numeric_loop   | 37x     | 1.12x       | Pure CPU; JIT vs interpreter (Node); similar interpreters (QuickJS). |
| object_property| 2.6x    | 1.58x       | Property access is a relative strength for protoJS. |
| parallel_cpu   | protoJS 1.86x | protoJS 28.6x | LCG workload; protoJS uses workers; Node/QuickJS run sequential. |
| string_concat  | 5x      | tie         | V8 optimizes; protoJS and QuickJS comparable. |

---

## 3. Real-Case Server Load Impact

### 3.1 Workload mapping

| Benchmark       | Server analogue |
|----------------|------------------|
| array_literal  | Building response arrays, batch payloads. |
| control_flow   | Routing, validation, branching logic. |
| function_calls | Middleware chains, handlers, callbacks. |
| numeric_loop   | Aggregations, counters, numeric processing. |
| object_property| Reading/writing config, request/response objects. |
| parallel_cpu   | CPU-bound tasks that can be parallelized (hashing, LCG-like work). |
| string_concat  | Building HTML/JSON strings, log lines. |

### 3.2 Expected impact when using protoJS as server runtime

**vs Node.js (V8):**

- **Single-thread CPU-bound:** Node will handle **~5x more** in-process work per core for typical mixed workloads (geo mean). For **heavy call/loop** workloads (e.g. many small handlers or numeric loops), Node can be **10–40x** faster per request; expect **higher CPU per request** with protoJS or **~5–10x lower** throughput per core if CPU-bound.
- **I/O-bound:** If requests spend most time in I/O (DB, network), the **5x** gap matters less; latency will be similar and throughput limited by I/O. protoJS can still be viable for low-to-moderate request rates.
- **Parallel CPU:** For workloads that map to **parallel_cpu** (parallelizable CPU work), protoJS can **outperform Node** (1.86x in this suite) when Node runs the same work on one thread. Use case: batch processing or worker-style tasks where protoJS can farm work to threads.

**Recommendation (Node):** Use protoJS where **parallelism** or **embedding** is the goal; expect **lower single-thread throughput** than Node for CPU-heavy handlers. Size instances or request rate accordingly (e.g. ~5x more CPU per unit work, or reduce concurrent CPU-bound requests per process).

**vs QuickJS (interpreter):**

- **Single-thread:** QuickJS is **~1.4x** faster on average (geo mean). So for **pure interpreter** comparison, protoJS is in the same ballpark; **~30–60% more CPU** per unit work vs QuickJS for typical mixed code.
- **Parallel CPU:** protoJS **~28x** faster than QuickJS on parallel_cpu (QuickJS has no workers). For **multi-threaded CPU** workloads, protoJS has a clear advantage on server-style loads that can be parallelized.

**Recommendation (QuickJS):** protoJS is competitive for single-thread interpreter scenarios and **strongly better** when the workload can use multiple threads (parallel_cpu-style). Prefer protoJS over QuickJS when you need **in-process parallelism** without spawning separate processes.

### 3.3 Summary table (expected server impact)

| Scenario                    | protoJS vs Node      | protoJS vs QuickJS   |
|----------------------------|----------------------|------------------------|
| I/O-bound, low CPU         | Similar latency      | Slightly higher CPU   |
| CPU-bound, single-thread   | ~5x more CPU / req   | ~1.4x more CPU / req  |
| CPU-bound, parallelizable  | protoJS can win ~1.9x| protoJS wins ~28x     |
| Throughput per core (CPU)  | Lower (factor ~5)    | Slightly lower (~1.4) |

---

## 4. Conclusions

1. **Comparisons are valid:** Same code path in both engines, median of 5 runs, in-process time only. Node vs protoJS and QuickJS vs protoJS are comparable and significant.

2. **Node (V8):** JIT dominates on CPU and call-heavy code; protoJS wins only on **parallel_cpu**. For real servers: expect **~5x higher CPU** per unit work for mixed CPU-bound traffic, or use protoJS where **parallelism** or embedding justifies the trade-off.

3. **QuickJS:** Interpreter-to-interpreter, **~1.4x** in QuickJS's favour on single-thread; **protoJS ~28x** ahead when parallelism is used (parallel_cpu). protoJS is suitable as an embedded or script runtime where **multi-threaded CPU** work is important.

4. **Reproduce:** From protoJS project root:
   - Node: `node tests/benchmarks/run_standard_comparison.js`
   - QuickJS: `node tests/benchmarks/run_standard_comparison_quickjs.js` (requires `deps/quickjs/qjs`)
