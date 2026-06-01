# Test262 Conformance Status — protoJS

**Last full-suite run:** 2026-06-01T (post-language-fixes cycle)
**Snapshot:** `tests/test262/reports/snapshot-language_built-ins-1780324296412.json`
**Binary:** `build_release/protojs` v0.1.0 (post 20-fix language cycle, commit `11b3988f` on `master`)
**Scope:** `language` + `built-ins` (46 963 tests; non-trivial subset of full Test262)
**Runner:** parallel (`TEST262_CONCURRENCY=10`, ~7 min wall on 12-core)

## Overall

| | Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Skipped | Pass rate |
|---|---:|---:|---:|---:|---:|---:|---:|
| **2026-06-01 (post 20-fix cycle)** | 46 963 | **27 884** | 874 | 18 156 | 38 | 11 | **59.37 %** |
| 2026-06-01 (morning) | 46 963 | 27 565 | 874 | 18 477 | 36 | 11 | 58.70 % |
| 2026-05-11 (prior full) | 46 963 | 27 025 | 830 | 18 666 | 431 | 11 | 57.55 % |
| **Δ vs 06-01 morning** | 0 | **+319** | 0 | **−321** | +2 | 0 | **+0.67 pp** |
| **Δ vs 05-11 baseline** | 0 | **+859** | +44 | **−510** | **−393** | 0 | **+1.82 pp** |

Key takeaways:

1. **+319 passes** from the 20-fix language cycle over a few hours; semantics failures dropped by **−321**, meaning every new pass came from a previously-failing test being correctly resolved (not from previously-timing-out tests resolving as failures).
2. Cumulative since 2026-05-11: **+859 passes (+1.82 pp)** and **−393 timeouts** (the latter overwhelmingly from the for-of OP_for_of_start fix earlier in the cycle).
3. `language` jumped **+262** in this cycle (75.24 % → 75.73 %); `built-ins` gained **+57** from collateral fixes.

## By Family

| Family | Total | Passed (this run) | Passed (06-01 AM) | Δ | Pass rate (this run) |
|---|---:|---:|---:|---:|---:|
| `built-ins` | 23 334 | 9 990 | 9 933 | +57 | 42.81 % |
| `language` | 23 629 | **17 894** | 17 632 | **+262** | **75.73 %** |

## The 20 Fixes (commit ids on `master` between `1e4eca41..11b3988f`)

Each commit is one root cause; tests were added/verified at every step. All commits live in the protoJS interpreter — no protoCore changes.

| # | Commit | Fix |
|---|---|---|
| 1 | `fdc5d068` | `toString()` helper now invokes user `.toString()` / `.valueOf()` instead of returning the literal `"[object Object]"` for object args. Fixes `obj[{toString(){...}}] = v` and all ToPropertyKey paths. |
| 2 | `ca931ec2` | `OP_set_name_computed` coerces object keys via `toString()` (same root cause as #1, different call site — function-name property of computed-key methods). |
| 3 | `0568f9ff` | `Array.prototype.hasOwnProperty` for numeric indices. Pre-fix returned false for `[10].hasOwnProperty(0)` because array elements live in the `__elements__` ProtoList, not as own attributes. Synthesised array-index + length check. |
| 4 | `e5ae627e` | `Array.prototype.propertyIsEnumerable` for numeric indices — same shape of fix as #3. |
| 5 | `8aa6b1e8` | `toString()` recognises the heap `undefined`-sentinel (protoJS has two undefined representations: PROTO_NONE and a thread-local heap sentinel used by the global `undefined` identifier). Pre-fix `String(undefined) === '[object Object]'`. |
| 6 | `21e5610b` | `OP_strict_eq` / `OP_strict_neq` unify all undefined forms. Pre-fix `undefined === void 0` was false. |
| 7 | `3079a6e5` | `OP_is_undefined` recognises the heap undefined-sentinel — fires for `x === undefined` after QuickJS's peephole optimization, which doesn't go through `OP_strict_eq`. |
| 8 | `600d0a18` | `OP_is_undefined_or_null` (the `??` and `?.` op) recognises the heap sentinel — same shape as #7. |
| 9 | `720a0c86` | Primitives are never `instanceof` their wrapper class. Pre-fix `false instanceof Boolean` was true because primitives inherit from BooleanPrototype. Spec § OrdinaryHasInstance step 1: primitives → false. |
| 10 | `80d86120` | Object-literal getters/setters install as accessor sidecars. `OP_define_method` and `OP_define_method_computed` were ignoring the op_flags byte that distinguishes METHOD / GETTER / SETTER, installing every form via plain `setAttribute`. `({ get foo() { return 42 } }).foo` now correctly invokes the getter. |
| 11 | `5cbf283e` | Comparison ops (`<`, `<=`, `>`, `>=`) numerify booleans per Abstract Relational Comparison §7.2.13. Pre-fix `1 < true` was true (pointer-order compare of SmallInt vs PROTO_TRUE singleton). |
| 12 | `31769a98` | Calls on a non-callable receiver throw TypeError. Pre-fix `Math()` / `({}).x()` silently returned undefined. |
| 13 | `716ac360` | `Math.{PI,E,...}` are non-configurable / non-writable / non-enumerable per spec. Pre-fix `delete Math.PI` returned true and `Math.PI = 99` overwrote silently. |
| 14 | `1e4eca41` | Install `globalThis` as a self-reference on the global object. Pre-fix `typeof globalThis === 'undefined'`. |
| 15 | `2454f3e0` | `ToString(Number)` formats safe-integer doubles as integers (was `%.15g` which lost the last digit on `Number.MAX_SAFE_INTEGER`). |
| 16 | `57570f78` | `Number()` / `Boolean()` (called as conversion functions, not constructors) dispatch via the `__construct__` marker and unwrap to the primitive. Fixes a regression from #12 that made `Number('3')` throw TypeError. |
| 17 | `25ec9613` | `for-of` over `undefined` throws TypeError (was a silent vacuous pass). |
| 18 | `96f6ce9f` | `OP_append` reads source array via `arrayTryFastGet` (arrays now keep elements in `__elements__`, not as string-keyed attributes — same root cause as #3, different call site). |
| 19 | `f78d9e03` | `Array(n)` creates a sparse n-length array; `Array(a, b, ...)` installs each element. Pre-fix `Array(3).length` was 1 (the int was stored as one element). |
| 20 | `11b3988f` | String bracket indexing returns the character. Pre-fix `'abc'[0] === undefined`. The receiver is checked at the top of `L_OP_get_array_el`; for strings, a UTF-8 walk extracts the requested codepoint. |

### Architectural observations from this cycle

- **protoJS keeps two representations of `undefined`** — the tagged-pointer `PROTO_NONE` used everywhere by the dispatch loop, and a heap-allocated `t_undefinedSentinel` (TypeBridge.cpp) used as the value of the global `undefined` identifier. Several opcodes (`OP_strict_eq`, `OP_is_undefined`, `OP_is_undefined_or_null`, the global `toString()` helper, `OP_for_of_start`) had not been audited for both; this cycle's fixes #5–#8 + #17 close that audit. The proper long-term fix is to collapse the two representations, but that's a multi-week project.
- **Arrays store elements in `__elements__` ProtoList**, not as string-keyed attributes. Three sites still iterated via `getAttribute(indexKey(i))` and silently returned nullptr — fixed in #3, #4, #18 (hasOwnProperty, propertyIsEnumerable, OP_append). A future audit could find similar lingering sites.
- **Accessor sidecars** are stored under `__get_<name>__` / `__set_<name>__`. The object-literal `{ get foo() {} }` form was bypassing this convention (fix #10); the `Object.defineProperty(..., {get:...})` form already used it. Fix #10 unifies them.

## How to Run

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
PROTOJS=$PWD/build_release/protojs \
TEST262_ROOT=/home/gamarino/Documentos/proyectos/test262 \
TEST262_USE_PROTO_EVAL=1 \
TEST262_CONCURRENCY=10 \
PROTOCORE_GC_CONTEXT_THRESHOLD=1000000000 \
  node tests/test262/runner/test262_runner.js
```

Wall-clock on a 12-core machine: ~5:30–7 min for the full 46 963-test `language + built-ins` suite. Sequential fallback (no env var or `=1`) is unchanged.

Per-test stdout is suppressed in parallel mode; progress prints every `TEST262_PROGRESS_EVERY` tests (default 500). `TEST262_VERBOSE=1` restores per-test output.

## Methodology Notes

- **Pass rate ≠ ECMA conformance score.** Pass rate here is `passed / total`, where `total` includes `failed_syntax`, `failed_semantics`, `timeout`, and `skipped`. Some "syntax failures" are tests using ES2024+ syntax that the QuickJS frontend doesn't parse — those aren't conformance failures of the runtime; they're a known frontend limitation.
- **Test262 root is pinned** to `../test262` (a sibling repo).
- **`PROTOCORE_GC_CONTEXT_THRESHOLD=1e9`** suppresses GC during the run so test timing is dominated by test code, not GC.
- **Skip list:** `tests/test262/config/skip_proto_eval.json` records 11 tests that hang or crash protoJS in ways unrelated to conformance.

## Next Steps

1. **`language/expressions/object` (519 remaining fails)** — most are destructuring + async-generator forms (`dstr/async-gen-meth-*`), which need destructuring rest + member-target destructuring + async generators. Deep work; a separate cycle.
2. **`language/statements/for-await-of` (1128 remaining fails)** — async iteration protocol; full async generator support needed.
3. **`built-ins/Iterator` + `built-ins/Promise`** — closely related; iterator protocol + Promise microtask plumbing.
4. **Run the suite weekly.** The parallel runner means a full pass is ~7 min; weekly cadence catches regressions before they entrench.
5. **Re-baseline `CONFORMANCE_JS.md`.** That per-sub-category file hasn't been updated in this cycle; the per-category numbers in this doc supersede it.

## Historical Context

- **2026-03-18:** 94.4 % overall claim — superseded as a false positive; the binary at that time had assertion bugs in `assert.throws` / `sameValue` that inflated the pass count.
- **2026-04-10 (Phase 13):** 87.1 % on `language/statements` only. First honest measurement.
- **2026-05-11:** 57.55 % on `language + built-ins` (27 025 / 46 963).
- **2026-06-01 (morning, cycle 1 — 6 commits):** 58.70 % (27 565 / 46 963).
- **2026-06-01 (afternoon, cycle 2 — 20 language-fix commits):** **59.37 % (27 884 / 46 963).**
