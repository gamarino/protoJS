# protoJS on protoCore's unified exception model: final measurement (2026-10-22)

## Builds

| | protoJS | protoCore | Notes |
|---|---|---|---|
| **BEFORE** | `fb95f3c5d` (`perf/cpp-exceptions` before the port) | `cf52f9f5` | `JSException` thrown as a C++ exception; no `ProtoException` |
| **AFTER** | `40f7d66f3` (`perf/cpp-exceptions` tip) | `65760e4a` (`feature/args-calling-convention` tip; code `e64b211f`) | adopts `ProtoException`, one exception type selected by prototype |
| context only | `8182924ea` (pending-exception flag) | 3.0.0 | measured on 2026-10-21 in [`../2026-10-21-cpp-exceptions/`](../2026-10-21-cpp-exceptions/), under a different machine state; not re-measured |

- **Build and linkage**
  - Release builds, made in clean worktrees under
    `.agent_scratch/meas_1022/{before,after}/{protoCore,protoJS}`.
  - protoJS was configured with `-DCMAKE_DISABLE_FIND_PACKAGE_protoCore=ON`
    and links its sibling `../protoCore/build_release`.
  - Both binaries carry a `RUNPATH`, never `/usr/lib`. Every run loaded a
    fresh copy of its own `libprotoCore.so.4` through `LD_LIBRARY_PATH`; see
    `ldd.txt`.
- **Build script**: `build.sh` built everything before any measurement. Builds
  ran one at a time; protoJS used `-j4`.
- **Measurement script**: `js_run.sh`, written for the measuring machine's
  paths. Nothing else ran during the measurements.
- **Quiet gate**: `quiet_*.txt`, with **`QUIET_CORES=4.0`**.
  - The default threshold of 2.5 was never reached. The VS Code GPU process
    sat at about 100 % of a CPU for 13 h and Chrome added 1-2 more.
  - At the start of the runs, 2.1-3.7 CPUs were busy. That is about 0.5-1 CPU
    more than on 2026-10-21.
  - Absolute times are therefore 15-30 % higher than in the 10-21 report.
    Only the interleaved BEFORE/AFTER ratios are meaningful.
- **Self-report check**: `js_verify.py`, output in `verify.txt`.
  - Every standard workload and every probe ran once with BEFORE, AFTER and
    Node. The work each one reported had to be identical: every
    `__BENCH_RESULT__` field except timings, and the probes' `CHECK` line.
  - 29 of 30 matched.
  - `parallel_cpu` differs only in its `executor` and `parallel` fields: Node
    runs it sequentially, protoJS on 4 protoCore threads. Its `total_work` is
    the same (8,000,000), and BEFORE equals AFTER.
  - Many standard workloads report only their parameters, not a checksum.
    `tree_traversal` (`sum`) and `list_snapshot_history` (`checksum`) do
    report a result. The structure benchmarks verify per-task checksums
    against Node.

## Standard comparison (`tests/benchmarks/run_standard_comparison.js`, Node 22.17.0)

- Three rounds, with the order alternated per round (A,B / B,A / A,B).
- Each figure is the median of three per-round medians, in ms; each per-round
  median is over 5 in-process runs.
- Raw data: `{before,after}_r{1,2,3}.{txt,json}`.

| Workload | AFTER | BEFORE | AFTER / BEFORE | Node | 8182924ea (10-21, context) |
|---|---:|---:|---:|---:|---:|
| `object_write_only` | 2054 | 2067 | 0.994 | 17 | 1595 |
| `object_property` | 687 | 672 | 1.022 | 54 | 502 |
| `array_literal` | 253 | 244 | 1.037 | 4 | 187 |
| `function_calls` | 242 | 238 | 1.017 | 1 | 183 |
| `control_flow` | 226 | 240 | 0.942 | 7 | 182 |
| `numeric_loop` | 114 | 98 | **1.163** | 1 | 78 |
| `object_read_only` | 102 | 97 | 1.052 | 2 | 80 |
| `string_concat` | 96 | 93 | 1.032 | 2 | 72 |
| `tree_traversal` | 65 | 70 | 0.929 | 1 | 53 |
| `json_transform` | 45 | 44 | 1.023 | 2 | 36 |
| `list_snapshot_history` | 19 | 19 | 1.000 | 0 | 14 |

- Geomean of AFTER / BEFORE over these eleven workloads (each 10 ms or
  more): **1.017**.
- The seven workloads under 10 ms are in `analysis.txt` and are not
  interpretable.
- Per-round spread is up to 25 %; for example, BEFORE `tree_traversal` ran
  70, 92 and 69 ms. Apart from `numeric_loop`, every ratio lies within that
  noise.

### `numeric_loop` (the one regression), rechecked

`recheck/` holds the recheck: three more interleaved rounds with
`perf stat -r 3` cycles and instructions.

| | BEFORE | AFTER |
|---|---:|---:|
| in-process ms (r1, r2, r3) | 86, 86, 90 | 95, 95, 105 |
| cycles (whole process) | 1.363-1.366 G | 1.548-1.686 G |
| instructions | 4.296 G | 4.252 G (**−1.0 %**) |

- AFTER executes 1 % *fewer* instructions but needs 13-23 % more cycles.
- The plausible cause is code layout or branch alignment in the interpreter
  loop after the port changed `ProtoInterpreter.cpp` and 27 other files (+563/−279 under `src/`):
  the IPC dropped, not the work. **Not isolated.** Neither perf-annotate nor
  any layout experiment was run.
- `function_calls` and `tree_traversal` in the same recheck: instructions
  are −0.5 % and −0.2 %, and cycles are within ±3 %.

### Startup

- The probe run (`perf stat -r 3`) showed AFTER startup at +12 % cycles.
- The recheck (`recheck/startup.txt`, `-r 10`, three interleaved rounds)
  does not reproduce it: BEFORE 74.7-79.1 M cycles, AFTER 75.0-78.5 M.
- **No startup regression.** The first reading was noise.

## Exception probes ([`probes/`](probes/), `probes.txt`)

- Each probe ran under `perf stat -r 3`, all variants interleaved per probe.
- The table shows cycles per iteration net of start-up. Start-up is measured
  by `startup.js`, an empty script with one `CHECK` line, and subtracted
  before dividing by the iteration count.
- Node's start-up variance exceeds the work of the cheap loops, so its
  cheap-loop rows read about 0.

| Probe | iterations | AFTER | BEFORE | AFTER / BEFORE | Node | 8182924ea (10-21, context, not net of start-up) |
|---|---:|---:|---:|---:|---:|---:|
| `throw_primitive` (`throw i`, same function) | 200k | 11,013 | 11,321 | 0.97x | 4,394 | 1,005 |
| `throw_catch` (`throw new Error`) | 200k | 27,762 | 29,548 | 0.94x | 15,635 | 16,373 |
| `native_typeerror` (`undefined.x`) | 200k | 70,655 | 69,693 | 1.01x | 44,352 | 17,169 |
| `throw_across_calls` (5 frames deep) | 100k | 149,192 | 160,538 | **0.93x** | 17,627 | 30,618 |
| `try_no_throw` | 2M | 618 | 639 | 0.97x | ~0 | 441 |
| `finally_loop` | 2M | 912 | 944 | 0.97x | ~0 | 652 |
| `empty_loop` | 2M | 413 | 422 | 0.98x | ~0 | 401 |
| `method_call` | 1M | 2,113 | 2,153 | 0.98x | ~0 | 2,150 |
| `gen_iter` | 200k | 330,729 | 330,576 | 1.00x | 425 | 325,999 |
| `async_await` | 100k | 38,560 | 38,536 | 1.00x | 380 | 37,768 |
| `fib(25)` (Mcycles) | 1 | 397 | 397 | 1.00x | ~0 | 451 (whole process) |

- The unified model costs nothing on these paths. Throws are 3-7 % cheaper,
  which is at the edge of the noise for `-r 3`. Normal paths are unchanged.
- Against the pending-flag build (`8182924ea`), the C++-exception design is
  still far more expensive on throw:
  - `throw_primitive`: about 11x.
  - `native_typeerror`: about 4x.
  - `throw_across_calls`: about 5x.
  - Entering a `try` costs about +40 %.
- Those costs belong to the 10-21 change (C++ unwinding, LSDA scans), not to
  this port. These context figures are not net of start-up, so for the cheap
  loops they are upper bounds.

## Structure benchmarks (`benchmarks/structures/run.py`)

- Settings: reps 3, scale 1, N = 1, 2, 4, 6 and 12, sequential (`seq`) and
  parallel (`par`).
- Order: per workload, BEFORE then AFTER, each behind the gate. These
  workloads were *not* alternated.
- Every run's checksums were verified against Node.
- Raw data: `structures_{before,after}.jsonl`; tables in
  `structures_analysis.txt`.

| Workload | geomean AFTER / BEFORE (10 cells) |
|---|---:|
| `records` | 0.991 |
| `graph` | 0.999 |
| `wordfreq` | 1.032 |
| `doctree` | 1.039 |
| `join` | 1.081 |
| **all 50 cells** | **1.028** (cells 0.92-1.38) |

- The worst cells are `join seq N=1` (1794 against 1296 ms, 1.38) and `join
  par N=1` (1.18). The larger-N `join` cells are 0.95-1.13.
- These workloads are dominated by protoCore allocation and GC.
- BEFORE always ran first within each workload, so a drift in machine load
  would bias the comparison one way.
- The +3 % is within the 10-21 run's cell spread (0.81-1.20). Treat it as
  noise until an alternated rerun says otherwise. **Not isolated.**

## test262 recheck

The three tests that timed out under load were rerun on the gated machine
with the AFTER binary. Setup:

- Runner: `tests/test262/runner/test262_runner.js`, with
  `TEST262_USE_PROTO_EVAL=1` and `TEST262_CONCURRENCY=1`.
- Corpus: `aae8cf6e`, 5 s timeout.
- Raw output: `test262/run.txt` and `test262/snapshot.json`.

| Test | Result | Time |
|---|---|---:|
| `staging/sm/JSON/parse-mega-huge-array.js` | **passed** | 3187 ms |
| `staging/sm/TypedArray/set-same-buffer-different-source-target-types.js` | **passed** | 2275 ms |
| `staging/sm/Array/length-truncate-nonconfigurable-sparse.js` | failed_semantics | 2333 ms |

- `length-truncate-nonconfigurable-sparse` is **not** a regression.
  - BEFORE fails it identically: `Expected SameValue(«1», «27182819»)`.
  - The whole-corpus snapshots of 2026-10-21 (`build_ue`, three runs) and of
    the earlier `pjs_s5` build also record it as `failed_semantics`. Only
    the loaded run turned it into a timeout.
  - It is a pre-existing bug in array `length` truncation over a
    non-configurable sparse element.
- The two passing tests run in 2.3-3.2 s against a 5 s timeout. Under load
  they can still time out.

## Files

- `build.sh`, `js_run.sh`, `js_verify.py`, `js_analyze.py`, `js_struct.py`:
  the scripts.
- `analysis.txt` and `structures_analysis.txt`: the full tables, with
  per-round raw values.
- `recheck/`: the `numeric_loop` and startup recheck.
- The worktrees are kept for the next measurement:
  - `/home/gamarino/Documentos/proyectos/.agent_scratch/meas_1022/before/{protoCore,protoJS}`
  - `/home/gamarino/Documentos/proyectos/.agent_scratch/meas_1022/after/{protoCore,protoJS}`
  - protoCore builds are in `build_release/`, protojs in `build/`.
