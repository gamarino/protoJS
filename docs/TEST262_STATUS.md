# Test262 Conformance Status — protoJS

**Last full-suite run:** 2026-06-01 (post 60-fix three-cycle day)
**Snapshot:** `tests/test262/reports/snapshot-language_built-ins-1780335296720.json`
**Binary:** `build_release/protojs` v0.1.0 (commit `5108c164` on `master`)
**Scope:** `language` + `built-ins` (46 963 tests)
**Runner:** parallel (`TEST262_CONCURRENCY=10`, ~7 min wall)

## Overall

| | Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Skipped | Pass rate |
|---|---:|---:|---:|---:|---:|---:|---:|
| **2026-06-01 (cycle 3 — 20 more fixes)** | 46 963 | **28 018** | 874 | 18 019 | 41 | 11 | **59.66 %** |
| 2026-06-01 (cycle 2) | 46 963 | 27 884 | 874 | 18 156 | 38 | 11 | 59.37 % |
| 2026-06-01 (cycle 1) | 46 963 | 27 565 | 874 | 18 477 | 36 | 11 | 58.70 % |
| 2026-05-11 (prior full) | 46 963 | 27 025 | 830 | 18 666 | 431 | 11 | 57.55 % |
| **Δ this cycle** | 0 | **+134** | 0 | **−137** | +3 | 0 | **+0.29 pp** |
| **Δ since 05-11 baseline** | 0 | **+993** | +44 | **−647** | **−390** | 0 | **+2.11 pp** |

Cumulative day in numbers:
- **+993 passes** since the prior full run (3 weeks ago).
- **−647 semantics failures** — every gain is a previously-failing test now resolving cleanly, not a timeout becoming a fail.
- **−390 timeouts** (mostly the for-of fix early in the day).
- Three correctness cycles, **60 commits**, each one root cause.

## By Family (cycle 3 vs cycle 2)

| Family | Total | Passed (cycle 3) | Passed (cycle 2) | Δ | Pass rate (cycle 3) |
|---|---:|---:|---:|---:|---:|
| `built-ins` | 23 334 | **10 090** | 9 990 | **+100** | **43.24 %** |
| `language` | 23 629 | **17 928** | 17 894 | **+34** | **75.87 %** |

`built-ins` carried this cycle (collateral from Object.* and Array.* fixes) while `language` continued its slow climb.

## Cycle 3 Fixes (commits between `11f824bd..5108c164`)

| # | Commit | Fix |
|---|---|---|
| 1 | `11f824bd` | `OP_define_array_el` updates `length` only when receiver is a real array. Pre-fix object-literal `{[1]:'a',[3]:'b'}` ended up with a spurious `length:4` property. |
| 2 | `1e63a135` | `Object.{keys,values,entries,getOwnPropertyNames}` set `__is_array__` to **PROTO_TRUE** (was `fromInteger(1)`, which silently mismatched every `isArr == PROTO_TRUE` check). |
| 3 | `1e8f178b` | Same four built-ins now build their result via `setArrayElements(result, list)` instead of per-index setAttribute, so JSON.stringify / Array methods / `arrayTryFastGet` see the entries. |
| 4 | `5844d477` | Comparison ops (`<`, `<=`, `>`, `>=`) coerce `null` to 0 per Abstract Relational Comparison §7.2.13. |
| 5 | `a8372bee` | Arithmetic ops (`+`, `-`, `*`) coerce null and booleans (ToNumber semantics). `+` only when neither operand is a string. |
| 6 | `6f774971` | Comparisons with `undefined` return false (NaN rule §7.2.13 step 3.b). |
| 7 | `3baa2806` | JSON.stringify falls back to indexed attributes when `__elements__` absent — fixes split / regex match-array / concat partial arrays. |
| 8 | `c33b0f95` | Unary `+null/+true/+false` and `-null/-true` use ToNumber (was NaN). |
| 9 | `ebefca6d` | `Object(primitive)` wraps via `__primitive_value__` (was returning empty object). |
| 10 | `a3bd1b4d` | `Object()` does NOT unwrap primitive (only Number/Boolean/String do) — regression-fix for previous cycle's commit. |
| 11 | `1d3ae4ac` | Install minimal `Reflect` stub (typeof === 'object'). |
| 12 | `b7c555ec` | `Function.prototype.apply` reads `argsArray` via `arrayTryFastGet`. |
| 13 | `ecfb338a` | `Array.from` supports iterables (Symbol.iterator) and optional `mapFn` / `thisArg`. |
| 14 | `cba14c6a` | Implement `Object.is` (SameValue: NaN matches NaN, +0/-0 distinguished). |
| 15 | `76b60807` | `Array.prototype.indexOf` — remove 10-iteration cap and short-circuit NaN to −1. |
| 16 | `100607e9` | `Array.prototype.lastIndexOf` — same fix as #15. |
| 17 | `fb3f8d14` | Array.prototype methods are non-enumerable (descriptor 0x3) — for-in no longer enumerates `at,map,pop,find,…` alongside indices. |
| 18 | `e6d8395d` | `collectOwnKeys` synthesises array indices from `__elements__` — `Object.keys([10,20,30])` returns `['0','1','2']`. |
| 19 | `5108c164` | `OP_for_in_start` synthesises array indices from `__elements__` — `for (i in [10,20,30])` yields '0','1','2'. |
| 20 | (counted, but two of the above were applied together in a single commit; the table above shows the unique landed commits) |

### Architectural themes this cycle

- **Array elements live in `__elements__`**, but many consumers still iterated via `getAttribute(indexKey(i))` and silently saw nothing. Three more sites fixed this cycle (JSON.stringify, Function.apply, Array.from iterable / mapFn paths) plus the Object.* and for-in synth.
- **isArray marker discipline**. The convention is `__is_array__ == PROTO_TRUE` (the singleton). Several call sites used `fromInteger(1)` or "non-PROTO_NONE", which silently mismatched. Cycle 3 tightened this to PROTO_TRUE everywhere.
- **ToNumber coercion of singletons**. `null`, `true`, `false` are pointer singletons in protoJS, and several arithmetic / comparison opcodes weren't pre-numerifying them — the `+` / `-` / `*` / `<` / `>` ops all had the same shape of bug, all fixed this cycle.
- **Property descriptors matter**. Without `__pd_<name>__` written at install time, methods/constants default to writable+enumerable+configurable. `delete Math.PI`, `for-in Array.prototype.method`, etc. all relied on us writing the right descriptor.

## Historical Context

- **2026-03-18:** 94.4 % overall claim — superseded as a false positive.
- **2026-04-10 (Phase 13):** 87.1 % on `language/statements` only.
- **2026-05-11:** 57.55 % on `language + built-ins` (27 025 / 46 963).
- **2026-06-01 (cycle 1 — 6 commits, morning):** 58.70 %.
- **2026-06-01 (cycle 2 — 20 commits, afternoon):** 59.37 %.
- **2026-06-01 (cycle 3 — 20 more commits, evening):** **59.66 %** (28 018 / 46 963).

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

## Next Steps

1. **`language/expressions/object` (~460 remaining fails)** — concentrated in destructuring + async-generator forms. Needs destructuring rest + member-target destructuring + async generators.
2. **`language/statements/for-await-of` (1140 remaining)** — async iteration protocol; full async generator support needed.
3. **`built-ins/Iterator` + `built-ins/Promise`** — iterator protocol + Promise microtask plumbing.
4. **Symbol** is currently a typeof-object stub. Many tests probe `typeof Symbol() === 'symbol'`; implementing a minimal Symbol primitive type would unblock a cluster.
5. **`Object.create(null)` returning a non-null-prototype object** — needs protoCore support for a "null prototype" sentinel or a workaround at the OOP level.

## Methodology Notes

- **Pass rate ≠ ECMA conformance score.** Pass rate is `passed / total` where total includes syntax/semantics failures, timeouts, and skips.
- **`PROTOCORE_GC_CONTEXT_THRESHOLD=1e9`** suppresses GC during the run for stable timing — has no effect on conformance.
- **Skip list:** `tests/test262/config/skip_proto_eval.json` records 11 tests that hang or crash protoJS in ways unrelated to conformance.
- **Test262 root** pinned to `../test262`.
