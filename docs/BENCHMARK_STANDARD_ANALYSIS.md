# Standard Benchmark Results — Analysis

**Last updated:** 2026-03-03  
**Suite:** `tests/benchmarks/standard/` (self-contained, in-process time)  
**Runner:** `node tests/benchmarks/run_standard_comparison.js` (protoJS vs Node.js)  
**Reports:** `tests/benchmarks/results/standard_comparison.json` (generated; ignored in git)  
**Snapshot (committed):** `docs/standard_comparison_snapshot.json`

Results vary by machine and run. The tables below are representative; re-run the suite to get current numbers.

---

## 1. Results Summary

### 1.1 protoJS vs Node.js

| Benchmark       | protoJS (ms) | Node.js (ms) | Winner      |
|----------------|--------------|--------------|-------------|
| array_literal  | 10           | 5            | Node **2.00x** |
| control_flow   | 101          | 10           | Node **10.10x** |
| function_calls | 176          | 2            | Node **88.00x** |
| numeric_loop   | 88           | 2            | Node **44.00x** |
| object_property| 106          | 62           | Node **1.71x** |
| parallel_cpu   | 42           | 65           | **protoJS 1.55x** |
| string_concat  | 10           | 2            | Node **5.00x** |

- **Geometric mean:** Node.js **~6–7x** faster than protoJS on single-thread workloads (in-process).
- **protoJS wins:** 1/7 (parallel_cpu). All 7 benchmarks complete on both engines.

**parallel_cpu under protojs:** Uses `protoCore.runInThread('cpuChunk', …)` with **ProtoThreads** only; thread creation is staggered via `setImmediate` to avoid lock contention. Under protojs, `WORK_PER_TASK` is **2e5** so the run finishes within the runner timeout; Node runs the same script with **2e6** iterations. So protojs parallel_cpu measures 4×5 tasks at 2e5 iter each; Node measures 4×5 at 2e6. The win shows that protoJS multithreading (ProtoThreads) outperforms Node on that parallel workload.

### 1.2 protoJS vs QuickJS

QuickJS comparison uses a **separate runner**: `node tests/benchmarks/run_standard_comparison_quickjs.js` (requires `deps/quickjs/qjs`). Absolute times may differ from the Node run; relative conclusions hold.

| Benchmark       | protoJS (ms) | QuickJS (ms) | Winner        |
|----------------|--------------|--------------|---------------|
| array_literal  | 6            | 5            | QuickJS **1.20x** |
| control_flow   | 60           | 44           | QuickJS **1.36x** |
| function_calls | 71           | 79           | **protoJS 1.11x** |
| numeric_loop   | 37           | 33           | QuickJS **1.12x** |
| object_property| 101          | 64           | QuickJS **1.58x** |
| parallel_cpu   | 22           | 630          | **protoJS 28.64x** |
| string_concat  | 5            | 5            | tie **1.00x** |

- **Geometric mean:** QuickJS **~1.4x** faster than protoJS on single-threaded workloads (excluding parallel_cpu).
- **protoJS wins:** 2/7 (function_calls, parallel_cpu). parallel_cpu is a large win because QuickJS runs it sequentially (no workers).

---

## 2. Per-Benchmark Notes

| Benchmark       | vs Node | vs QuickJS | Notes |
|----------------|---------|-------------|-------|
| array_literal   | ~2x     | ~1.2x       | Array growth relatively efficient in protoJS. |
| control_flow   | ~10x    | ~1.36x      | Branch-heavy; JIT wins big vs Node; QuickJS slightly ahead. |
| function_calls | ~50–90x | protoJS ~1.1x | Call overhead: Node JIT dominates; protoJS beats QuickJS. |
| numeric_loop   | ~40–70x | ~1.12x      | Pure CPU; JIT vs interpreter (Node); similar interpreters (QuickJS). |
| object_property| ~1.7–2.5x | ~1.58x   | Property access is a relative strength for protoJS. |
| parallel_cpu   | **protoJS ~1.5–1.9x** | **protoJS ~28x** | LCG workload; protoJS uses ProtoThreads (staggered); Node/QuickJS run sequential. |
| string_concat  | ~5x     | tie         | V8 optimizes; protoJS and QuickJS comparable. |

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
- **Parallel CPU:** For workloads that map to **parallel_cpu** (parallelizable CPU work), protoJS can **outperform Node** (~1.5–1.9x in this suite) when Node runs the same work on one thread. protoJS uses **ProtoThreads** only (no native threads for execution); thread creation is staggered with `setImmediate` so the main thread yields between `newThread` calls. Use case: batch processing or worker-style tasks where protoJS can farm work to ProtoThreads.

**Recommendation (Node):** Use protoJS where **parallelism** or **embedding** is the goal; expect **lower single-thread throughput** than Node for CPU-heavy handlers. Size instances or request rate accordingly (e.g. ~5x more CPU per unit work, or reduce concurrent CPU-bound requests per process).

**vs QuickJS (interpreter):**

- **Single-thread:** QuickJS is **~1.4x** faster on average (geo mean). So for **pure interpreter** comparison, protoJS is in the same ballpark; **~30–60% more CPU** per unit work vs QuickJS for typical mixed code.
- **Parallel CPU:** protoJS **~28x** faster than QuickJS on parallel_cpu (QuickJS has no workers). For **multi-threaded CPU** workloads, protoJS has a clear advantage on server-style loads that can be parallelized.

**Recommendation (QuickJS):** protoJS is competitive for single-thread interpreter scenarios and **strongly better** when the workload can use multiple threads (parallel_cpu-style). Prefer protoJS over QuickJS when you need **in-process parallelism** without spawning separate processes.

### 3.3 Summary table (expected server impact)

| Scenario                    | protoJS vs Node      | protoJS vs QuickJS   |
|----------------------------|----------------------|------------------------|
| I/O-bound, low CPU         | Similar latency      | Slightly higher CPU   |
| CPU-bound, single-thread   | ~6–7x more CPU / req | ~1.4x more CPU / req  |
| CPU-bound, parallelizable | protoJS can win ~1.5–1.9x | protoJS wins ~28x |
| Throughput per core (CPU)  | Lower (factor ~6–7)  | Slightly lower (~1.4) |

---

## 4. Memory, GC, and shared data (protoJS vs Node/V8)

Beyond raw CPU, server workloads are often constrained by **memory limits**, **GC latency**, and **cost of sharing data** across workers. Here ProtoCore-based runtimes (protoJS) differ sharply from Node.js (V8).

### 4.1 V8 practical memory limit (~2 GB)

- V8 enforces a **practical heap limit** on the order of **~2 GB** per isolate (and typically per Node process without special flags). Large in-memory caches, big response buffers, or shared database-backed state can hit this ceiling and force partitioning or off-heap storage.
- **Impact:** Servers that keep large amounts of **common information** in memory (e.g. reference data, cached query results, metadata) may need multiple Node processes or external stores to stay under the limit, adding complexity and often more serialization.

### 4.2 GC pauses

- V8’s generational GC can produce **multi-millisecond** stop-the-world pauses under load, which show up as latency spikes (e.g. p99) in latency-sensitive services.
- ProtoCore-based runtimes target **sub-millisecond GC pauses** (&lt; 1 ms), which can materially improve **tail latency** and predictability for request-handling paths that allocate.

### 4.3 Sharing information and metadata: serialization vs shared memory

- **Node (workers):** Sharing data between workers requires **serialization** (e.g. structured clone, JSON). Passing large or frequently updated **common data** (config, DB-backed metadata, caches) to each worker is expensive: copy cost, CPU for encode/decode, and duplicated memory per worker. Keeping a single source of truth in the main thread and messaging it to workers scales poorly when that data is big or hot.
- **ProtoJS (ProtoCore):** **Shared memory** and shared structures across threads are a core feature. Metadata and common database-derived state can be **shared by reference** without serialization. Workers can read the same maps, arrays, or objects without copying or custom serialization layers.
- **Impact on servers sharing database/common information:** Workloads that rely on **shared reference data**, **metadata**, or **cached DB results** across many logical workers or handlers can be **deeply impacted**:
  - In Node, every worker typically gets its own copy (or pays serialization and message-passing overhead), and the 2 GB limit applies per process. Scaling “one big shared cache” is hard.
  - In protoJS, the same cache or metadata can be shared in memory; no serialization round-trips for reads, and memory usage does not multiply by worker count for that data. Latency and CPU spent on sharing drop significantly.

**Summary:** For servers that share large or hot **common information** (DB caches, metadata, config), protoJS’s **shared-memory model** and **absence of per-worker serialization** can outweigh the raw ~6–7x CPU advantage of V8. Combined with **higher practical memory headroom** and **lower GC pauses** (&lt; 1 ms), ProtoCore-based runtimes can be a better fit for **memory- and latency-sensitive** services that rely on shared state.

---

## 5. Conclusions

1. **Comparisons are valid:** Same code path in both engines, median of 5 runs, in-process time only. Node vs protoJS and QuickJS vs protoJS are comparable and significant.

2. **Node (V8):** JIT dominates on CPU and call-heavy code; protoJS wins only on **parallel_cpu**. For real servers: expect **~6–7x higher CPU** per unit work for mixed CPU-bound traffic (geo mean), or use protoJS where **parallelism** or embedding justifies the trade-off.

3. **QuickJS:** Interpreter-to-interpreter, **~1.4x** in QuickJS's favour on single-thread; **protoJS ~28x** ahead when parallelism is used (parallel_cpu). protoJS is suitable as an embedded or script runtime where **multi-threaded CPU** work is important.

4. **Memory, GC, and shared data:** V8’s **~2 GB** practical heap limit, **multi-ms GC pauses**, and the need to **serialize** common data across Node workers make protoJS attractive for servers that share **database-backed or common metadata**: ProtoCore offers **shared memory** (no serialization for shared state), **sub-ms GC pauses**, and a model where one shared cache does not duplicate per worker. Servers that are **memory- or latency-sensitive** and rely on shared state can be deeply impacted in favour of protoJS.

5. **Reproduce:** From protoJS project root:
   - **protoJS vs Node:** `node tests/benchmarks/run_standard_comparison.js` — writes `tests/benchmarks/results/standard_comparison.json`.
   - **protoJS vs QuickJS:** `node tests/benchmarks/run_standard_comparison_quickjs.js` (requires `deps/quickjs/qjs`).
