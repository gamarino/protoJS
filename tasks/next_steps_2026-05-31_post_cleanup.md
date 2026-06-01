# Next Steps — Post-Cleanup Snapshot, 2026-05-31

## What's done

Six commits land the bug-fix / cleanup pass on top of the
`bf51db7c perf plan` baseline:

```
0e7f13f4  fix(interp): generator multi-yield — push (sent_val, kind) AFTER InterpFrame
2726042c  perf(symbols): cache interpreter sentinels as static JSSymbols entries
4a00b338  fix(interp): three protoJS-layer bugs uncovered by the balanced suite
72d7dc36  docs: add balanced suite results
a3fbd3ba  bench: add four "balanced suite" benchmarks + honest first-run report
bf51db7c  tasks: perf plan — five highest-leverage optimisations
```

All fixes live in **`src/runtime/ProtoInterpreter.cpp`** and
`src/JSSymbols.{h,cpp}`.  No protoCore edit was made.

### Bugs closed

| Bug | Where | Fix |
|---|---|---|
| `return obj.nativeMethod(...)` → undefined | L_OP_tail_call_method (both branches) | early `return result` on tail-call instead of skipping the stack push |
| `s.length` → undefined | L_OP_get_length | fast path: when receiver is a string, return `obj->asString(ctx)->getSize(ctx)` directly, bypassing the wrapper layer above protoCore |
| Multi-yield generator stuck at first yield | runBytecode generator-resume path | (a) push sent+kind AFTER InterpFrame exists; (b) replace `if (t_genResumePc < 0)` with `if (!gen_resume_active)` so initStack only runs on fresh entry |
| Interpreter sentinels re-interned on every dispatch | various sites in ProtoInterpreter.cpp | converted 12+ call sites to JSSymbols-cached statics; added 7 new DEFINE_SYMBOL entries (construct, genSent, genThrowVal, pdPrototype, jsNullSentinel, jsTdzSentinel, isConstructor) |

### Known follow-ups (not in scope here)

- **for-of over generator** sums to NaN; iterator-protocol read of
  `.value` via OP_get_field appears not to interact correctly with
  the resume contract on certain paths.
- **`it.next(arg).value`** on the FINAL call that triggers
  `OP_return` surfaces an object rather than the returned primitive.
  Likely a wrapping issue in `makeIterResult` when the body itself
  is mid-return.

Both are tracked.  Multi-yield (the dominant path) is now correct
and matches Node / QuickJS for the common cases the standard suite
exercises.

## Current measurement (vs QuickJS via the standard suite)

Same shape as the pre-cleanup snapshot — the bug fixes were
correctness, not performance, and the static-symbol caching collapsed
~0.3 % of aggregate dispatch cost rather than headline-moving work.
Standard suite still shows protoJS losing geomean ~50×–53× to Node,
~10× to QuickJS, and winning on `parallel_cpu`.

## Profile (post-cleanup, 2026-05-31, GC suppressed)

Top 25 symbols → **51.3 %** of wall time across 10 standard benches
(`/tmp/protojs_perf_v2/*.data`).  Pattern unchanged from the
`bf51db7c` plan: protoCore allocation chain dominates (~21 %), AVL
list operations are ~12 %, runBytecode is ~5 % aggregate but ~50 %
in `numeric_loop`/`control_flow`.

| rank | abs ms | %  | symbol |
|---:|---:|---:|---|
| 1 | 3923 | 7.12 | kernel fault/zero-fill |
| 2 | 3682 | 6.68 | `ProtoListImplementation` ctor |
| 3 | 3635 | 6.60 | `ProtoContext::allocCell` |
| 4 | 2616 | 4.75 | `runBytecode` |
| 5 | 1564 | 2.84 | `ProtoSpace::getFreeCells` |
| 6 | 1447 | 2.63 | `implInsertAt` |
| 7 | 1377 | 2.50 | anonymous-ns helpers / `rebalance` |
| 8 |  915 | 1.66 | `addCell2Context` |
| 9 |  836 | 1.52 | anonymous-ns stack helpers |
| 10 |  761 | 1.38 | `Cell::Cell` |

Full data: `tasks/perf_aggregate_2026-05-31.json` (original plan
snapshot, structurally still valid).

## Next steps — ranked

### 1. P-JS-11 first (string rope tag-dispatch)

**Why first**: the explicit explanation in our earlier discussion
showed that the hot accessors — `nodeHeight`, `charCount`,
`byteCount`, `isStringInternalNode`, `isStringLeafNode` — ALREADY
have the data they need stored in the cell (StringInternalNode's
`total_chars`, `total_bytes`, `height`; StringLeafNode's
`char_count`, `byte_count`).  The cost is the doubled tag-check
dispatch (`isStringLeafNode` then `isStringInternalNode`) at every
call site, NOT the data computation.

Scope: rewrite the seven accessors to:
- Single pointer-tag switch (replaces the two sequential
  `isStringLeafNode` + `isStringInternalNode` branches)
- Inline the bodies in `proto_internal.h` so the constructor's
  `byteCount(l) + byteCount(r)` and `nodeHeight` computation get
  inlined at the call site
- Add an unchecked typed-pointer overload for the constructor (which
  knows by construction the child types) — skip the tag check
  entirely there

Expected: 20–30 % reduction on `string_concat`-style benchmarks
(currently ~600 µs per concat), proportional improvements on every
rope-touching path.  Self-contained, low-risk, fast win.  Confirms
the measurement pipeline before tackling structural changes.

### 2. P-JS-8 (cell allocation fast path)

Once the rope work establishes that the per-op-overhead diet is
achievable, the next-biggest target is the protoCore allocation
chain (~14 % of total wall time spread across allocCell,
getFreeCells, addCell2Context, Cell::Cell, kernel zero-fill).

Even though we're not touching protoCore in this cycle, identifying
the cost surface is what the perf plan already documents.  The
**protoJS-side** preparation is: make sure the interpreter doesn't
gratuitously allocate in places it doesn't have to.  Specifically:

- Audit `setSlot` / `stackPush` for any temporary cell construction
- Confirm `argsList` for native methods isn't allocating when not
  consumed (the existing CS-protected newList path looks OK; verify
  no leak when the callee returns early)
- Look for OP_undefined / OP_null path: is `pContext->fromInteger(0)`
  call site (introduced by my generator-resume push) hot enough to
  warrant a cached zero?  Currently it allocates per resume; if
  multi-yield generators get heavy use we should preallocate the
  small-int zero sentinel once.

These are small fixes the interpreter can make above protoCore
before any protoCore allocation work is contemplated.

### 3. P-JS-12 (SmallInteger arithmetic / compare fast paths)

`runBytecode` is **50 %+** of `numeric_loop` and `control_flow`.
Inside runBytecode, every `OP_add` / `OP_sub` / `OP_lt` / `OP_le`
goes through `toTempBignum` even when both operands are tagged
SmallIntegers.  The proposed fast path tests `isSmallInt(a) &&
isSmallInt(b)`, does the arithmetic directly on the tagged values,
re-tags, falls back on overflow.

Scope: first PR covers OP_add and OP_lt SmallInt fast path with
`__builtin_add_overflow` fallback.  Expected: `numeric_loop` drops
from 1.46× QuickJS to roughly parity; `control_flow` drops from
10× to 4–6×.

Independent of the rope / allocation work; can be developed in
parallel.

### 4. Wrapper-layer trim above StringPrototype.cpp

`BuildStringPrototype` registers every string method in an object
wrapper that carries `__native_fn__` + length + name descriptors.
Every method call therefore does:

  prototype lookup → wrapper object → `__native_fn__` attribute →
  unwrap to ProtoMethod → invoke

That's 2-3 extra hops per call.  The protoCore-side prototype
already supports installing ProtoMethod cells directly; the wrapper
layer was added for `.length` / `.name` reporting.  Now that
`OP_get_length` has its own fast path, the wrapper for length is
redundant on string methods — we can install raw ProtoMethod cells
and let `.name` / `.length` queries fall through.

This is the most direct expression of "minimize what's done above
protoCore" for the string subsystem.  Self-contained in
`StringPrototype.cpp`.  Expected: 30–50 % reduction in the per-
method-call overhead, which matters most for short hot methods
(`charAt`, `charCodeAt`, etc.) where the dispatch dominates the
actual work.

### 5. The two remaining generator bugs

- **for-of over generator** — likely OP_for_of_start +
  OP_for_of_next interaction with the generator iterator's `.next`
  method dispatch.  When for-of calls `it.next()` internally, it
  goes through OP_call_method, but the iterator's `next` was
  installed via `regM` with a wrapper.  Probably the same wrapper-
  layer issue as #4 above, surfacing under a specific dispatch
  pathway.
- **final-return wrapping** — `makeIterResult` may be invoked twice
  when the body returns immediately after a `yield` resume.

Pair these with item #4 since they're in the same dispatch area.

## Order recommendation

```
1. P-JS-11  rope tag-dispatch       (low risk, immediate win, confirms pipeline)
2. #4       wrapper-layer trim      (cleans up the string method dispatch path)
3. #5       remaining generator     (probably falls out of #4 cleanup)
4. P-JS-12  SmallInt fast paths     (separate area, can run in parallel)
5. P-JS-8   alloc chain audit       (protoJS-side cleanup before any
                                     protoCore work)
```

## Out of scope (per the cycle's protoCore-untouched constraint)

- Anything in protoCore (the actual P-JS-8 internal mechanism work,
  P-JS-9 ProtoList AVL specialisation, P-JS-10 shape-cached
  put_field).  Those become available when the protoCore-side cycle
  reopens.
- JIT (multi-year, separate cycle).
- balanced-suite numeric work — the per-op constants identified in
  `tests/benchmarks/results/balanced_suite_2026-05-31.md` are
  fundamentally protoCore's strConcat etc., not addressable here.
