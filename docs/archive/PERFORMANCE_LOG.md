# Performance Log

> **Historical document.** Dated record kept for reference; not maintained and may not match the current code. Current documentation: [docs/README.md](../README.md).

**2026-06-16 — Scope-as-chain refactor landed.**  Five commits
collapse the interpreter's bespoke variable-resolution machinery onto
protoCore's native prototype chain.  Functions, methods, classes, and
nested closures all resolve uniformly: a function-object is a child of
the scope where it was defined, the call frame is a lazy child of the
function-object + moduleScope, and `__captured_cells__` (the parallel
SparseList sidecar) is gone — closure cells are reached via plain
`getAttribute(name, chain=true)` walks, memoised by the AttributeCache.
The model the user codified — "defining a method or class means adding
to the module an attribute that is an object derived from that same
module; everything resolves automatically" — is now what the interpreter does.

Pipeline of the cycle's commits (chronological):

1. `e1e4cb82b` — **module-scope split**: top-level user bindings live
   in a small mutable whose parent is the stdlib/built-ins root.
   `put_var` on a top-level `let` drops from ~12 cells per write
   (log₂(200)-deep AVL rebuild on the global mutable) to ~6.
2. `6e01321a5` — function-objects get `moduleScope` as a second
   parent (Function.prototype stays head, so instanceof / call /
   bind / apply / getPrototypeOf(f) are unchanged).
3. `6c0c400d4` — fix `set_loc_uninitialized` / `put_loc_check` /
   `set_loc_check` to honour closure cells (had been raw `setSlot`,
   overwriting the cell pointer on the very next `let x` TDZ-init).
4. `7cdb7a21c` — frame-as-object infrastructure (lazy mutable
   `frameObj` with `parents = [activeFunc, moduleScope]`, only
   materialised on first capture) + outer-`frameObj` as third
   parent of each closure-fn + fix `get_loc_check` cell-dereference
   (was pushing the cell wrapper as the value of `n` after `let n
   = 0; inc(); return n`, observable as `typeof n === "object"`).
5. `6e93cb536` — **eliminate `__captured_cells__`**: every captured
   cell is published on the outer's `frameObj` under its source
   name; `populateClosureCellsFromInstance` resolves it via the
   regular `getAttribute(name, chain=true)` walk.
6. `9f45980ad` — **arrayPush skips the per-push length writeback**:
   OP_get_length synthesises `arr.length` from `__elements__.size()`
   for dense arrays.  Saves one SparseList rebuild and one
   AttributeCache invalidation per push (array_literal: -18%).
7. `bef35701d` + `ee6c5cc41` — **symbol-contract audit (52 sites in
   ProtoInterpreter / ArrayPrototype / ObjectPrototype hot paths)**:
   replaced rope-rebuilding `fromUTF8String("X")->asString` patterns
   that forced protoCore's defensive `SymbolTable::lookupByContent`
   on every read with `JSSymbols::xxxx(ctx)` / pre-interned
   `module->closureSymbols`.  Validated via `PROTOCORE_TRUST_SYMBOLS=1`
   experiment (opt-in env flag in protoCore that returns PROTO_NONE
   for STRING-tagged names at `getAttribute` entry — exposes embedder
   violators by perf regression rather than test failure).  Audit
   cleared the combined-suite regressions the flag had originally
   surfaced; standard suite gains -5 % geomean with flag on.
8. `63e0b6354` — **single-shot debug-check init** in `getSlot` /
   `setSlot` helpers (`debugSlotsEnabled` / `debugBindEnabled`).
   The lazy "init on first call" pattern was branching at every
   inlined call site (568 sites × millions of calls/bench).
   `perf record` flagged it at 3.29 % of `numeric_loop`'s CPU.
   Replaced with `static const bool` initialised once at process
   start; helpers marked `[[gnu::always_inline]]`.  Latent issue
   unrelated to symbols — surfaced by re-profiling during the
   audit experiment.
9. `99aee28e7` — **JSSymbols magic-static**: `DEFINE_SYMBOL` used
   `std::once_flag` + `std::call_once` + lambda capturing ctx,
   which the compiler could not inline through the lambda
   boundary.  Every `JSSymbols::xxxx(ctx)` call (and there are
   many in the OP_get_array_el / OP_put_field / dispatch paths)
   went through a non-inlined function with an atomic load and a
   lambda thunk dispatch.  The JSSymbols family added up to
   ~3 % of CPU on object_read_only (hasAccessorProps 0.96 %,
   isSymbol 0.86 %, primitiveValue 0.93 %, etc.).
   Replaced with the C++11 thread-safe static initialisation
   pattern (`static const T s_sym = expr;`), which the compiler
   reduces to a guard-byte test + load and can fully inline.
   Post-fix: JSSymbols::* entries disappear from the perf top-20,
   object_read_only drops from 87 → 79 ms (−9 %).  Same family
   of bug as 63e0b6354 — a "harmless" lazy-init pattern paying
   cycles at every call site.

#### Standard In-Process Suite — current reading — vs Node.js 22 / V8 / vanilla QuickJS

`build_release/protojs` against Node 22.17.0 and `qjs_minimal_release`
(QuickJS rebuilt with `-O3 -DNDEBUG`).  All times are the bench's own
in-process measurement (median of 3 outer runs, each averaging 5
inner iterations); we do not report wall-clock to avoid startup-cost
contamination.

| Benchmark                | Node  | QuickJS | protoJS | × Node | × QuickJS |
|--------------------------|------:|--------:|--------:|-------:|----------:|
| **array_literal**        |  2 ms |    6 ms |  217 ms |   109× |  **36×**  |
| **control_flow**         |  4 ms |   49 ms |  183 ms |    46× |  **3.7×** |
| **function_calls**       |  1 ms |   10 ms |  202 ms |   202× |  **20×**  |
| json_transform           |  1 ms |    3 ms |  137 ms |   137× |     46×   |
| json_transform_small     |  0 ms |    0 ms |   14 ms |  parity|   parity  |
| json_transform_tiny      |  1 ms |    0 ms |    9 ms |     9× |   parity  |
| list_snapshot_history    |  0 ms |    1 ms |   18 ms |  parity|     18×   |
| **numeric_loop**         |  1 ms |   38 ms |   76 ms |    76× | **2.0×**  |
| **object_property**      | 37 ms |   79 ms |  559 ms |    15× |   **7.0×**|
| **object_read_only**     |  1 ms |    6 ms |   75 ms |    75× | **12.5×** |
| object_write_only        | 14 ms |   62 ms | 1737 ms |   124× |     28×   |
| **parallel_cpu**         | 41 ms |  804 ms |   52 ms | Node 1.3× | **protoJS 15×** |
| string_concat            |  1 ms |    5 ms |  116 ms |   116× |     23×   |
| string_concat_large_ch.  |  0 ms |    0 ms |    0 ms |  parity|   parity  |
| string_insert_middle     |  0 ms |    0 ms |    1 ms |  parity|   parity  |
| string_processing        |  0 ms |    0 ms |    8 ms |  parity|   parity  |
| string_repeated_doubling | 40 ms |    1 ms |    1 ms | **protoJS 40×** | parity |
| **tree_traversal**       |  1 ms |    4 ms |  159 ms |   159× |  **40×**  |

**Geometric mean (in-process time, 11 single-thread benches):**

- **protoJS / QuickJS = 15.2 ×**
- **protoJS / Node    = ~95 ×**

**Headline single-bench wins from this cycle (vs. the pre-cycle 2026-06-16 baseline):**

- `function_calls`: 27.5× QuickJS → **18×** (−34%) — module-scope split
  collapsed the `put_var` cost on `state = work(state)` from ~12
  cells/op to ~6, and the chain-walk closure model skipped the
  __captured_cells__ SparseList rebuild on every fclosure.
- `numeric_loop`: 1.3× → **2.4× QuickJS**.  This bench's reading is
  noisier than the others (small absolute times; sensitive to first-
  iter init) — see the QuickJS 50→38 ms swing across the cycle's
  measurements.  Real-side: the debug-check fix in `getSlot`/`setSlot`
  reduced ~3 % of `runBytecode`'s share that was pure pre-init
  bookkeeping.
- `tree_traversal`: 71× QuickJS → **48×** (−32%) — closures-as-chain
  removed a per-call SparseList rebuild and let AttributeCache memoise
  every closure variable lookup.
- `object_read_only`: stable at ~12.5× QuickJS (was already brought
  down from 63× earlier the same day via the accessor-gate fix).

**Optional `PROTOCORE_TRUST_SYMBOLS=1` flag (experimental, NOT default):**

Adds an env-var gate that treats STRING-tagged `name` at
`getAttribute` entry as definitely-absent — the embedder contract
is "always pass an interned SYMBOL".  Net effect on the standard
suite measured at **30-run trimmed mean (middle 14 of 30) on an
unloaded system**: ratio 0.974, i.e. **−2.6 % geomean** with the
flag on (11-bench single-thread set).  Heterogeneous bench by bench:

| Bench                | OFF (ms) | ON (ms) | Δ     |
|----------------------|---------:|--------:|------:|
| **control_flow**     |    223   |   185   | **−17 %** |
| object_read_only     |     81   |    75   |  −6 % |
| string_concat        |    115   |   112   |  −3 % |
| tree_traversal       |    160   |   155   |  −3 % |
| **object_write_only**|   1828   |  1782   |  −3 % |
| numeric_loop         |     77   |    75   |  −2 % |
| array_literal        |    222   |   230   |  +4 % |
| function_calls       |    193   |   199   |  +3 % |
| object_property      |    576   |   582   |  +1 % |
| json_transform       |    144   |   143   |    =  |
| list_snapshot_hist.  |     18   |    18   |    =  |

Geomean ratio (11 benches): **0.974 = −2.6 %** with flag on.

Earlier reports claimed a bimodal distribution under the flag
(object_read_only: 82–92 ms × 16 runs and 105–161 ms × 14 runs).
That was an artefact of **CPU contention from background processes
during the measurement window**, not a code-path bifurcation.
Re-running on an idle system gives a uniform distribution
(76–88 ms, IQR ≈ 5 ms) — the supposed "audit signal" was system
noise.  The honest takeaway: the flag's effect is mostly positive
but modest; promoting it to default requires confidence that the
+3 / +4 % regressions on `function_calls` / `array_literal` are not
real cycle regressions, which the present measurement cannot
distinguish from noise either.

The unambiguous wins of the cycle came from the **lazy-init
discoveries** that the audit experiment triggered via re-profiling:

- `63e0b6354` — `debugSlotsEnabled` in every `getSlot` / `setSlot`
  inline = 3.29 % of `numeric_loop`'s CPU on a useless atomic check.
  Single-shot init dropped numeric_loop by ~3 %.
- `99aee28e7` — `JSSymbols::xxxx(ctx)` getters used
  `std::call_once + lambda` that the compiler could not inline.
  JSSymbols::hasAccessorProps / isSymbol / primitiveValue summed to
  ~3 % CPU on object_read_only.  Magic-static replacement dropped
  object_read_only by ~7 % and the JSSymbols family vanished from
  the perf top-20.

Both bugs predate the audit and are independent of the symbol
contract.  They were latent for months and surfaced only because
the trust-symbols experiment forced a fresh profile session.  The
lesson is methodological: an "experiment that does not validate
its own hypothesis" can still pay for itself by exposing
unrelated bugs that prior profiles had normalised as background.

#### Where protoJS wins by architecture

These are not closeable by tuning the interpreter — they are direct
properties of the protoCore object model that single-threaded /
flat-string engines cannot match:

- **`parallel_cpu` — 18× faster than QuickJS, edges Node.**  Four
  CPU-bound worker threads on real OS threads via protoCore's
  GIL-free runtime.  QuickJS is single-threaded; Node 22's
  `worker_threads` get close on this micro but pay IPC and message
  serialisation overhead.  GIL-free landing at the benchmark level,
  not just at the manifesto level.
- **`string_repeated_doubling` — 15× faster than Node.**  Rope-based
  concatenation in protoCore vs Node's flat-string rebuild — the
  spec-correct `s = s + s` loop in Node is O(N²) in time and
  memory; ours is O(N log N) by structural sharing.

#### The dominant interpreter cost — and how this cycle attacked it

Every single-threaded interpreter bench in the table that runs
slower than ~50× QuickJS is currently allocation-bound, not
dispatch-bound.  The diagnostic signature is consistent:

```
/usr/bin/time -v ./protojs bench.js
  → RSS  > 1 GB,  Minor page faults > 100 K,
    System time roughly equal to User time
```

The bench is being measured against the kernel's
zero-fill-on-first-touch path, not against the interpreter.  Each
~100 protoCore cells the hot path allocates per op = ~6 KB of
freshly-faulted memory pages.

Recent fixes target exactly this signature:

- `L_OP_call`'s `__is_class_ctor__` probe used a per-call
  `fromUTF8String` that built a fresh ProtoString rope; interned via
  `JSSymbols::isClassCtor`.  `function_calls`: 1.4 GB RSS / 348 K
  faults  →  24 MB / 4.7 K.
- `L_OP_get_array_el` (string-key AND numeric-index branches),
  `resolvePutFieldOOP`, and `invokeGetterIfPresentFast` each built
  `__get_<key>__` / `__set_<key>__` rope strings on every property
  access.  Two-tier `__has_accessor_props__` check (OWN before
  chain-walk, since Object.prototype's `__proto__` accessor sets the
  chain flag globally).  `object_property` 12 GB → 1.4 GB;
  `object_read_only` 3.4 GB → **24 MB** (numeric-index branch
  fix landed in `347288441` — was still allocating ~49 cells per
  `arr[idx]` after the string-key fix).
- `resolvePutFieldOOP`'s length-truncation post-check identity-matched
  `JSSymbols::length` instead of `toUTF8String + std::string ==`;
  `OP_define_field`'s isNumericKey decision cached per-thread by
  key pointer.  Tree-shaped object construction got cheaper.
- `Array.prototype.concat` native fast path was disabled on every
  call by the same Object.prototype chain-walk taint, AND its empty-
  dst-list precondition was failing because arraySpeciesCreate
  pre-sized the result list.  Fix both:
  `list_snapshot_history` 327× QuickJS → 27× (12× speedup); RSS
  1.4 GB → 155 MB.
- `ProtoString::isInlineString()` exposed as public protoCore API
  so `ensureInterned` can pointer-identity-match short ASCII keys
  without the `toUTF8String` + `createSymbol` round-trip.

- The BytecodeSpecialiser (sprint-11 port from protoPython) ships
  three fused super-instructions and is on by default
  (`PROTOJS_SPECIALISER=off` to disable):
  - `OP_PROTO_ACC_LOC8_LOC8 dst src` — `local[dst] += local[src]`
  - `OP_PROTO_LT_LOC8_LOC8_JFALSE a b T` — `if local[a] < local[b]
    fall through; else jump T`
  - `OP_PROTO_LT_LOC_VAR_JFALSE loc varIdx T` — `if local[loc] <
    closureVar[varIdx] fall through; else jump T` — the typical
    `for (let i = 0; i < N; i++)` shape where N is a module-level
    `const`.
  All three accept the TDZ-tracked (`_check`) source forms and
  inline a sentinel pointer-compare for correctness.  Together they
  closed `numeric_loop` from 4.6× to 1.7× QuickJS — the same
  coherence point the protoPython sprint-11 work landed at vs
  CPython 3.14t.

The remaining outliers in the table (`tree_traversal` 74×,
`string_concat` 27×) are dominated by inherent rope / object-
construction allocation patterns rather than uninterned-string
storm.  `string_concat`'s `s += 'x'` profile shows 50 %+ of CPU in
`StringInternalNode` rope operations — the rope build is the
benchmark, not an accidental cost.  Further wins here need
protoCore-side work (batched setAttributes, mutable string
builder), not embedder cleanups.

#### Specialiser micro-result

The optional sprint-11 peephole specialiser
(`PROTOJS_SPECIALISER=off|nop|compact`, default `off`) cuts targeted
loop micros by **−12 %** at the `compact` setting (`var`-declared
`intSum(5e6)`: 539 ms / 498 ms / 472 ms).  It does not move the
standard-suite geomean because the suite is dominated by
property-access patterns the fused opcodes don't cover.

#### Repro

```bash
# Build
cmake --build build_release      # ctest 33/33 must be green

# Run vs QuickJS
node tests/benchmarks/run_standard_comparison_quickjs.js

# Run vs Node
node tests/benchmarks/run_standard_comparison.js

# A/B the specialiser modes on a single loop
echo 'function f(n){var s=0,i=0;while(i<n){s+=i;i+=1}return s}let t=Date.now();let r=f(5_000_000);console.log(r, Date.now()-t)' > /tmp/loop.js
                                ./build_release/protojs /tmp/loop.js   # off
PROTOJS_SPECIALISER=nop          ./build_release/protojs /tmp/loop.js   # nop-pad
PROTOJS_SPECIALISER=compact      ./build_release/protojs /tmp/loop.js   # compact+remap
PROTOJS_SPECIALISER_DIAG=1       ./build_release/protojs /tmp/loop.js   # diagnostics
PROTOJS_SPECIALISER_DUMP=1       ./build_release/protojs /tmp/loop.js   # raw bytecode dump
```

---

**Honest baseline — 2026-06-07 (late, after structural cleanup)** —
3-round median, same `libprotoCore` build from the
`digression-attr-cache-padding` branch.  This is a continuation of
the morning baseline below: a static analysis identified seven
patterns where protoJS was reimplementing logic that protoCore
already exposes, plus one interpreter-side wrapper that was paying
function-call overhead for what should be a single memory access.
Fifteen commits later (no protoCore changes), geomean against
QuickJS is **17.65 ×** — below the 2026-06-01 baseline of 21.0 ×.

> **Structural cleanup landed this cycle (2026-06-07 late)**
>
> Each commit follows one rule: *if protoCore exposes the primitive,
> call it; if it exposes a flag, set it; if it exposes a fast path,
> take it.*  No reimplementation of what already exists.
>
> 1. **`4b99011c`** — `arrayPush` calls `ProtoList::appendLast` in a
>    tight loop with `getElements`/`setElements` hoisted out.  Per
>    100 K-push iteration: 1 getAttribute + 100 K `appendLast` + 1
>    setAttribute (was: 100 K × {3 attribute ops + arrSet ceremony}).
>    **`array_literal` 2315 → 199 ms (−91 %)** — fully recovered
>    versus the 2026-06-01 pre-regression baseline of 195 ms.
> 2. **`9fff940c`** — `arrayShift` / `arrayUnshift` use
>    `ProtoList::removeFirst` / `appendFirst` directly when the chain
>    has no inherited indexed setters and no accessors.  The O(N) spec
>    walk (Get + Set per element) collapses to one O(log N) tree
>    operation.
> 3. **`47e269e1`** — `arraySlice` → `ProtoList::getSlice`.  One
>    O(log N) tree-splice instead of O(N) arrGet +
>    arrayCreateDataPropertyOrThrow per element.
> 4. **`bf12c184`** — `arrayConcat` plans-then-extends: build a vector
>    of (spread?, list) pairs, then `extend` each spreadable and
>    `appendLast` each non-spreadable.  Fallback path preserved
>    verbatim.
> 5. **`c73cb574`** — `arraySplice` assembles prefix + inserts + suffix
>    via `getSlice` + `appendLast` + `extend`.  Removed array is a
>    single `getSlice` of the deleted range.
> 6. **`1b5a527c`** — `arrayIncludes` / `arrayIndexOf` /
>    `arrayLastIndexOf` walk `__elements__` via `ProtoListIterator`
>    instead of arrGet per index.  Eliminates the per-element
>    `__get_<i>__` / `__set_<i>__` rope construction.
> 7. **`5785108d`** — `Object.keys` / `values` / `entries` skip the
>    per-key `__pd_<key>__` enumerable probe AND the `__get_<key>__`
>    accessor probe when the per-target flags
>    `__has_nonwritable_props__` / `__has_accessor_props__` are absent.
> 8. **`b989e88a`** — `runBytecode` passes a 256-slot stack buffer to
>    `ProtoContext` as `externalSlots`.  protoCore documents this as
>    "zero heap cost" — every JS function call WAS doing
>    `new const ProtoObject*[N]` + `delete[]`.
>    `ProtoContext::ProtoContext` disappeared from the top-15 profile.
> 9. **`6e40d219`** — bind callee args directly from the parent's
>    stack slice, skipping `argsList->getAt(i)` per arg (the AVL walks
>    on a tree we just built from the same slice).
> 10. **`3ed66a04`** — `ProtoBytecodeModule::usesArguments` flag,
>     computed once at module load by scanning for `OP_special_object`
>     kind=0/1, `OP_rest`, `OP_init_ctor`.  When false, the per-call
>     `pContext->newList(argc, slice)` is skipped — one fewer cell
>     allocation per JS-to-JS call.
> 11. **`f4438ebb`** — arithmetic Integer fallback paths delegate to
>     `ProtoObject::add` / `subtract` / `multiply` / `modulo`.  Fixes
>     a silent `int64` truncation bug (`9007199254740990 + 3` used to
>     truncate; now promotes to `LargeInteger` via `TempBignum`).
>     The SmallInt inline fast path stays — that's the hot case.
> 12. **`e02ec70b`** — `collectOwnKeys` walks the own-attributes
>     SparseList via `ProtoSparseList::processElements` callback.
>     The `getIterator + advance()` loop was allocating an iterator
>     wrapper cell per step (`implAsObject` call); the callback path
>     walks SmallSparseList inline pairs directly and reuses the
>     internal iterator without wrapper allocations for the AVL form.
> 13. **`ddf7f82d`** — `stackPush` / `stackPop` / `stackTop` / `getSlot`
>     / `setSlot` + auxiliaries marked
>     `[[gnu::always_inline]] static inline`.  Pre-fix profile on
>     `control_flow`: 5 helpers totalled ~15.5 % of CPU as real
>     function calls.  Verified via `nm`: all helpers eliminated from
>     the binary.  `control_flow` 252 → 226 ms (−10 %).
>     Three documented hang causes from the prior macro attempt are
>     explicitly guarded against (proper `ctx` propagation, saturating
>     doubling on `(idx+1)*2` overflow, preserved NULL guards) — see
>     memory `feedback_protojs_runbytecode_macros_caution.md`.

#### Standard In-Process Suite — vs Node.js 22 / V8 / vanilla QuickJS

3-round median.  Same `build_release/protojs`, same Node 22,
`qjs_minimal_release` (QuickJS rebuilt with `-O3 -DNDEBUG`).

| Benchmark                  |    Node |    QuickJS |   protoJS |   Node × |   QuickJS × |
|----------------------------|--------:|-----------:|----------:|---------:|------------:|
| array_literal              |    2 ms |       5 ms |    203 ms |    102 × |       41 ×  |
| control_flow               |    4 ms |      43 ms |    226 ms |     57 × |       5.3 × |
| function_calls             |    1 ms |       8 ms |    216 ms |    216 × |        27 × |
| json_transform             |    1 ms |       3 ms |    122 ms |    122 × |        41 × |
| json_transform_small       |    0 ms |       0 ms |     13 ms |      —   |        —    |
| list_snapshot_history      |    0 ms |       1 ms |    269 ms |      —   |       269 × |
| numeric_loop               |    1 ms |      32 ms |    125 ms |    125 × |       3.9 × |
| **object_property**        |   35 ms |      64 ms |    561 ms |     16 × |       8.8 × |
| **object_read_only**       |    1 ms |       5 ms |    103 ms |    103 × |        21 × |
| **object_write_only**      |   12 ms |      52 ms |   1275 ms |    106 × |        25 × |
| **parallel_cpu**           |**40 ms**|  **730 ms**|  **52 ms**|**Node 1.3 ×**|**protoJS 14.0 ×**|
| string_concat              |    1 ms |       5 ms |     99 ms |     99 × |        20 × |
| string_insert_middle       |    0 ms |       0 ms |    237 ms |      —   |        —    |
| string_processing          |    0 ms |       0 ms |    231 ms |      —   |        —    |
| string_repeated_doubling   |   36 ms |       1 ms |   2151 ms |     60 × |      2151 × |
| tree_traversal             |    1 ms |       4 ms |    298 ms |    298 × |        75 × |

**Geometric mean (12 benches where all three engines > 0 ms):**
- **protoJS / Node = 66.6 ×**   (was 132 × on 2026-06-06 morning, was 58.5 × on 2026-06-01)
- **protoJS / QuickJS = 17.65 ×** (was 34.4 × on 2026-06-06 morning, was 21.0 × on 2026-06-01)
- QuickJS / Node = 3.77 ×

#### Recovery vs the 2026-06-06 morning baseline

Geomean today / 2026-06-06 morning = 0.50 ×  — **protoJS is ~50 %
faster across the standard suite than the morning snapshot**, and
**~16 % faster** than the 2026-06-01 pre-regression baseline (which
was 21.0 × QuickJS; we are at 17.65 × now).

| Benchmark                  | 06-06 (ms) | 06-07 (ms) |        Δ |
|----------------------------|-----------:|-----------:|---------:|
| **array_literal**          |       2315 |        203 | **−91 %** |
| **object_write_only**      |       9554 |       1275 | **−87 %** |
| **object_property**        |       2793 |        561 | **−80 %** |
| **object_read_only**       |        394 |        103 | **−74 %** |
| list_snapshot_history      |        324 |        269 |     −17 % |
| string_processing          |        251 |        231 |      −8 % |
| numeric_loop               |        124 |        125 |       +0 % |
| string_concat              |        101 |         99 |      −2 % |
| tree_traversal             |        308 |        298 |      −3 % |
| parallel_cpu               |         52 |         52 |       ±0 % |
| control_flow               |        239 |        226 |      −5 % |
| function_calls             |        213 |        216 |      +1 % |
| json_transform             |        199 |        122 |     −39 % |
| string_repeated_doubling   |       2106 |       2151 |      +2 % |
| string_insert_middle       |        236 |        237 |      ±0 % |

**Honest framing.**  17.65 × QuickJS is *better than the 2026-06-01
baseline*, but still ~3 × off the < 5 × goal.  The remaining gap is a
mix of:

  - Closure-cell `__cv__` writes for every let/const at module scope
    (200 K writes per `function_calls` iteration = 200 K SmallSparseList
    snapshots — structural to protoCore's immutable model)
  - `string_repeated_doubling` and `string_insert_middle` bottleneck
    in protoCore-side rope construction
  - Doubled prototype walk (`t_jsProtoMap` parallel to protoCore parents)
    that this cycle did NOT address

`parallel_cpu` still wins **14 × against QuickJS**, **77 % of V8's
JIT'd throughput**.  The GIL-free architectural payoff is intact.

> **Cycle summary**: thirteen commits, none touching protoCore,
> reduced the protoJS-vs-QuickJS geomean from **33.9 ×** (start of day
> 2026-06-06) to **17.65 ×** — a 48 % improvement attributable
> entirely to *using protoCore primitives instead of reimplementing
> them*.  The architectural lesson is preserved in the memory
> `feedback_protocore_cache_for_stable_mutables.md` and now also
> `feedback_protojs_runbytecode_macros_caution.md`.

Raw rounds: `tests/benchmarks/results/three-way-rounds-2026-06-07b.txt`.
Raw JSON: `tests/benchmarks/results/node_quickjs_comparison.json`.

---

**Prior baseline — 2026-06-07 (early)** (in-process median time, protoJS built
in pure Release mode against the same `libprotoCore` from the
`digression-attr-cache-padding` branch as 2026-06-06).  This run lands
on top of the 2026-06-06 snapshot and reflects a **five-commit
structural fix** that recovers the entire ~78% geomean regression
introduced by the 10 Array cleanup packages.

Same sampling protocol as 2026-06-06: 3 outer × 5 inner = 15 samples
per cell, median reported.  No CPU pinning; concurrent GC stays on its
own core.

> **Structural fix landed this cycle (2026-06-07)**
>
> Root cause: every Array cleanup package added one or more
> spec-mandated probes behind freshly-constructed `ProtoString` ropes —
> `ctx->fromUTF8String("__pd_length__")`, `"__set_<idx>__"`,
> `"__pd_<key>__"`, `"__get_<key>__"`, etc. — on the per-call hot path.
> Cumulative: **+29 fresh `fromUTF8String` sites in `ArrayPrototype.cpp`
> alone since 2026-06-01**, 134 total across the codebase.  100K
> `arr.push(i)` in `array_literal` constructed ~300K throwaway ropes
> per iteration; 200K `obj[key]=v` writes in `object_property`
> constructed ~600K.  All wasted on benches with no monkey-patching.
>
> 1. **`51819174`** — strong-intern 134 sites of `__pd_length__` /
>    `__pd_name__` / `__pd_constructor__` / `__pd_message__` /
>    `__pd_size__` / `__is_symbol__` / `__fields_init__` via JSSymbols
>    DEFINE_SYMBOL entries.  Mechanical rewrite via two regex patterns.
>    `array_literal` 2315 → 1660 ms (−28%).
> 2. **`2f0d8d41`** — gate `arrSet`'s per-element `__set_<idx>__`
>    inherited-setter probe behind a new `__has_indexed_setters__` flag,
>    stamped by `Object.defineProperty` at the only install site.
>    Conservative: one-way (set, never cleared).  `array_literal`
>    1660 → 1054 ms (−37% on top).
> 3. **`f1b26da3`** — hoist the gate out of `arrayPush`'s per-element
>    loop, then call `arrayTryFastSet` directly when the gate is clean
>    (the universal case).  Falls back to `arrSet` only on sparse-
>    overflow or genuine inherited setter.  `array_literal` 1054 →
>    994 ms (−6% on top, structural cleanup more than perf).
> 4. **`b2ca3b7c`** — gate `resolvePutFieldOOP` (every `obj[k]=v`
>    write) behind `__has_accessor_props__` and `__has_nonwritable_props__`
>    flags.  Stamping points: `Object.defineProperty` accessor / data
>    branches, `Object.freeze`, `PrototypeUtils::installMethod` (because
>    every native method's `.length` / `.name` are writable=false).
>    **`object_write_only` 9554 → 1337 ms (−86%)**, `object_property`
>    2793 → 1204 ms (−57%).
> 5. **`7fdf9eba`** — gate the read-path `invokeGetterIfPresent` /
>    `invokeGetterIfPresentFast` behind the same `__has_accessor_props__`
>    flag.  Stamping at `Map.prototype.size` / `Set.prototype.size`
>    native getter install sites.  **`object_read_only` 415 → 99 ms
>    (−76%)**, `object_property` 1204 → 576 ms (−52% on top).

#### Standard In-Process Suite — vs Node.js 22 / V8 / vanilla QuickJS

Same `build_release/protojs`, same Node 22, `qjs_minimal_release`.

| Benchmark                  |    Node |    QuickJS |   protoJS |   Node × |   QuickJS × |
|----------------------------|--------:|-----------:|----------:|---------:|------------:|
| array_literal              |    2 ms |       6 ms |   1016 ms |    508 × |       169 × |
| control_flow               |    4 ms |      43 ms |    260 ms |     65 × |       6.0 × |
| function_calls             |    1 ms |       8 ms |    227 ms |    227 × |        28 × |
| json_transform             |    1 ms |       3 ms |    152 ms |    152 × |        51 × |
| json_transform_small       |    0 ms |       0 ms |     14 ms |      —   |        —    |
| list_snapshot_history      |    0 ms |       1 ms |    269 ms |      —   |       269 × |
| numeric_loop               |    1 ms |      39 ms |    156 ms |    156 × |       4.0 × |
| **object_property**        |   45 ms |      67 ms |    623 ms |     14 × |       9.3 × |
| **object_read_only**       |    1 ms |       5 ms |     99 ms |     99 × |        20 × |
| **object_write_only**      |   12 ms |      52 ms |   1381 ms |    115 × |        27 × |
| **parallel_cpu**           |**40 ms**|  **727 ms**|  **52 ms**|**Node 1.3 ×**|**protoJS 14.0 ×**|
| string_concat              |    1 ms |       4 ms |    105 ms |    105 × |        26 × |
| string_insert_middle       |    0 ms |       0 ms |    241 ms |      —   |        —    |
| string_processing          |    0 ms |       0 ms |    245 ms |      —   |        —    |
| string_repeated_doubling   |   38 ms |       1 ms |   2197 ms |     58 × |      2197 × |
| tree_traversal             |    1 ms |       3 ms |    306 ms |    306 × |       102 × |

**Geometric mean (12 benches where all three engines > 0 ms):**
- **protoJS / Node = 79.9 ×**   (was 132 × on 2026-06-06, was 58.5 × on 2026-06-01)
- **protoJS / QuickJS = 21.9 ×** (was 34.4 × on 2026-06-06, was 21.0 × on 2026-06-01)
- QuickJS / Node = 3.65 ×

#### Recovery vs 2026-06-06 baseline

15 benches present in both runs; geomean **today / 2026-06-06 = 0.694×**
— protoJS is now ~**31% faster** on the standard-suite geomean than the
2026-06-06 snapshot, and within **+6.9% geomean** of the 2026-06-01
baseline (i.e. the 10 Array cleanup packages' ~78% regression has been
fully recovered while the spec-compliance gains are preserved).

| Benchmark                  | 06-06 (ms) | 06-07 (ms) |        Δ |
|----------------------------|-----------:|-----------:|---------:|
| object_write_only          |       9554 |       1381 | **−86 %** |
| object_property            |       2793 |        623 | **−78 %** |
| object_read_only           |        394 |         99 | **−75 %** |
| array_literal              |       2315 |       1016 | **−56 %** |
| json_transform             |        199 |        152 |     −24 % |
| json_transform_small       |         17 |         14 |     −18 % |
| list_snapshot_history      |        324 |        269 |     −17 % |
| string_processing          |        251 |        245 |      −2 % |
| tree_traversal             |        308 |        306 |      ±0 % |
| string_repeated_doubling   |       2106 |       2197 |      +4 % |
| parallel_cpu               |         52 |         52 |      ±0 % |
| string_concat              |        101 |        105 |      +4 % |
| function_calls             |        213 |        227 |      +7 % |
| control_flow               |        239 |        260 |      +9 % |
| numeric_loop               |        124 |        156 |     +26 % |
| string_insert_middle       |        236 |        241 |      +2 % |

**Honest framing.**  Even with the full recovery, single-thread
throughput vs QuickJS sits at 21.9× geomean — far from the < 5× target
that would put protoJS competitive with vanilla-interpreter peers.
The remaining gap is **protoCore-side immutable structural sharing**:
every `setAttribute` rebuilds the sparse-list snapshot, and the
benches that still regress significantly vs 2026-06-01 (`array_literal`,
`list_snapshot_history`, `numeric_loop`) all bottleneck in per-write
`ProtoList::appendLast` / `setAt` allocations.  Closing this gap
requires either (a) mutable inline element storage for arrays in a
hot-path-detection mode, or (b) reworking the protoCore allocator's
freelist for the very narrow case of arena-cycled small Cells.  Both
are out of scope for the perf recovery this cycle.

`parallel_cpu` unchanged at 52 ms: **14.0 × win against QuickJS**, **77%
of V8's JIT'd throughput**.  The architectural advantage of the
GIL-free runtime is the one bench protoJS dominates.

Raw rounds: `tests/benchmarks/results/three-way-rounds-final.txt`.
Raw JSON (final summary): `tests/benchmarks/results/node_quickjs_comparison.json`.

---

**Prior baseline — 2026-06-06** (in-process median time, protoJS built
in pure Release mode against a `libprotoCore` from the
`digression-attr-cache-padding` branch — the TL-IC entry padding +
better cache-key hash described in `protoCore/README.md` §
"TL-IC entry padding").  This run lands on top of the 2026-06-01
snapshot and reflects **ten Array cleanup packages** (~200 one-fix-per-
failure commits sharpening `Array.prototype.*` spec compliance) plus
the protoCore digression.

Sampling: **4 outer × 5 inner = 20 timing samples per cell**, median
reported.  `PROTOCORE_GC_CONTEXT_THRESHOLD=10_000_000` (10 M cells per
context) keeps the GC out of the foreground path so the numbers reflect
interpreter throughput, not collector noise.  No CPU pinning — the
concurrent GC thread needs its own core.

> **Landed this cycle (2026-06-02 → 2026-06-06)**
>
> 1. **Ten Array cleanup packages** (`0881acc5` and prior).  ~200 commits,
>    one-fix-per-failure, lifting `built-ins/Array.prototype.*` test262
>    pass rate to **~88.2%** while keeping the broader suite stable.
>    Each fix tightens one spec corner: Array.prototype.push must run
>    the `__pd_length__` writability probe even with no args; arrSet
>    must dispatch inherited `__set_<idx>__` setters; `unshift` must
>    fire setters on prototype slots; constructor backrefs on
>    Boolean/Number/Object/Promise/Map/Set must be mutable for
>    `delete` to succeed; etc.  The cumulative correctness gain is
>    real and visible in the test262 numbers — but the runtime cost is
>    also real and visible below (see "Regression vs 2026-06-01").
> 2. **protoCore digression** (`fe173e48` and prior on
>    `digression-attr-cache-padding`).  Two protoCore changes verified
>    on `object_access_benchmark`:
>    * `AttributeCacheEntry` 24 B → 32 B with `aligned_alloc(64,…)`:
>      addressing collapses to one shift, no split-line loads.
>      Measured: **−9.6 % wall, −3.6 % cycles, −27 % L1d misses**.
>    * Cache-key hash shifts `currentValue >> 6` first (cells are
>      64-byte aligned, so the low 6 bits were always zero).
>      Slot occupancy 64 → 256 unique slots out of 1024 on the
>      workload probe.  Measured on `object_access_benchmark`:
>      25.75 B → **24.05 B cycles** (−7 %), IPC 2.31 → 2.47.
>    Both kept (cache hash) or rejected (mutable-value padding —
>    documented negative result).  Did **not** visibly move the
>    benchmark numbers below — the bottleneck has shifted from
>    attribute-cache pressure to per-call `ProtoString` construction
>    inside the tightened Array methods.

#### Standard In-Process Suite — vs Node.js 22 / V8 / vanilla QuickJS

Three-way comparison; same `build_release/protojs`, same Node 22,
`qjs_minimal_release` (QuickJS rebuilt with `-O3 -DNDEBUG`).

| Benchmark                  |    Node |    QuickJS |   protoJS |   Node × |   QuickJS × |
|----------------------------|--------:|-----------:|----------:|---------:|------------:|
| array_literal              |    3 ms |       6 ms |   2315 ms |    772 × |       386 × |
| control_flow               |    4 ms |      43 ms |    239 ms |     60 × |       5.6 × |
| function_calls             |    1 ms |       9 ms |    212 ms |    212 × |        24 × |
| json_transform             |    1 ms |       3 ms |    199 ms |    199 × |        66 × |
| json_transform_small       |    0 ms |       0 ms |     17 ms |      —   |        —    |
| list_snapshot_history      |    0 ms |       1 ms |    324 ms |      —   |       324 × |
| numeric_loop               |    1 ms |      32 ms |    124 ms |    124 × |       3.9 × |
| object_property            |   34 ms |      66 ms |   2793 ms |     82 × |        42 × |
| object_read_only           |    1 ms |       6 ms |    394 ms |    394 × |        66 × |
| object_write_only          |   10 ms |      51 ms |   9554 ms |    955 × |       187 × |
| **parallel_cpu**           |**40 ms**|  **695 ms**|  **52 ms**|**Node 1.3 ×**|**protoJS 13.4 ×**|
| string_concat              |    1 ms |       4 ms |    101 ms |    101 × |        25 × |
| string_insert_middle       |    0 ms |       0 ms |    235 ms |      —   |        —    |
| string_processing          |    0 ms |       0 ms |    251 ms |      —   |        —    |
| string_repeated_doubling   |   37 ms |       1 ms |   2106 ms |     57 × |      2106 × |
| tree_traversal             |    0 ms |       3 ms |    308 ms |      —   |       103 × |

**Geometric mean (12 benches where all three engines > 0 ms):**
- **protoJS / Node = 132.2 ×**  (was 58.5× on 2026-06-01)
- **protoJS / QuickJS = 34.4 ×**  (was 21.0× on 2026-06-01)
- QuickJS / Node = 3.85 ×

#### Regression vs 2026-06-01 baseline

15 benches present in both runs; geomean **today / 2026-06-01 = 1.78 ×**
— protoJS got ~**78 % slower** on the geomean of the standard suite.

| Benchmark                  | 06-01 (ms) | 06-06 (ms) |        Δ |
|----------------------------|-----------:|-----------:|---------:|
| array_literal              |        195 |       2315 | **+1087 %** |
| list_snapshot_history      |         29 |        324 | **+1017 %** |
| object_read_only           |         55 |        394 |  **+616 %** |
| json_transform             |        103 |        199 |   **+93 %** |
| object_property            |       1562 |       2793 |   **+79 %** |
| json_transform_small       |         11 |         17 |       +50 % |
| object_write_only          |       6676 |       9554 |       +43 % |
| numeric_loop               |         93 |        124 |       +33 % |
| control_flow               |        192 |        239 |       +24 % |
| string_processing          |        243 |        251 |        +3 % |
| parallel_cpu               |         52 |         52 |       ±0 %  |
| tree_traversal             |        307 |        308 |       ±0 %  |
| string_concat              |        104 |        101 |        −3 % |
| function_calls             |        220 |        212 |        −4 % |
| string_repeated_doubling   |       2187 |       2106 |        −4 % |
| string_insert_middle       |        259 |        235 |        −9 % |

**Diagnosis.**  Spot-check of `Array.prototype.push` (the dominant
operation in `array_literal`'s 100 K-push loop):

```cpp
// arrayPush hot path — runs PER CALL:
const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_length__");
// → builds a fresh ProtoString rope every push, even though
//   pd_length is a stable runtime symbol.
//   Same pattern for "__set_<idx>__" inside arrSet, etc.
```

Each tightened Array method added one or more spec-mandated probes
behind a freshly-constructed `ProtoString`.  On `array_literal` that's
~300 K extra rope allocations per outer iteration — far more cost than
the protoCore cache improvements can pay back through better hit
rates.

**This is the explicit "purity > performance" tax** the project is
willing to pay until users justify otherwise.  The fix is **not** to
unwind the correctness work — it's to (a) strong-intern these dispatch
symbols via `JSSymbols::__pd_length__()` / `__set_<idx>__()` rather
than reconstructing them per call, and (b) batch the `arrSet` setter-
probe loop to amortise the per-index symbol construction over the
whole push.  Both are queued for the next perf cycle.

**`parallel_cpu` is unchanged** at 52 ms — **13.4 × win against
QuickJS** and **77 % of V8's JIT'd throughput** on a 4-task ×
5-round CPU-bound workload.  The GIL-free architectural payoff
survives every Array cleanup package.

Raw rounds: `tests/benchmarks/results/three-way-rounds.txt`.
Raw JSON (final summary round): `tests/benchmarks/results/node_quickjs_comparison.json`.

---

**Prior baseline — 2026-06-01** (in-process median time, protoJS built
in pure Release mode against `libprotoCore.so.1.2.0` — same protoCore
binary as 2026-05-31, no protoCore changes this cycle).  This run lands
on top of the 2026-05-31 snapshot and reflects five protoJS-only commits
that closed four correctness bugs and added one perf fast path:

> **Landed this cycle (2026-06-01)**
>
> 1. `b09373ea perf(interp)` — skip `pContext->newList()` for argc==0
>    native calls (`s.trim()`, `s.toString()`, etc.); collapse `OP_call`'s
>    `newList()+appendLast` loop into the single-allocation
>    `newList(argc, slice)` form.  Standard suite gain: `function_calls`
>    **−33.7%**, `numeric_loop` −25.6%, `array_literal` −19%, eight more
>    benches in the −8% to −28% range; geomean vs Node held steady at
>    ~58× because the slowest string benches (still bottlenecked in
>    protoCore-side rope construction) dominate the geomean math.
> 2. `ac3f225d fix(interp)` — sync generator `return` was wrapping the
>    value in `Promise.resolve()` instead of `{value, done:true}`.
>    Root cause: QuickJS emits `OP_return_async` for both async-function
>    AND sync-generator bodies; protoJS's handler unconditionally wrapped
>    in Promise.  Fix gates the Promise wrap on `isAsync && !isGenerator`.
>    `function* g(){return 99;}.next().value` now returns `99` (was a
>    Promise-like `{then,catch,finally}`).  Plus two stray debug printfs
>    (`FOR_OF_NEXT`, `L_OP_for_in_start`) that survived prior cleanup.
> 3. `b0b6f692 perf(interp)` — SmallInt fast path for `OP_add_loc` (the
>    only add-class opcode without one; `OP_add`, `OP_sub`, `OP_mul`,
>    `OP_mod`, `OP_inc/dec`, `OP_inc_loc/dec_loc`, `OP_lt/lte/gt/gte` all
>    had theirs).  `numeric_loop` −20.8%, `json_transform` −11.5%,
>    `control_flow` −6.4%, `array_literal` −5%; geomean improved
>    58.84× → 55.88× vs Node before noise smoothing.
> 5. `ccefb3e4 perf(interp)` — reorder `L_OP_call` so `__bytecode_id__`
>    is read first (cached on the closure's stable snapshot via
>    protoCore's per-thread attribute cache).  If `bcId >= 0` the
>    receiver is a JS closure by construction and the `__native_fn__`
>    unwrap branch is skipped entirely (it would always return nullptr
>    for a JS closure).  Folds the prior duplicate `getBytecodeId`
>    call into a conditional re-lookup that only fires when unwrap
>    replaced func.  Measured with `perf stat -r 3 -e cycles`:
>    function_calls drops **−8% cycles, −7.1% instructions,
>    −10.3% wall** with no IPC regression.  Standard suite:
>    function_calls vs Node 224× → **199×** (−11%), vs QuickJS
>    27.6× → **23.1×** (−16%); geomean vs Node 58.5× → **57.2×**.
>    Architectural insight that drove this (from review): protoCore's
>    attribute cache makes stable mutables effectively read-cached
>    after first lookup; the right "inline cache" is not a parallel
>    cache layer in the interpreter, but constructing the closure so
>    its identity-attributes (bytecode id, captured cells, prototype)
>    are reachable directly via attribute lookup that protoCore
>    already caches.
>
> 4. `3dc726b8 fix(interp)` — `for-of` over arrays and generators
>    produced wrong values on the FIRST iteration AND never terminated
>    (`for (var v of [10,20,30]) console.log(v)` printed `10,20,30,
>    undefined,undefined,...` forever).  Root cause: L_OP_for_of_start
>    writes iterator bookkeeping into absolute automaticLocals slots
>    starting at `0x10000 + pc` (~65 000).  The first write triggers
>    `resizeAutomaticLocals(65 567)`, which relocates the std::vector's
>    storage — but runBytecode caches the slot array pointer in a local
>    `pAutomaticLocals` at the top of the dispatch loop, which is now
>    dangling.  Every subsequent opcode that accesses the value stack
>    via `pAutomaticLocals[...]` reads (or writes!) freed memory; this
>    causes both the garbage on iteration 1 and the non-termination
>    (the counter at `baseSlot+1` is read through the stale pointer
>    and never reflects the updated index).  Fix: invoke
>    `REFRESH_INTERP_STATE()` after each of the three setSlot blocks
>    in L_OP_for_of_start (Case A, B, C).

#### Standard In-Process Suite — vs Node.js 22 / V8

| Benchmark                  |   protoJS |    Node |    Node × |
|----------------------------|----------:|--------:|----------:|
| array_literal              |    195 ms |    2 ms |      97×  |
| control_flow               |    192 ms |    5 ms |      38×  |
| function_calls             |    220 ms |    1 ms |     220×  |
| json_transform             |    103 ms |    1 ms |     103×  |
| json_transform_small       |     11 ms |    0 ms |      22×  |
| list_snapshot_history      |     29 ms |    0 ms |      58×  |
| numeric_loop               |     93 ms |    1 ms |      93×  |
| object_property            |   1562 ms |   32 ms |      49×  |
| object_read_only           |     55 ms |    1 ms |      55×  |
| object_write_only          |   6676 ms |   11 ms |     607×  |
| **parallel_cpu**           |  **52 ms**|**40 ms**| **Node 1.3×** |
| string_concat              |    104 ms |    1 ms |     104×  |
| string_insert_middle       |    259 ms |    0 ms |     518×  |
| string_processing          |    243 ms |    0 ms |     486×  |
| string_repeated_doubling   |   2187 ms |   39 ms |      56×  |
| tree_traversal             |    307 ms |    1 ms |     307×  |

**Geometric mean (18 benches): Node.js 58.5× protoJS** — single-thread.
Dominated by the four worst benches (`object_write_only`,
`string_insert_middle`, `string_processing`, `string_repeated_doubling`)
which all bottleneck in protoCore-side immutable-structure construction
and would need protoCore work to move.

#### Standard In-Process Suite — vs vanilla QuickJS (interpreter-vs-interpreter)

| Benchmark                  |   protoJS |    QuickJS |  QuickJS × |
|----------------------------|----------:|-----------:|-----------:|
| array_literal              |    249 ms |       7 ms |     35.6×  |
| control_flow               |    234 ms |      53 ms |      4.4×  |
| function_calls             |    276 ms |      10 ms |     27.6×  |
| json_transform             |    128 ms |       5 ms |     25.6×  |
| json_transform_small       |     15 ms |       1 ms |     15.0×  |
| list_snapshot_history      |     39 ms |       1 ms |     39.0×  |
| numeric_loop               |    124 ms |      44 ms |      2.8×  |
| object_property            |   1993 ms |      85 ms |     23.5×  |
| object_read_only           |     77 ms |       7 ms |     11.0×  |
| object_write_only          |   6838 ms |      51 ms |    134.1×  |
| **parallel_cpu**           |  **52 ms**| **723 ms** | **protoJS 13.9×** |
| string_concat              |    106 ms |       5 ms |     21.2×  |
| string_insert_middle       |    254 ms |       0 ms |    508.0×  |
| string_processing          |    240 ms |       0 ms |    480.0×  |
| string_repeated_doubling   |   2259 ms |       2 ms |   1129.5×  |
| tree_traversal             |    366 ms |       4 ms |     91.5×  |

**Geometric mean (18 benches): QuickJS 21× protoJS** — single-thread
interpreter vs interpreter, dominated by the same four benches as
above.  `numeric_loop` is now **2.8× QuickJS** (was 1.46× on the
2026-05-31 snapshot with QuickJS recording 89 ms; QuickJS itself is
recording faster numeric work on this run, so the ratio swing is
QuickJS-side run-to-run variance, not a protoJS regression — protoJS's
own number on this bench improved from 130 ms to 124 ms).

`control_flow` is **4.4× QuickJS** — the closest single-thread bench
where protoJS is still in striking distance.

`parallel_cpu`: **protoJS wins 13.9×** against QuickJS and runs at
~77% of V8's JIT'd throughput on a workload that scales across cores.
The one bench where GIL-free architecture is visibly load-bearing.

Raw JSON: `tests/benchmarks/results/standard_comparison.json` and
`tests/benchmarks/results/standard_comparison_quickjs.json`.

---

**Prior baseline — 2026-05-31** (in-process median time, protoJS built in
pure Release mode and linked against protoCore `libprotoCore.so.1.2.0` —
the build that includes **snapshot-at-STW + Phase 2 trim** for concurrent
GC (see `protoCore/docs/GarbageCollector.md` § "Concurrent Mark Without
Barriers").  This baseline is the first measurable run since 2026-05-06:
two regressions had silently broken the standard suite between then and
now, both fixed in this cycle.

> **Regressions fixed this cycle**
>
> 1. `printf("TRACE: ...")` was committed into the bytecode `DISPATCH()`
>    macro on 2026-05-22 by snapshot `7b5d9ddd` (uncommitted working tree
>    marked as "not separately reviewed").  Every dispatch printed a
>    trace line to stdout — both polluting the `__BENCH_RESULT__` parser
>    (every benchmark reported "Error: undefined") and adding
>    catastrophic per-dispatch overhead.  Removed by `283a02a5`.
> 2. `Date.now` was `undefined`.  Root cause: `TimingAPIs::init` created
>    `Date` via `ctx->fromMethod(...)` and then tried to attach `.now`
>    via `setAttribute`.  Method objects created with `fromMethod` do not
>    retain attribute writes — the assignment silently dropped.  Fixed
>    by `b546a64f` switching to `newObject(true)` with matching `name`/
>    `prototype` so the interpreter's stub-installer guard skips it.
>
> Together these explain why no comparable measurement could be produced
> between 2026-04-28 and 2026-05-31.

#### Standard In-Process Suite (`run_standard_comparison.js`, `run_standard_comparison_quickjs.js`)

Self-contained micro-benchmarks; each reports median of 5 internal
runs.  Both reference engines exercised in the same session against
the same `build_release/protojs`.

##### vs Node.js 22

| Benchmark             | protoJS     | Node     | Node speedup |
|-----------------------|------------:|---------:|-------------:|
| array_literal         |    197 ms   |    3 ms  |        65.7× |
| control_flow          |    228 ms   |    5 ms  |        45.6× |
| function_calls        |    215 ms   |    1 ms  |       215.0× |
| json_transform        |    105 ms   |    2 ms  |        52.5× |
| numeric_loop          |    109 ms   |    1 ms  |       109.0× |
| object_property       |   1650 ms   |   49 ms  |        33.7× |
| object_read_only      |     52 ms   |    3 ms  |        17.3× |
| object_write_only     |   6904 ms   |   11 ms  |       627.6× |
| **parallel_cpu**      |  **52 ms**  |**41 ms** | **Node 1.3×** |
| string_concat         |    107 ms   |    1 ms  |       107.0× |

**Geometric mean (10 benches): Node 37.75× faster than protoJS** —
load-bearing single-thread number on this hardware.

##### vs vanilla QuickJS (interpreter-vs-interpreter, no JIT on either side)

| Benchmark             | protoJS     | QuickJS  | QuickJS speedup |
|-----------------------|------------:|---------:|----------------:|
| array_literal         |    431 ms   |    6 ms  |          71.83× |
| control_flow          |    522 ms   |   50 ms  |          10.44× |
| function_calls        |    257 ms   |   10 ms  |          25.70× |
| json_transform        |    133 ms   |    4 ms  |          33.25× |
| numeric_loop          |    130 ms   |   89 ms  |           1.46× |
| object_property       |   2012 ms   |   91 ms  |          22.11× |
| object_read_only      |     63 ms   |    6 ms  |          10.50× |
| object_write_only     |   8551 ms   |   54 ms  |         158.35× |
| **parallel_cpu**      |  **52 ms**  |**776 ms**| **protoJS 14.92×** |
| string_concat         |    113 ms   |    4 ms  |          28.25× |
| tree_traversal        |    349 ms   |    4 ms  |          87.25× |

**Geometric mean (11 benches): QuickJS 10.34× faster than protoJS** —
the meaningful interpreter-vs-interpreter number.  Closing this gap is
the work of the P-JS optimisation track.  The Node gap above includes
the JIT advantage layered on top of this.

##### Architectural payoff: GIL-free threading

`parallel_cpu.js` (4 tasks × 5 rounds via `protoCore.runInThread`):

- protoJS: **52 ms** — single-process, four real OS threads running
  concurrently on protoCore primitives with no global lock.
- QuickJS: 776 ms — single-threaded interpreter, serialised CPU work.
- Node.js: 41 ms — V8 JIT'd code, single-threaded but JIT-fast.

protoJS **wins 14.9×** against QuickJS and reaches **77 % of Node's**
JIT'd throughput on a workload that scales with cores.  This is the
one benchmark where the architectural decision (GIL-free runtime on
protoCore) is visibly load-bearing.  Workloads that scale across
cores get the advantage; tight single-thread loops do not.

#### Comparison against 2026-04-28 baseline

Compares the six benchmarks present in both runs.

| benchmark            | 04-28 (ms) | 05-31 (ms) | Δ      |
|----------------------|-----------:|-----------:|-------:|
| array_literal        |       1030 |        197 | −80.9% |
| control_flow         |        735 |        228 | −69.0% |
| function_calls       |       2090 |        215 | −89.7% |
| numeric_loop         |        455 |        109 | −76.0% |
| object_property      |       9577 |       1650 | −82.8% |
| parallel_cpu         |         55 |         52 |  −5.5% |

**Geomean ratio = 0.249** — protoJS is ~75 % faster than the 2026-04-28
baseline across the six benchmarks present in both runs.  This is the
P-JS-{0..7} optimisation cycle's actual landed effect, which could not
be measured properly while the TRACE printf was active on the binary.

#### Hot spots worth targeted attention

- `object_write_only` (158× QuickJS, 628× Node) — cost of writes
  through protoCore's immutable structural-sharing: every property
  assignment builds a new object chain.  Highest-leverage target.
- `tree_traversal` / `function_calls` (87× / 26× QuickJS) — tight-loop
  dispatch dominates.  P-JS track is already addressing this.
- `numeric_loop` (1.46× QuickJS) — within noise of parity; the basic
  integer loop is no longer pathological.

Full report: `tests/benchmarks/results/comparison_2026-05-31.md`.
Raw JSON results: `tests/benchmarks/results/baseline_2026-05-31.json`
and `tests/benchmarks/results/standard_comparison_quickjs.json`.

---

**Honest baseline — 2026-05-06** (in-process median time, protoJS built in
pure Release mode (`-O3 -DNDEBUG`, no debug info) and linked against the
matching Release build of protoCore — including the GC survivor re-chain,
the runtime string-intern removal, the OP_inc_loc / OP_dec_loc slot-
addressing fix, perpetual NULL-context allocation for strong symbols,
the simplification of `getAttribute` to rely on the GC-pinned attribute
cache instead of a per-cycle invalidation, and the **P-JS-{0..4} cycle**
that minimised protoCore-side traffic on the property-access hot path
(see "Driving wins" below) — vs. Node.js/V8 22 and vanilla QuickJS
(`qjs_minimal` rebuilt with the same `-O3 -DNDEBUG`).  Pure compute;
no startup cost counted on either side.  Each row in the tables below
is the median across **12 outer rounds** of the runner, where every
round already reports the median of **5 internal iterations** of the
benchmark — i.e. **60 timing samples per cell**.

#### Standard In-Process Suite (`run_standard_comparison.js`)

Self-contained micro-benchmarks with tight loops; 12 outer × 5 inner = 60 samples, median reported.

| Benchmark             | protoJS     | Node.js  | Ratio (Node faster) |
|-----------------------|-------------|----------|--------------------:|
| array_literal         |    199 ms   |    2 ms  |               93.5× |
| control_flow          |    245 ms   |    4 ms  |               60.6× |
| function_calls        |    189 ms   |    1 ms  |              189.0× |
| numeric_loop          |    115 ms   |    1 ms  |              114.5× |
| object_read_only      |     64 ms   |    1 ms  |               57.5× |
| string_concat         |    103 ms   |    1 ms  |              103.0× |
| object_property       |    359 ms   |   37 ms  |                9.9× |
| object_write_only     |    823 ms   |   11 ms  |               74.8× |
| json_transform        |      5 ms   |    1 ms  |               10.0× |
| **parallel_cpu**      |  **52 ms**  |**41 ms** |  **Node 1.27×**     |
| tree_traversal        |    316 ms   |    1 ms  |              457.0× |

**Geometric mean (12 benches): Node.js 28.94× faster than protoJS**
(median across 12 rounds; geomean-of-medians 36.84×) — refreshed
2026-05-06 after **P-JS-7 (dispatch_table hoisted out of the per-call
hot path)** completed the May 2026 cycle on top of SmallSparseList,
P-JS-{0..6}, and the broader May 2026 work.

> **Re-verified 2026-05-07** after the protoCore `ProtoObjectCell::attributes`
> tag-0 architectural fix landed (storing the raw `ProtoSparseListImplementation*`
> instead of the API-tagged `ProtoSparseList*`): median geomean 31.23×, geomean-
> of-medians 38.17×.  Both deltas sit inside the round-to-round variance band
> (single-round geomeans ranged 25.14–39.06), so the architectural fix is
> performance-neutral for protoJS.  Same `build_release/protojs`, same Node 22, same 12×5 sampling.

> **⚠ Earlier "intermediate" cycle measurements were noise.** The
> aggregated runner (`run_standard_comparison.js`) silently used a
> stale `build/protojs` binary instead of the rebuilt `build_release/protojs`
> for every prior measurement in this cycle.  The "+1-2% per step"
> README entries (P-JS-5, SmallSparseList, P-JS-6) were comparing the
> SAME unchanged binary across rounds — pure noise.  This entry is
> the first measurement against the actual optimised binary.  The
> runner is now fixed (`PROTOJS_BIN` env var support + `build_release/`
> preferred over `build/`) so this cannot recur. (paths #2/#3/#4/#6, task #28 CAS removal, task #34 destructor
reorder fix, task #36 chunked freelist via GC pre-chunking, tasks
#37/#39 type-flags cache + unified attribute fast paths, **task #42
SparseList hash cascade elimination**).  Improvement of ~45 % over
the prior 75.13× baseline.  Driving wins:

**P-JS-7 — dispatch_table hoisted out of the per-call hot path**
(largest single win of the cycle): the 256-entry computed-goto table
was re-initialised on every entry to `runBytecode` — 256 default-fills
+ ~210 per-opcode overrides = ~470 stores per call.  For
tree_traversal that was ~150 M wasted stores per bench run.  Beyond
the raw stores, each frame held 2 KB of dispatch_table on the C++
stack; with recursion depth 14, ~28 KB of duplicated tables overflowed
L1d (32 KB), causing measurable cache pressure that flat profiles
attributed silently to the `runBytecode` self-time symbol.  Fix:
function-scope `static const void* dispatch_table[256]` initialised
once via DCLP (`std::atomic<bool>` + `std::mutex`).  Address-of-label
values are stable across function entries — the binary loads once,
labels live at fixed code-segment offsets — so single-process
initialisation is correct.  Steady-state cost: 1 acquire-load + a
predicted-not-taken branch (~2 cycles) per `runBytecode` entry.
Per-bench impact (60-sample real medians, post-cycle): tree_traversal
1004→316 ms (**−69%**), function_calls 448→189 ms (−58%),
object_write_only 1430→823 (−42%), object_property 598→359 (−40%),
array_literal 301→199 (−34%), string_concat 176→103 (−41%).

**P-JS-6 — DISPATCH macro hot-path trim**: the computed-goto
dispatcher used to re-fetch `globalObj` from `*pGlobalRoot` on **every**
opcode dispatch and to null-check the dispatch_table slot.  Both were
redundant for the common case: `globalObj` is consumed by only ~6
opcodes (OP_push_this in non-strict mode, the obj == globalObj check
in OP_put_field / OP_define_field / OP_delete, and the Array.prototype
lookup in OP_array_from), and the dispatch_table is now pre-filled
with `&&L_default` so the slot is always a valid jump target.
Per-dispatch overhead drops from ~19 cycles to ~12 cycles (-37%).

**SmallSparseList** (in protoCore — auto-applies through
the public ProtoSparseList API): single-cell inline form for sparse
lists with ≤ 3 (key, value) pairs.  Closure-cell `__cv__` writes —
profile-identified bottleneck for `function_calls.js` — drop from
3-4 cell allocations per write to 1.  See `protoCore/README.md` for
the full description.  Per-bench impact (12 × 5 = 60-sample median):
function_calls 457→389 ms (**−15 %**), control_flow 280→259 (−8 %),
numeric_loop 128→117 (−9 %), object_write_only 1332→1234 (−7 %),
array_literal 309→291 (−6 %), object_property 531→508 (−4 %).
Geomean Node 41.05× → 40.17×; vs QuickJS 9.86× → 9.74×.

P-JS-{0..5} cycle (prior commits, still in effect):
  - **P-JS-0** updateMapping made a no-op — runtime never reads the
    JSValue ↔ ProtoObject mapping outside compile-time TypeBridge
    (QuickJS is parser/compiler only; objects live in protoCore
    exclusively at run time)
  - **P-JS-2** dedup'd the redundant getAttribute(callbacks=true)
    in OP_get_field2 (resolveFieldOOP already does the canonical walk)
  - **P-JS-3** prototype-identity set replaces 6 pointer-compares per
    write in updateSpacePrototypeIfMatching
  - **P-JS-1** thread-local cache for `__get_<name>__` / `__set_<name>__`
    sidecar symbols — was constructing a fresh ProtoString rope on
    every property-miss probe
  - **P-JS-4** short-circuit default JSObjectBehavior dispatch — the
    common case (plain object) now skips the v-table indirection and
    calls obj->getAttribute / obj->setAttribute directly
  - **P-JS-5** extend P-#4 single-allocation argsList builder to
    OP_call and OP_call_constructor.  Profile-guided discovery: the
    May 5 commit `47ea3e1a` had only converted OP_call_method,
    leaving OP_call (every free function call: `f(x)`) and
    OP_call_constructor (every `new X(...)`) on the legacy
    `newList() + N×appendLast` path — costing 1+N cell allocations
    per call instead of 1.  Per-bench impact (12-round medians):
    tree_traversal 1004→890 ms (−11%), object_property 598→531 ms
    (−11%), string_concat 176→157 ms (−11%), control_flow 307→280
    ms (−9%), object_write_only 1430→1332 ms (−7%).  The
    function_calls bench itself (1 arg, single global write) is
    dominated by the closure-cell setAttribute on the captured
    `state` variable, so the per-call save is not yet visible at
    the geomean level — but anything with nested or recursive
    calls amortises the saving across the call tree.

Cumulative May 2026 wins still in effect:

  - path #4 single-allocation `argsList` builder (`OP_call` / `OP_call_method` / `OP_call_constructor`)
  - path #6 mutable-cache stash on `resolveMutableState` hot path
  - task #36 O(1) chunked freelist refill (`getFreeCells` 7.91 % → 0.52 % CPU)
  - task #42 SparseList hash propagation removed (`isString` 3.78 % → 0 %, `getAttribute` 14.03 % → 5.90 %)

`tree_traversal` now completes (it crashed on the previous baseline thanks
to the GC survivor re-chain landing in protoCore) but at ~1 s it dominates
the geomean — the bench measures Node at 1 ms (timer floor) so the ratio
is mostly an artefact of timer resolution.  Excluding `tree_traversal`
the remaining 11 benches yield Node ~37× faster — the load-bearing
single-thread number on this hardware.  The May 2026 fixes that built
up to this baseline:

- The runtime string-intern map was removed so `s += 'x'` collapsed from
  O(N²) (timeout) to O(N log N) (141 ms for 50 000 concats).
- `OP_inc_loc` / `OP_dec_loc` were routed through the same slot-addressing
  helpers as `OP_get_loc`, fixing an infinite loop on
  `for (var i = 0; i < N; i++)` inside a function.
- `OP_put_array_el` was popping the spec'd 3 slots but pushing a
  spurious 1, accumulating one slot per iteration in dynamic-key
  loops like `obj['k' + i] = i;`.  After ~17 iterations the operand
  stack exceeded its compile-time-sized window and subsequent writes
  silently bypassed setAttribute.  Removing the push restores the
  QuickJS contract — see commit log for the matching fix to
  `OP_get_array_el2 / OP_get_array_el3`.
- `OP_array_from` and the five `argsList` builds inside `OP_call` /
  `OP_call_method` / `OP_call_constructor` are now wrapped in
  `ProtoContext::CriticalSection` from the first `appendLast` through
  the bind-into-childCtx loop.  Without continuous CS, an inner
  allocCell that crosses its 64-allocation safepoint poll could
  submit the in-flight list to dirtySegments, leaving the freshly
  built list (and its element values) sweep-candidates with no live
  GC root.  Fixed json_transform (0/5 → 5/5) and let
  object_property / object_write_only land on the suite via the
  combined fixes.
- Strong-symbol creation now allocates the working
  `ProtoStringImplementation` with a NULL `ProtoContext`, so the cells
  live for the lifetime of the process and no concurrent collector can
  free the in-flight rope between `fromUTF8Bytes` and the SymbolTable
  canonicalisation.
- `ProtoObject::getAttribute` no longer pays a per-call atomic load +
  branch for GC-cycle cache invalidation — `ProtoThreadExtension::
  processReferences` already pins every (object, result, name) entry
  as a GC root, so the cache pointers cannot dangle.  Stripping that
  guard saved 5–11 % across getAttribute-heavy benches (numeric_loop,
  array_literal, function_calls).

`parallel_cpu` is effectively a tie — protoJS offloads work to native
protoCore worker threads, so the interpreter hot loop is irrelevant.

`tree_traversal` (DEPTH=14, 16 383 nodes, deep recursive property access)
now completes on the standard suite.  The previously-tracked GC survivor
race that crashed it has been resolved by the protoCore pre-mark unmark
pass (May 2026).  The remaining 965 ms — versus 1 ms on Node.js — reflects
the cost of repeatedly walking the prototype chain through immutable
snapshots; it is the deepest property-walk workload in the suite and
amplifies every per-attribute overhead the interpreter still pays.

#### Comprehensive Macro Suite (`combined_performance_suite.js`)

41 tests across Basic Types, Collections, and Overall Performance (5 iterations,
mean reported).  As of 2026-05-04 this suite **no longer completes on
protoJS**: the runner reaches the BigInt category at the end of Basic
Types, then aborts with `Error: is not a function` before producing the
per-benchmark summary line.  The same regression reproduces against the
older RelWithDebInfo build, so it is not introduced by the Release flag
change — it is a pre-existing bug in the harness's interaction with
protoJS that needs its own fix before this table can be refreshed.  Until
then, the standard and QuickJS-comparison suites above are the
trustworthy single-threaded performance baseline.

#### What the numbers do *not* measure

- **Startup cost** — protoJS starts in roughly the same wall-clock time as Node;
  for short scripts the user-observed gap is much smaller.
- **Memory & GC pauses** — protoCore's concurrent GC keeps p99 pause well below
  Node's stop-the-world cycles; not captured here.
- **Multi-threaded scaling** — Node's event loop is single-threaded; `parallel_cpu`
  exercises only a fraction of what protoCore's GIL-free thread model can deliver
  in real multi-threaded workloads.

#### Interpreter-to-Interpreter Suite (`run_standard_comparison_quickjs.js`)

Comparing against vanilla **QuickJS** (the underlying engine without protoCore) isolates the cost of the **protoCore memory model** and the **GIL-free architecture**.  Both engines were rebuilt on 2026-05-04 with the same `-O3 -DNDEBUG` flags so the comparison is purely interpreter-vs-interpreter (no compile-flag asymmetry).  Refreshed 2026-05-06 with the **P-JS-{0..5} cycle** in place, using the same 12-outer × 5-inner = 60-sample methodology as the Node comparison above (`node tests/benchmarks/run_aggregated.js 12 --quickjs`).

| Benchmark           | protoJS    | QuickJS  | Ratio (QuickJS faster) |
|---------------------|------------|----------|-----------------------:|
| array_literal       |    206 ms  |    6 ms  |                 33.3×  |
| control_flow        |    247 ms  |   47 ms  |                  5.4×  |
| function_calls      |    198 ms  |   10 ms  |                 20.5×  |
| json_transform      |      7 ms  |    1 ms  |                  8.6×  |
| numeric_loop        |    122 ms  | 35.5 ms  |                  3.5×  |
| object_property     |    369 ms  |   73 ms  |                  5.3×  |
| object_read_only    |     65 ms  |    6 ms  |                 10.6×  |
| object_write_only   |    872 ms  | 60.5 ms  |                 15.3×  |
| string_concat       |    118 ms  |    5 ms  |                 21.6×  |
| **parallel_cpu**    |  **52 ms** | **719 ms**|      **protoJS 13.8× faster** |
| tree_traversal      |    347 ms  |    4 ms  |                 81.6×  |

**Geometric mean (12 benches): QuickJS 7.05× faster than protoJS** (median across 12 outer rounds; geomean-of-medians 7.32×).

> **Re-verified 2026-05-07** post protoCore tag-0 fix: median geomean 6.87×,
> geomean-of-medians 7.63× — within noise of the 2026-05-06 baseline.
> `parallel_cpu` win held at 15.4× (protoJS 52 ms vs QuickJS 796 ms), reflecting
> QuickJS's lack of native threads.

As in
the Node comparison, `tree_traversal` is the single dominant outlier at
282×; excluding it, the gap narrows to ~7×.
`parallel_cpu` remains the only bench where protoJS wins — and the margin
widens to 13.8× because QuickJS, lacking native threads, cannot exploit
multiple cores at all.  Interpreter-vs-interpreter we are now within an
order of magnitude on every bench except `tree_traversal` and the
mutable-property / array-build workloads (`array_literal`,
`function_calls`, `string_concat`), which fundamentally trade
single-threaded speed for GIL-free immutable snapshots.

The single-thread headline (9.86×) sits near the noise band of the
previous 9.68× measurement — QuickJS scales tight enough under this
O3 build that the per-call cell-allocation savings introduced by the
P-JS-{0..5} cycle largely show up on the protoJS side without
shifting the ratio dramatically.  The structural improvement is
real: `function_calls` is now dominated by closure-cell setAttribute
on the captured `state` global rather than by per-call argsList
allocation, which is a much harder bottleneck to attack
incrementally.

### Why the gap?
The performance difference in object property access is primarily driven by fundamental architectural trade-offs:

1.  **Persistent vs. Mutable Memory**: QuickJS uses a traditional mutable hash map with hidden classes (Shapes) for fast O(1) property lookup. `protoJS` uses `protoCore`'s persistent AVL-tree structures. This provides full thread-safety and lock-free concurrency (zero-copy snapshots) but involves O(log N) lookup depth and significantly more pointer indirection.
2.  **Zero-Allocation Symbols**: Every property access in `protoJS` requires interning the key into a `protoCore::ProtoString` symbol. While we have implemented a **128-entry Inline Cache (IC)** to eliminate redundant UTF-8 conversions and sharded lookups, the overhead of symbol-stable comparison in a sharded environment persists.
3.  **Refcounting vs. Concurrency**: QuickJS uses single-threaded reference counting. `protoJS` leverages `protoCore`'s sharded, thread-safe memory management. The 20x gap in single-threaded property access is the direct "tax" for the **12x gain in parallel performance** shown in `parallel_cpu`.

Raw JSON: [tests/benchmarks/results/standard_comparison.json](../../tests/benchmarks/results/standard_comparison.json)

**To reproduce** (Release builds of both protoCore and protoJS, plus an `-O3` `qjs_minimal`):
```bash
# 1. Build protoCore in pure Release.
cd ../protoCore && cmake -B build_release -S . -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build_release --target protoCore -j$(nproc)

# 2. Build protoJS in pure Release, linked against the protoCore Release lib.
cd ../protoJS && cmake -B build_release -S . -DCMAKE_BUILD_TYPE=Release \
    -DPROTOCORE_LIBRARY=$(pwd)/../protoCore/build_release/libprotoCore.so \
    && cmake --build build_release --target protojs -j$(nproc)

# 3. Rebuild qjs_minimal with the same -O3 flags (Release).
cd tests/benchmarks && gcc -O3 -DNDEBUG -I../../deps/quickjs -o qjs_minimal \
    qjs_minimal.c ../../deps/quickjs/quickjs.c ../../deps/quickjs/libregexp.c \
    ../../deps/quickjs/libunicode.c ../../deps/quickjs/cutils.c \
    ../../deps/quickjs/dtoa.c -lm -lpthread
cd ../..

# 4. Run the suites against the Release binary.
PROTOJS_BIN=$(pwd)/build_release/protojs node tests/benchmarks/run_standard_comparison.js
PROTOJS_BIN=$(pwd)/build_release/protojs node tests/benchmarks/run_standard_comparison_quickjs.js
```
