# Standard benchmark suite

Self-contained benchmark scripts used to compare protoJS with Node.js and with a plain QuickJS interpreter. Each script:

- uses only common ECMAScript features and no `require`, so it runs unchanged under `protojs`, `node` and QuickJS;
- repeats its workload several times (`ITERATIONS`) and takes the median time measured inside the script;
- prints one final line `__BENCH_RESULT__<json>` whose `time_ms` field the runners read, so that process start-up time is not measured.

## Benchmarks

| File | Workload | Size (per iteration) | Iterations |
|------|----------|----------------------|-----------:|
| `numeric_loop.js` | Integer loop and sum | 1e6 steps | 5 |
| `control_flow.js` | `if`/`else` inside a loop | 1e6 steps | 5 |
| `function_calls.js` | No-op function calls | 2e5 calls | 5 |
| `array_literal.js` | Array built with `push` | 100,000 elements | 5 |
| `object_property.js` | Object property reads and writes | 200,000 operations | 5 |
| `object_read_only.js` | Property reads on a pre-populated object | 100,000 reads over 100 keys | 5 |
| `object_write_only.js` | Property writes with pre-created keys | 1,000,000 writes over 100 keys | 5 |
| `string_concat.js` | One-character appends | 50,000 appends | 5 |
| `string_concat_large_chunks.js` | Appends of 200-character chunks | see script | 5 |
| `string_insert_middle.js` | `s.slice(0, mid) + chunk + s.slice(mid)` | 100 inserts of 50 characters into a 1,000-character string | 5 |
| `string_repeated_doubling.js` | `s = s + s` | 200 sequences of 18 doublings | 5 |
| `string_processing.js` | CSV generation and field parsing | 100 rows | 5 |
| `json_transform.js` | Build, filter, map and serialize records | 5,000 records | 5 |
| `json_transform_small.js` | Same pipeline | 500 records | 5 |
| `json_transform_tiny.js` | Same pipeline | 50 records | 1 |
| `list_snapshot_history.js` | Keep every version of a growing array (`concat`) | 200 steps | 5 |
| `tree_traversal.js` | Build a binary tree and sum it recursively | depth 14 (16,383 nodes) | 5 |
| `parallel_cpu.js` | Four CPU tasks run in parallel | 2e5 steps per task under `protojs`, 2e6 elsewhere | 5 |

`parallel_cpu.js` uses `protoCore.runInThread('cpuChunk', ...)` under `protojs` when it is available, and reports `"parallel": true` or `false` in its result line depending on whether the tasks ran in parallel. `parallel_cpu_worker.js` and `parallel_worker.js` are worker scripts, not benchmarks; the runners skip every file ending in `_worker.js`.

## How to run

From the repository root:

```bash
# protoJS vs Node.js
node tests/benchmarks/run_standard_comparison.js

# protoJS vs QuickJS
node tests/benchmarks/run_standard_comparison_quickjs.js
```

- **protoJS binary:** `run_standard_comparison.js` uses `PROTOJS_BIN` when set, and otherwise searches `build_release/protojs` before `build/protojs`.
- **QuickJS binary:** `run_standard_comparison_quickjs.js` looks for `tests/benchmarks/qjs_minimal` or `qjs_raw` in the repository root. Neither binary is tracked; `tests/benchmarks/qjs_minimal.c` is a minimal QuickJS host built against `deps/quickjs`.
- **Timeout:** each benchmark run is limited to 120 seconds.

Output:

- Console: time per benchmark, the ratio between the engines, and the geometric mean.
- JSON: `tests/benchmarks/results/standard_comparison.json` (Node.js runner) or `tests/benchmarks/results/standard_comparison_quickjs.json` (QuickJS runner). Dated reports are kept in `tests/benchmarks/results/`.

`tests/benchmarks/run_nodejs_comparison.js` is an older comparison that measures wall-clock time over mixed workloads.
