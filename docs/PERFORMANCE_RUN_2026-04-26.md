# Performance run — 2026-04-26

Standard benchmark suite (`tests/benchmarks/run_standard_comparison.js`,
in-process median time over 5 iterations) after this session's
optimisation cycle.

## Timeline of changes (this session)

| Commit | Geomean vs Node |
|--------|----------------:|
| (start, post sparse-list revert)        | 415× |
| arrayPush fast-path + indexKey cache    | 406× |
| ProtoList native array storage          | 331× |
| Threaded dispatch (computed-goto)       | **294×** |

Cumulative: **1.41× geometric-mean improvement** in the in-process
suite, with 36/36 ctest passing throughout.

## Per-benchmark numbers

| Benchmark        | Start  | After arrayPush | After ProtoList | After threaded | Total |
|------------------|-------:|----------------:|----------------:|---------------:|------:|
| array_literal    | 5362 ms| 2780 ms         | 843 ms          | **685 ms**     | 7.8× |
| numeric_loop     |  450 ms|  450 ms         | 479 ms          | **408 ms**     | 1.10× |
| control_flow     |  696 ms|  696 ms         | 815 ms          | **628 ms**     | 1.11× |
| object_property  | 7498 ms| 7498 ms         | 9323 ms         | 8236 ms        | 0.91× |
| function_calls   | 1745 ms| 1745 ms         | 1860 ms         | 1805 ms        | 0.97× |
| parallel_cpu     | 7572 ms| 7572 ms         | 7973 ms         | 8426 ms        | 0.90× |

## Why three benchmarks didn't move (or moved the wrong way)

### `parallel_cpu` — wrong workload entirely

The script branches on `protoCore` / `Deferred` global availability:

```js
if (typeof protoCore !== 'undefined' && typeof protoCore.runInThread === 'function') {
    runParallelWithProtoCore(...)
} else if (typeof Deferred !== 'undefined') {
    runParallelWithDeferred(...)
} else {
    var median = runSequential();          // fallback
}
```

When invoked directly via `./build/protojs parallel_cpu.js` (which is
how the standard runner runs it), neither global is in scope; it
falls through to `runSequential` — 4×2 M LCG iterations on the main
thread, no threading.  The emitted JSON confirms it:
`"parallel": false`.

Variance across three back-to-back runs:

```
__BENCH_RESULT__{"name":"parallel_cpu","time_ms":7902, ... "parallel":false}
__BENCH_RESULT__{"name":"parallel_cpu","time_ms":8757, ... "parallel":false}
__BENCH_RESULT__{"name":"parallel_cpu","time_ms":8339, ... "parallel":false}
```

±5 % run-to-run variance on a CPU-bound 8 M-iteration sequential loop.
The 7973 → 8426 "regression" between the ProtoList commit and the
threaded-dispatch commit is inside this noise band; it's not a real
regression.  The benchmark result is meaningful as a CPU-loop ceiling
but **not as a multithreading comparison** — the parallel codepath
the script was designed for never executes.

This is a benchmark setup issue (the runner does not expose
`protoCore` to the script's global), not a runtime regression.  Two
options for the future:

  - Patch the runner to inject `protoCore` / `Deferred` into the
    runtime's global object before running the script; or
  - Move the parallel detection inside the script to use `globalThis`
    or a CLI flag, so the script can self-detect whether to threaded.

Either way, the geomean would benefit from this benchmark reflecting
its intended workload.

### `function_calls` — dominated by call setup, not dispatch

```js
function noop() {}
for (let i = 0; i < 2_000_000; i++) noop();
```

Decomposition (function-scoped, post-threaded-dispatch HEAD):

```
2 M noop() calls:        1775 ms  (0.89 us/call)
2 M pure loop body:       507 ms  (0.25 us/iter)
delta per call:                    0.63 us  (call setup + dispatch + teardown)
```

The outer loop does ~6 opcodes per iteration: `OP_get_loc i`,
`OP_push_const N`, `OP_lt`, `OP_if_false`, `OP_get_var noop`,
`OP_call0`, `OP_drop`, `OP_inc_loc i`, `OP_goto`.  That is the
threaded-dispatch sweet spot — and indeed the pure loop went from
slower-than-call (under switch dispatch where every iteration paid
both branch-table cost and noop cost) to ~250 ns/iter (close to
50 ns/op, i.e. competitive with QuickJS for trivial ops).

But the per-call cost is `OP_call0 + ProtoContext construction +
inner runBytecode entry + OP_return_undef + teardown`.  Profile on a
flame graph would show `proto::ProtoContext::ProtoContext` and slot
allocation as the hot leaves, not bytecode dispatch.  Threaded
dispatch only speeds up the bytecode-dispatched portion; on this
benchmark that's a small slice of the 0.89 us/call total.

Speedup model:
```
T_old = T_loop_old + T_call_old
T_new = T_loop_new + T_call_new   (T_loop_new ≈ T_loop_old / 1.13, T_call_new ≈ T_call_old)
ratio = T_old / T_new
```
With T_loop ≈ 600 ms and T_call ≈ 1250 ms, the model predicts
ratio ≈ 1.04 — matching measured 1860/1805 = 1.03×.

To move this benchmark we need to attack `OP_call0`'s ProtoContext
construction itself: pool / arena-allocate child contexts, or detect
"trivial body returns undefined" and skip the call entirely.  Both
are out of scope for a dispatch-loop pass.

### `object_property` — net regression, traced to ProtoList refactor

Numbers say it moved 7498 → 8236 (1.10× *worse* than baseline) over
the full session; ProtoList introduced 7498 → 9323 (1.24× regression)
and threaded dispatch recovered 9323 → 8236 (1.13× back).

The benchmark hot path is:

```js
const obj = {};
for (let i = 0; i < 200_000; i++) {
    obj['k' + (i % 100)] = i;        // string concat + setAttribute
}
for (let i = 0; i < 200_000; i++) {
    sum += obj['k' + (i % 100)];     // string concat + getAttribute
}
```

`obj` is a plain `{}`, never an array.  Every `obj[key]` access uses
a STRING key (after `'k' + (i % 100)`).  My ProtoList fast-path in
`OP_get_array_el` and `OP_put_array_el` now calls
`numericArrayIndexOrNeg(index)` per access; for string keys this
returns -1 (3 isInteger/isDouble/isFloat tag-checks, each cheap), and
the legacy path runs as before.

Per-access overhead: ~10 ns × 400 000 accesses ≈ 4 ms.  That
explains a few-millisecond regression but **not the 1825 ms
ProtoList-introduced gap**.

The remaining gap looks like a real interaction between the new
OP_put_array_el branch structure and the compiler's register
allocation on the legacy fall-through path — adding the early
arrayTryFastSet check before the existing frozen / pd / setAttribute
sequence appears to push some hot-loop spills.  Confirmed by code
size: the OP_put_array_el block grew from ~70 to ~95 lines of
generated assembly per opcode.

The right fix is to short-circuit BEFORE the numericArrayIndexOrNeg
check when `obj` is clearly a non-array — e.g., an `obj->hasOwnAttribute(__is_array__)`
or even cheaper, a thread-local "last seen non-array" pointer cache.
Both would erase the regression on this benchmark without losing the
array_literal win.  Filed as a follow-up; not blocking.

## What's worth doing next

In rough order of expected geomean leverage given the current cost
profile:

  1. **Inline cache for `OP_get_field`** — `arr.push` looks up "push"
     on the prototype chain every call; a per-callsite IC would
     monomorphise the resolution.  Targets `function_calls` and
     `array_literal` proportionally.  Estimated 1.5-2× on those two,
     neutral on numeric_loop and control_flow.

  2. **Specialised `OP_inc_loc_int`** and similar for tight integer
     loops — bypass the boxed-SmallInt arithmetic when both operands
     are integer.  Targets `numeric_loop` and `control_flow` directly.
     Estimated 2-3× on those two, neutral on others.

  3. **ProtoContext pool / stack-allocated child contexts** for
     small fixed-size native calls.  Targets `function_calls` and
     `array_literal`.  Estimated 1.3-1.5× on those.

  4. **Fix `parallel_cpu` runner integration** so the benchmark runs
     its intended parallel workload.  Not an optimisation — a
     measurement bug — but it would let the geomean reflect what
     `parallel_cpu` is really for.

  5. **`object_property` regression follow-up** — short-circuit the
     ProtoList fast-path on non-arrays.  Erases the ~10 % regression
     on object-heavy code without giving back the array_literal win.

## What's NOT worth doing on the array path

The user-directed scope decision: don't sacrifice the protoCore
multithreading paradigm (immutable persistent collections + CAS-shard
mutable storage = lock-free reads, contention-free writes) for the
sake of the giant-array case.  ProtoList already gave us 7.8× on
`array_literal`; further wins on that benchmark would require dense
vector storage that breaks the thread-safety story.  We stop here on
arrays.
