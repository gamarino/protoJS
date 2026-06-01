# Balanced benchmark suite — first run, 2026-05-31

Four benchmarks added to `tests/benchmarks/standard/` to test the
hypothesis that **structural-sharing data structures should
outperform flat-buffer engines on operations where the structural
advantage applies** (string concat of large chunks, repeated
doubling, immutable history of a growing list, string insertion in
the middle).

The hypothesis is **not supported by the data**.  The benchmarks
instead reveal a large implementation overhead per operation in
protoJS that more than negates the theoretical structural-sharing
advantage at the sizes that fit in memory on this hardware.  This is
the most important finding of the session — more important than the
geomean ratio we were trying to refine — because it changes which
optimisations matter and in what order.

## Setup

- Binary: `build_release/protojs` rebuilt 2026-05-31 against
  `libprotoCore.so.1.2.0`.
- GC suppressed: `PROTOCORE_GC_CONTEXT_THRESHOLD=1000000000`.
- Reference engines: Node.js v22.17.0, QuickJS minimal
  (`tests/benchmarks/qjs_minimal`).
- Methodology: each benchmark prints `__BENCH_RESULT__<json>` with
  median of 5 internal runs.

## Result summary

| benchmark | Node (ms) | QuickJS (ms) | protoJS (ms) | protoJS / Node |
|---|---:|---:|---:|---:|
| `string_repeated_doubling` (balanced concat, 200 × 18 doublings) | 42 | 1 | 2224 | 53× |
| `string_concat_large_chunks` (1 × 200 × 200-char chunks) | 0 | 0 | **timeout > 60 s** | ≫ 1000× |
| `list_snapshot_history` (200 snapshots of growing array) | 0 | 1 | 30 | ≫ 30× |
| `string_insert_middle` (100 inserts × 50 chars in 1 KB base) | 0 | 0 | **runtime bug, blocked** | — |

(Node sub-millisecond cells reflect timer floor; the true ratios are
larger than reported.)

## Why this is the wrong way round

The benchmarks were designed to favour structural sharing on
arguments that hold conceptually:

- A flat-buffer engine doing `s = s + s` must memcpy the entire
  string on every doubling.  A rope just allocates one internal
  node.
- A flat-buffer engine doing `arr.concat([x])` must allocate and
  copy every element.  An AVL-backed list with structural concat
  shares the prior root.
- A flat-buffer engine inserting in the middle must memcpy both
  halves.  A rope splits, allocates O(log N) new internal nodes,
  and concatenates.

All true in theory.  In protoJS's current implementation the
constant per concat / per insertion is so large that the wall-time
saving from doing logarithmic work instead of linear work never
appears at any size that fits in memory.

### Measured per-concat cost

`string_repeated_doubling` (balanced case, `s + s` where always
`ha = hb`, so `strConcat` always takes the simple
`makeInternal(a, b)` path — ONE allocation per call) scales linearly
in `OUTER × DOUBLINGS`:

```
OUTER=10,   18 doublings → 105 ms   (~0.58 ms / concat)
OUTER=50,   18 doublings → 527 ms   (~0.59 ms / concat)
OUTER=100,  18 doublings → 1098 ms  (~0.61 ms / concat)
OUTER=200,  18 doublings → 2187 ms  (~0.61 ms / concat)
```

**~600 µs per concat** for the simplest possible rope-concat path
that allocates one cell.  Node memcpy for the same workload averages
~12 µs per concat (and is memory-bandwidth-bound, not algorithmic).
protoJS is **50× slower per concat than Node memcpy on the SAME
sized input** — exactly the regime where the rope is supposed to
win.

### Why the unbalanced case is worse

`string_concat_large_chunks` exercises the `s + smallChunk` pattern
where the rope becomes increasingly imbalanced.  `strConcat` then
takes the recursive-into-right-spine path:

```cpp
if (ha > hb + 1) {
    const auto* an = StringInternalNode::fromObject(a);
    const ProtoObject* new_right = strConcat(ctx, an->right, b);
    return avlRebalance(ctx, makeInternal(ctx, an->left, new_right));
}
```

Each concat recurses down the right spine to the appropriate height,
then rebalances all the way back up — O(depth) work per concat plus
O(depth) cells allocated.  At 600 µs/cell baseline this multiplies
the per-concat constant by `log N`, and `log N` × 600 µs × N reaches
the seconds-per-bench regime well before the workload becomes
interesting.

200 chunks of 200 chars total ≈ 40 KB of source data.  Node's
flat-buffer concat moves perhaps 4 MB through memory bandwidth
cumulatively — that takes ~150 µs.  protoJS times out at 60 seconds.

### What this reveals about the optimisation order

The earlier perf-driven plan (`tasks/perf_plan_2026-05-31.md`)
identified five optimisations.  This run sharpens which one
matters most:

- **P-JS-11 (rope tag-bit fast paths)** was scoped as a
  string-concat-local win (~1 % of total wall time aggregate).  The
  measurement here suggests the actual cost in the rope path is
  much larger than the aggregate suggested — because the existing
  `string_concat.js` benchmark does byte-by-byte concat where
  amortisation hides part of the per-concat overhead.  The balanced
  benchmarks expose the per-call cost as ~600 µs, of which the
  tag-dispatch overhead in `nodeHeight`, `byteCount`, `charCount`,
  `isStringInternalNode`, `isStringLeafNode` is a substantial
  fraction.  P-JS-11 is probably more important than the plan
  estimated.
- **P-JS-8 (allocation fast path)** matters even more if every
  rope concat allocates one cell minimum: the allocation chain
  cost is paid on every `+`.

### Honest framing of the structural-sharing argument

The argument "immutable structural sharing should win on operations
where flat-buffer engines pay O(N) per step" is **conceptually
correct and currently empirically unmeasurable**.  protoJS's
per-operation constant is high enough that the asymptotic
advantage never crosses over to wall-time advantage at sizes that
both engines can process.

This does NOT refute the architectural premise — it shows that the
P-JS optimisation track is **the prerequisite for the architectural
premise to materialise as observable benefit**.  Until the per-op
constant comes down by roughly an order of magnitude, the rope's
algorithmic win remains hypothetical.

The next published comparison should explicitly state this:

- vs Node:    ~38× geomean on the existing (flat-favourable) suite,
              honestly attributable to JIT + flat-buffer memory model.
- vs QuickJS: ~10× geomean on the same suite,
              the meaningful interpreter-vs-interpreter number.
- **balanced suite**: protoJS LOSES on every benchmark by 30× to
  > 1000×, even where the structural argument predicts a win.  The
  gap is per-operation overhead, not algorithmic.

The "geomean tells half the story" framing is still useful for
publication — but the half it tells is **worse**, not better, than
the headline numbers suggest.  That is also worth publishing
honestly.

## Separately discovered protoJS bugs

While writing the benchmarks two bugs surfaced that were not the
target of this session but should be tracked.

### `String.prototype.length` returns undefined

```
protojs -e "var s='abc'; console.log(s.length)"  →  undefined
```

Documented in `src/JSONPolyfill.h` (the JSON polyfill works around
it by iterating with `charAt(i)` until empty-string sentinel).
Anything that needs string length in the standard suite would have
to track it externally.

### `return string.slice(...)` from a function returns undefined

```js
function f5() { let b = 'xxx'; return b.slice(0, 2); }
f5()  // → undefined (expected: 'xx')
```

`s.slice(0, 2)` works correctly at top-level eval, but the return
value is lost across the function boundary.  Bisected from
`string_insert_middle.js` which uses a `buildBlock` helper that
returns `b.slice(0, targetLen)`.  Blocks that benchmark entirely.

Both bugs are separate from the per-op overhead finding and warrant
their own tracking.

## Next steps

1. The four balanced benchmarks are committed to the standard suite
   so future P-JS-N work can re-measure against them.  Sizing is
   small because protoJS times out at production-realistic sizes —
   that sizing should be **revised upward** after P-JS-8/11 land,
   not held as the canonical methodology.
2. Update `tasks/perf_plan_2026-05-31.md` to reflect the elevated
   importance of P-JS-11 (rope tag-dispatch elimination) given that
   `strConcat`'s simple path is paying 600 µs / call largely in
   accessor overhead.
3. File the two separate runtime bugs (`String.prototype.length` and
   the slice-return path) as their own work items.  They were not
   the goal of this session but they are real and visible.
4. Resist the temptation to retitle the existing suite "favourable
   to mutable engines" and call it a day.  The honest framing is
   that protoJS currently loses on both flat-favourable AND
   structural-favourable workloads, and the gap is implementation
   work that is identified and tractable.

The conceptual case for protoJS's architecture remains valid (the
GIL-free `parallel_cpu` win is the existing proof point); the
performance case is currently undefendable on single-thread compute
across both workload shapes, and that is what the optimisation
track needs to address before any "structurally favourable
workloads where protoJS wins" public claim can be supported by
measurement.
