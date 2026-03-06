# Standard benchmark suite

Self-contained benchmarks for **protoJS vs Node.js** comparison. Each file:

- Uses only common ES features (no `require`, no `Array.from` in hot path where avoidable).
- Runs the same workload in both engines.
- Prints a single line at the end: `__BENCH_RESULT__<json>` with `time_ms` (median of several runs) so the runner can compare **in-process** time instead of wall-clock.

This gives **significant, comparable results** because both engines execute the same code and we measure the same thing.

## Benchmarks

| File | Description | Workload |
|------|-------------|----------|
| `numeric_loop.js` | Integer loop and sum | 1e6 iterations, 5 runs, median ms |
| `array_literal.js` | Array via push in loop | 100k elements, 5 runs |
| `object_property.js` | Object property read/write | 200k ops (100 keys), 5 runs |
| `string_concat.js` | String concatenation | 50k concats, 5 runs |
| `function_calls.js` | No-op function call overhead | 2e6 calls, 5 runs |
| `control_flow.js` | Conditionals in loop | 1e6 iterations if/else, 5 runs |
| `parallel_cpu.js` | Heavy parallel CPU (multithreading) | 4 × (2e6 iterations). Under protojs (`__protojs__`) runs sequential so the benchmark completes (Deferred callbacks may not drain before CLI event-loop timeout). Under Node or other runtimes uses Deferred/protoCore when available. |

**Note:** `parallel_worker.js` is a worker script used by a potential parallel_cpu implementation (Worker-based), not a standalone benchmark; the runner skips `*_worker.js` files.

## How to run

From the **protoJS project root**:

```bash
node tests/benchmarks/run_standard_comparison.js
```

- **protoJS:** If the protoCore compile path fails, the CLI falls back to direct QuickJS eval so scripts still run and emit `__BENCH_RESULT__`. `parallel_cpu.js` uses the sequential path under protojs so it always completes.

Output:

- Console: per-benchmark times and speedup, geometric mean.
- `tests/benchmarks/results/standard_comparison.json`: full results and summary.

## Design

- **No external deps**: each script is one file, runnable by both `node` and `protojs`.
- **Median of 5 runs**: reduces noise; runner parses `time_ms` from the JSON line.
- **Standard-style workloads**: inspired by common engine benchmarks (loop, array, object, string, call overhead, control flow).

For the legacy comparison (wall-clock, mixed workloads) use `run_nodejs_comparison.js`.
