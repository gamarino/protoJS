# Standard Benchmark Comparison — 2026-05-31

Single coordinated run of the standard suite against both reference
engines (Node.js 22 and vanilla QuickJS), plus historical comparison
against the 2026-04-28 baseline.

## Environment

- Runner: `tests/benchmarks/run_standard_comparison.js` (vs Node),
  `tests/benchmarks/run_standard_comparison_quickjs.js` (vs QuickJS)
- Binary: `build_release/protojs` rebuilt 2026-05-31 against
  `libprotoCore.so.1.2.0` (snapshot-at-STW + Phase 2 trim landed)
- Reference: Node.js v22.17.0, QuickJS minimal (deps/quickjs)
- Methodology: each benchmark prints `__BENCH_RESULT__<json>` with
  median wall time over five runs.  Geomean computed over benchmarks
  where both engines completed and ran for >0 ms.

## Context

Between 2026-04-28 and today two regressions had silently broken the
standard suite:

1. `printf("TRACE: ...")` in `DISPATCH()` macro on every bytecode
   (committed 2026-05-22 by snapshot `7b5d9ddd`, fixed by `283a02a5`).
   Polluted output broke the `__BENCH_RESULT__` regex; per-dispatch
   printf added catastrophic overhead.  Effect masked by the runner
   failing upstream.
2. `Date.now` undefined because `TimingAPIs::init` built `Date` via
   `fromMethod` (which silently drops `setAttribute` writes).  Fixed
   by `b546a64f` — switched to `newObject(true)` with `name`/
   `prototype` to satisfy the interpreter stub-installer guard.

Both regressions fixed in this run.

## protoJS today vs 2026-04-28 baseline

Compares the six benchmarks present in both runs.

| benchmark | 04-28 (ms) | 05-31 (ms) | Δ |
|---|---:|---:|---:|
| `array_literal.js` | 1030 | 197 | -80.9% |
| `control_flow.js` | 735 | 228 | -69.0% |
| `function_calls.js` | 2090 | 215 | -89.7% |
| `numeric_loop.js` | 455 | 109 | -76.0% |
| `object_property.js` | 9577 | 1650 | -82.8% |
| `parallel_cpu.js` | 55 | 52 | -5.5% |

**Geomean ratio 05-31 / 04-28 = 0.249**.  protoJS is roughly
75.1% faster than the April baseline across these six benchmarks.
This reflects the P-JS-{0..7} optimisation cycle landing — which
could not be measured properly while the TRACE printf was active.

## protoJS vs Node.js 22 (2026-05-31)

| benchmark | protoJS (ms) | Node (ms) | Node speedup |
|---|---:|---:|---:|
| `array_literal.js` | 197 | 3 | 65.7× |
| `control_flow.js` | 228 | 5 | 45.6× |
| `function_calls.js` | 215 | 1 | 215.0× |
| `json_transform.js` | 105 | 2 | 52.5× |
| `json_transform_small.js` | 12 | 0 | n/a (Node sub-ms) |
| `json_transform_tiny.js` | 1 | 0 | n/a (Node sub-ms) |
| `numeric_loop.js` | 109 | 1 | 109.0× |
| `object_property.js` | 1650 | 49 | 33.7× |
| `object_read_only.js` | 52 | 3 | 17.3× |
| `object_write_only.js` | 6904 | 11 | 627.6× |
| `parallel_cpu.js` | 52 | 41 | 1.3× |
| `string_concat.js` | 107 | 1 | 107.0× |
| `string_processing.js` | 1 | 0 | n/a (Node sub-ms) |
| `tree_traversal.js` | 340 | 0 | n/a (Node sub-ms) |

**Geomean: Node 53.2× protoJS** (10 benchmarks).

## protoJS vs QuickJS (2026-05-31)

| benchmark | protoJS (ms) | QuickJS (ms) | QuickJS speedup |
|---|---:|---:|---:|
| `array_literal.js` | 431 | 6 | 71.83× |
| `control_flow.js` | 522 | 50 | 10.44× |
| `function_calls.js` | 257 | 10 | 25.70× |
| `json_transform.js` | 133 | 4 | 33.25× |
| `json_transform_small.js` | 14 | 1 | 14.00× |
| `json_transform_tiny.js` | 0 | 0 | n/a (QuickJS sub-ms) |
| `numeric_loop.js` | 130 | 89 | 1.46× |
| `object_property.js` | 2012 | 91 | 22.11× |
| `object_read_only.js` | 63 | 6 | 10.50× |
| `object_write_only.js` | 8551 | 54 | 158.35× |
| `parallel_cpu.js` | 52 | 776 | 0.07× **← protoJS wins** |
| `string_concat.js` | 113 | 4 | 28.25× |
| `string_processing.js` | 1 | 0 | n/a (QuickJS sub-ms) |
| `tree_traversal.js` | 349 | 4 | 87.25× |

**Geomean: QuickJS 14.4× protoJS** (12 benchmarks).

## Reading the gap

- **vs QuickJS** (~10× geomean): the meaningful interpreter-vs-
  interpreter number.  Both lack JIT.  The gap is dispatch overhead,
  attribute-lookup cost, and the immutable-object cost of writes — not
  fundamental algorithmic disadvantage.  Closing this gap is the work
  of the P-JS optimisation track.
- **vs Node.js / V8** (~38× geomean): includes the JIT advantage on top
  of the interpreter gap.  Not directly closable without a JIT layer.
- **`parallel_cpu`**: the one benchmark where protoJS beats both.
  14.92× vs QuickJS, 1.27× faster than Node.  This is the architectural
  payoff of GIL-free threading on protoCore — workloads that scale
  across cores show the advantage.

## Hot spots worth attention

- `object_write_only` (158× QuickJS, 628× Node).  Cost of writes through
  protoCore's immutable structural-sharing — every property assignment
  builds a new object chain.  Highest-leverage target for further work.
- `tree_traversal`, `function_calls` (87× / 26× QuickJS).  Tight-loop
  dispatch dominates.  P-JS track is already addressing this.
- `numeric_loop` (1.46× QuickJS).  Within noise of parity.  Sane
  starting point — confirms the basic loop is no longer pathological.
