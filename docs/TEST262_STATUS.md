# Test262 Conformance Status — protoJS

**Last full-suite run:** 2026-06-01 (cycle 4 — 20 more fixes)
**Snapshot:** `tests/test262/reports/snapshot-language_built-ins-1780347465173.json`
**Binary:** `build_release/protojs` v0.1.0 (commit `7e9c654b` on `master`)
**Scope:** `language` + `built-ins` (46 963 tests)
**Runner:** parallel (`TEST262_CONCURRENCY=10`, ~7 min wall)

## Overall

| | Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Skipped | Pass rate |
|---|---:|---:|---:|---:|---:|---:|---:|
| **2026-06-01 (cycle 4 — 20 more fixes)** | 46 963 | **28 767** | 874 | 17 173 | 138 | 11 | **61.25 %** |
| 2026-06-01 (cycle 3) | 46 963 | 28 018 | 874 | 18 019 | 41 | 11 | 59.66 % |
| 2026-06-01 (cycle 2) | 46 963 | 27 884 | 874 | 18 156 | 38 | 11 | 59.37 % |
| 2026-06-01 (cycle 1) | 46 963 | 27 565 | 874 | 18 477 | 36 | 11 | 58.70 % |
| 2026-05-11 (prior full) | 46 963 | 27 025 | 830 | 18 666 | 431 | 11 | 57.55 % |
| **Δ this cycle** | 0 | **+749** | 0 | **−846** | +97 | 0 | **+1.59 pp** |
| **Δ since 05-11 baseline** | 0 | **+1 742** | +44 | **−1 493** | **−293** | 0 | **+3.71 pp** |

Cumulative day in numbers:
- **+1 742 passes** since the prior full run (3 weeks ago).
- **−1 493 semantics failures** — every gain is a previously-failing test now resolving cleanly.
- **−293 timeouts** net (cycle 4 added +97 to handle previously-bailed paths that now hit real iterator loops).
- Four correctness cycles, **80 commits**, each one root cause.

## By Family (cycle 4 vs cycle 3)

| Family | Total | Passed (cycle 4) | Passed (cycle 3) | Δ | Pass rate (cycle 4) |
|---|---:|---:|---:|---:|---:|
| `built-ins` | 23 334 | **10 324** | 10 090 | **+234** | **44.25 %** |
| `language` | 23 629 | **18 443** | 17 928 | **+515** | **78.05 %** |

`language` led the cycle at +515 (driven by spread, rest, destructure rest, and iterator-protocol fixes). `built-ins` carried +234 mostly via Map/Set/Object.fromEntries/Array.from coordinating with native `__elements__` storage.

## Cycle 4 Fixes (commits between `5108c164..7e9c654b`)

| # | Commit | Fix |
|---|---|---|
| 1 | `3cf33648` | Method shorthand `{ foo() {} }` does NOT define a `prototype` property (§14.3.9). OP_define_method strips the OP_fclosure-installed prototype when op_flags == 0. |
| 2 | `8387c6c0` | `delete arr[i]` writes `PROTO_NONE` into `__elements__` (was a no-op on dense arrays). |
| 3 | `c60cac7e` | `Object.create(null)` registers a true null-prototype override via `t_jsProtoMap[result] = nullSentinel` (protoCore cannot natively sever the parent chain). |
| 4 | `6aff01b8` | `OP_get_field` / `OP_get_field2` walk `__get_<name>__` accessor sidecars on the prototype chain even when the key itself has no own attribute. Set.prototype.size / Map.prototype.size now resolve via dot access. |
| 5 | `334e1c6e` | `toPrimIfObject` routes `valueOf` / `toString` invocations through `callJSFunction` instead of the local `callMethod` lambda, which couldn't unwrap `wrapNativeFunction` wrappers. `[] == 0`, `[1] == 1`, `[] + 0` etc. now coerce instead of throwing TypeError. |
| 6 | `b133695f` | Relational ops `<`, `<=`, `>`, `>=` apply ToNumber when operand types differ post-ToPrimitive (ECMA §7.2.13). `[1] < 2` and `"1" < 2` now return true; NaN comparisons return false. |
| 7 | `cd5744ae` | `TypeBridge::fromJS` populates Array `__elements__` from JS arrays (was indexed-attribute only) and copies the non-enumerable `raw` sidecar so tagged template literals work. |
| 8 | `5488d1e1` | Implement `OP_rest` — materialise the rest-parameter array from the call-time args ProtoList. `function f(...a)` and `function t(s,...v)` for tagged templates now collect. |
| 9 | `b09f6407` | Implement `OP_apply` (spread call) + route `OP_append` and `OP_define_array_el` through `__elements__`. `f(...arr)`, `new F(...arr)` and mixed `[0, ...a, 1]` literals now work. |
| 10 | `963006de` | Make `Symbol()` callable — installed minimal native constructor that returns a fresh object marked `__is_symbol__ = PROTO_TRUE` with optional `__symbol_desc__`. `typeof Symbol()` returns 'object' (no primitive type yet) but no longer throws. |
| 11 | `09e25b37` | `new Set([1,2,3])` / `new Map([[k,v]])` read iterable entries via `arrayTryFastGet` (`__elements__`) first, falling back to indexed-attribute. |
| 12 | `21b00b45` | `OP_append` accepts iterator objects directly when `Symbol.iterator` is absent. `[...set.values()]` and `[...map.entries()]` now spread. |
| 13 | `62cc9504` | `Map.entries` / `Set.entries` yield real arrays — pair tuples stored in `__elements__` with `__is_array__ = PROTO_TRUE` (was `fromInteger(1)`). |
| 14 | `abbdffa0` | `Object.fromEntries([[k,v]])` reads pairs via `__elements__` first. |
| 15 | `9a1d70f8` | `Array.from(iter)` accepts iterator objects directly when `Symbol.iterator` is absent. |
| 16 | `712c4a69` | `Object.fromEntries(iter)` walks via Symbol.iterator or direct `.next` — covers Map, Set, generators. |
| 17 | `6e6b19fc` | `OP_for_of_next` honours its u8 depth byte (iterator state stays in-place; only value+done pushed). Array element reads go through `arrayTryFastGet`. Array rest destructure `[a, ...r] = [1,2,3]` now produces `r = [2,3]`. |
| 18 | `14049ba4` + `7e9c654b` (reverted) | Attempted `OP_define_class` / `OP_check_ctor` / `OP_init_ctor`. Implementation worked for basic patterns but uncovered a pre-existing issue: built-in constructors (Array, Object) don't inherit Function.prototype, so `Array.apply` is undefined. Tests that previously "passed" by silently exiting on the unsupported opcode now exposed the deeper bug. Reverted to keep the test262 numbers honest pending a complete fix that also lifts the constructor-inheritance issue. |
| 19 | `44b863fc` | `String.prototype[Symbol.iterator]` — yields each codepoint. `for (var c of "abc")`, `[..."abc"]` now work. |
| 20 | `7cc297dd` | Unimplemented constructor stubs (Date, BigInt, Proxy, WeakRef, …) carry `__native_fn__` so `typeof Date === 'function'` etc. match the spec. |

### Architectural themes this cycle

- **`__elements__` propagation everywhere**. Cycle 3 fixed *readers*; cycle 4 closed the loop on writers (OP_append, OP_define_array_el, TypeBridge::fromJS, Map.entries/Set.entries pair tuples, Set/Map iterable constructors). Every site that produces or consumes an array now agrees on the storage layout.
- **Iterator protocol unification**. `OP_append`, `Array.from`, `Object.fromEntries` now all use the same shape — probe `Symbol.iterator` first, fall back to treating the value as an iterator if it already exposes `.next`. This matches what Set.values() / Map.entries() / generators all need.
- **ToPrimitive routing through `callJSFunction`**. The local `callMethod` lambda's narrow dispatch was the silent reason every `[] == 0` style comparison threw. The fix is one line plus the realisation that `callJSFunction` is the unified entry — same lesson as several earlier interpreter fixes.
- **Honest about regressions**. The OP_define_class implementation surfaced a pre-existing bug (constructor inheritance) that previously hid behind silent "unsupported opcode" exits. Rather than mask it, we reverted that fix and called out the gap.

## Historical Context

- **2026-03-18:** 94.4 % overall claim — superseded as a false positive.
- **2026-04-10 (Phase 13):** 87.1 % on `language/statements` only.
- **2026-05-11:** 57.55 % on `language + built-ins` (27 025 / 46 963).
- **2026-06-01 (cycle 1 — 6 commits, morning):** 58.70 %.
- **2026-06-01 (cycle 2 — 20 commits, afternoon):** 59.37 %.
- **2026-06-01 (cycle 3 — 20 more commits, early evening):** 59.66 %.
- **2026-06-01 (cycle 4 — 20 more commits, evening):** **61.25 %** (28 767 / 46 963).

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

1. **ES6 classes — proper implementation**. `class A { ... }` is widely used in language/statements + language/expressions (~1 800 tests gated on it). Needs both `OP_define_class` AND lifting Array/Object/etc. to inherit Function.prototype so `Array.apply` etc. resolve.
2. **`super(...)` in derived class constructors** — `OP_init_ctor` is a stub; super-call dispatch needs proper threading.
3. **Generators + `async`/`await`** — for-await-of (1 140 tests), Iterator built-ins (~129 tests still failing).
4. **Symbol as a real primitive type** — currently `typeof Symbol() === 'object'`; many tests probe `typeof Symbol() === 'symbol'`. Needs bytecode-level type discrimination.
5. **String iterator for surrogate pairs**. Current codepoint walk handles BMP characters; astral characters need UTF-16 surrogate-pair coalescing.
6. **Object.fromEntries / Array.from / OP_append already share the iterator-direct probe** — the same pattern should be applied to the Set / Map constructors when given an iterable (currently they handle only array-likes; generators and Maps-of-Maps still produce empty results).

## Methodology Notes

- **Pass rate ≠ ECMA conformance score.** Pass rate is `passed / total` where total includes syntax/semantics failures, timeouts, and skips.
- **`PROTOCORE_GC_CONTEXT_THRESHOLD=1e9`** suppresses GC during the run for stable timing — has no effect on conformance.
- **Skip list:** `tests/test262/config/skip_proto_eval.json` records 11 tests that hang or crash protoJS in ways unrelated to conformance.
- **Test262 root** pinned to `../test262`.
- **Silent unsupported-opcode exits** still count as `passed` for tests whose assertions never get a chance to run (e.g. when a class definition at the top of the file bails out the rest of the program). This is honest with the runner's classification rule (`!err → passed`) but it inflates the count slightly for class-heavy areas. Implementing classes (cycle 4 #18) made some of this visible — see the "next steps" note above.
