# JavaScript exceptions as C++ exceptions: final measurement (2026-10-21)

- **NEW**: `perf/cpp-exceptions` at `fb95f3c5d`. A JavaScript `throw` is a C++
  `throw` of `protojs::JSException`, and each `try` is a nested level of the
  interpreter inside a C++ `try`.
- **BASE**: `perf/fast-paths` at `8182924ea`. It used a pending-exception
  check after each call.
- **Build and setup**:
  - Both are Release builds on protoCore 3.0.0.
  - NEW was built in a clean worktree.
  - Each run waited for the quiet gate (`quiet_*.txt`; VS Code kept 2-3 CPUs
    busy throughout) and loaded a fresh copy of the library.
  - Script: [`run.sh`](run.sh). Its paths are those of the measuring machine.

## Standard comparison

`run_standard_comparison.js`, three rounds with the two builds interleaved,
medians in ms. Node.js is 22.17.0.

| Workload | NEW | BASE | NEW / BASE | Node |
|---|---:|---:|---:|---:|
| `object_write_only` | 1608 | 1595 | 1.01 | 14 |
| `object_property` | 532 | 502 | 1.06 | 39 |
| `function_calls` | 199 | 183 | 1.09 | 1 |
| `array_literal` | 195 | 187 | 1.04 | 3 |
| `control_flow` | 186 | 182 | 1.02 | 5 |
| `numeric_loop` | 84 | 78 | 1.08 | 1 |
| `object_read_only` | 80 | 80 | 1.00 | 1 |
| `string_concat` | 76 | 72 | 1.06 | 1 |
| `tree_traversal` | 63 | 53 | 1.19 | 1 |
| `json_transform` | 37 | 36 | 1.03 | 1 |
| `list_snapshot_history` | 15 | 14 | 1.07 | 0 |

- The geomean of NEW / BASE over these eleven workloads (all 10 ms or more) is
  **1.058**: the normal path is about 6 % slower.
- `tree_traversal` (+19 %) and `function_calls` (+9 %) are the most
  call-heavy workloads.
- A likely cause is that `runBytecodeImpl` carries 457 call-site records of
  unwind tables. Each call into it now sets up a frame that is larger and more
  scattered in the instruction cache. This cause has not been isolated.

## Exception probes

[`probes/`](probes/): `perf stat -r 3` cycles for the whole process, once each.
The table divides those cycles by the iteration count, so process start-up is
included and the figures are upper bounds.

| Probe | iterations | NEW | BASE | NEW / BASE | Node |
|---|---:|---:|---:|---:|---:|
| `throw_primitive` (`throw i` caught in the same function) | 200k | 11,530 | 1,005 | 11.5x | 4,985 |
| `throw_catch` (`throw new Error`) | 200k | 28,199 | 16,373 | 1.72x | 16,165 |
| `native_typeerror` (`undefined.x`) | 200k | 67,552 | 17,169 | 3.9x | 39,598 |
| `throw_across_calls` (five frames deep) | 100k | 144,471 | 30,618 | 4.7x | 17,446 |
| `try_no_throw` | 2M | 672 | 441 | 1.52x | 54 |
| `finally_loop` | 2M | 934 | 652 | 1.43x | 56 |
| `empty_loop` | 2M | 443 | 401 | 1.11x | 61 |
| `fib` (whole process, Mcycles) | 1 run | 462 | 451 | 1.02x | 105 |
| `method_call` | 1M | 2,145 | 2,150 | 1.00x | 105 |
| `gen_iter` | 200k | 323,604 | 325,999 | 0.99x | 958 |
| `async_await` | 100k | 37,717 | 37,768 | 1.00x | 1,486 |

- A thrown exception costs 1.7-11.5x more than before.
- Entering a `try` costs +50 %: it calls a non-inlined function inside a C++
  `try`.
- C++ unwinding is the root cause of the throw cost. It looks up the FDE
  (`_Unwind_Find_FDE` / `_dl_find_object`), runs the personality routine, and
  scans the 457-record LSDA of `runBytecodeImpl` twice per frame (search phase
  and cleanup phase).
- The old protoJS check was one local bool per dispatch.

## Structure benchmarks

`benchmarks/structures/run.py`, reps 3, scale 1, with 1, 2, 4, 6 and 12
copies of the work, sequential and on workers. The raw results are in
`structures_{new,base}.jsonl`.

- The geomean of NEW / BASE over the 50 protoJS cells is **1.012**, which is
  within this machine's noise.
- The individual cells range from 0.81 to 1.20 with no pattern.
- These workloads are dominated by protoCore allocation and the GC, not by
  interpreter dispatch.

## Correctness (same branch, measured earlier)

- test262, whole corpus: 33,849 passes, against 33,824 for BASE. The branch
  wins 26 tests and loses 1. The lost test is a timeout flake,
  `map/15.4.4.19-8-c-ii-1`, which takes 4 s on both binaries.
- ctest: 169/169 pass.
