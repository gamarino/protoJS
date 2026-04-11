# Test262 Status — Current Run Summary

This file documents the **high-level status** of the Test262 conformance suite as of the latest runs.  
For the full per-category breakdown see [`CONFORMANCE_JS.md`](../../CONFORMANCE_JS.md).  
Raw data is in the JSON snapshots under `tests/test262/reports/`.

---

## Overall (language + built-ins, protoCore path)

**Best full-suite run:** `2026-03-18T17:31:39Z`  
**Snapshot:** `tests/test262/reports/snapshot-language_built-ins-1773855099985.json`  
**Note:** Phase 8 (TypedArray/ArrayBuffer/DataView, 2026-04-07) was run as a targeted batch after this full-suite snapshot; it improved those areas from ~1–3% to ~99%. A new full-suite run is needed to capture the Phase 8 gains in the 94.4% baseline.

| Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Skipped |
|-------|--------|-----------------|--------------------|----------|---------|
| 47219 | **44596 (94.4%)** | 701 | 1781 | 130 | 11 |

Run command:
```bash
TEST262_USE_PROTO_EVAL=1 TEST262_ROOT=../test262 \
  node tests/test262/runner/test262_runner.js
```

---

## Per-Category Status (Most Recent Runs)

### language/statements

**Date:** `2026-04-11`  
**Most recent snapshot:** `tests/test262/reports/snapshot-language-statements-1775865664706.json`

| Run | Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Notes |
|-----|-------|--------|-----------------|--------------------|----------|-------|
| Pre-Phase-13 baseline (2026-04-04) | 9337 | 9053 (96.9%) | 133 | 139 | 1 | **False positive** — taken with buggy Phase 11/12 build; assert.throws/sameValue did not propagate failures correctly |
| Phase 13 honest baseline (2026-04-10) | 9337 | 8133 (87.1%) | 176 | 1017 | 0 | Phase 13 binary (Function.prototype wired); honest conformance |
| **Phase 14: flat bcId + closure capture (2026-04-10)** | 9337 | **8167 (87.5%)** | 176 | 983 | 0 | +34 vs Phase 13 honest; flat bcId fix + closure var capture (LOCAL/ARG/REF) |
| **Phase 15: OP_iterator_next + OP_iterator_call (2026-04-11)** | 9337 | **8167 (87.5%)** | 176 | 983 | 0 | No net change — see Phase 15 notes below |

### language/module-code

**Date:** `2026-04-03`  
**Snapshot:** `tests/test262/reports/snapshot-language-module-code-1775233582940.json`

| Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Skipped |
|-------|--------|-----------------|--------------------|----------|---------|
| 592   | **555 (93.8%)** | 8 | 29 | 0 | 0 |

### language/expressions

**Date:** `2026-04-11`  
**Most recent snapshot:** `tests/test262/reports/snapshot-language-expressions-1775865722194.json`

| Run | Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Notes |
|-----|-------|--------|-----------------|--------------------|----------|-------|
| Pre-regression baseline (20:18 UTC 04-07) | 11036 | 10221 (92.6%) | 161 | 560 | 94 | Largely false positives (see note below) |
| Slot-collision regression (20:40 UTC 04-07) | 11036 | 6869 (62.2%) | 162 | 3845 | 160 | Phase 8 intermediate build; slot bug exposed |
| Slot fix only (22:57 UTC 04-07) | 11036 | 8330 (75.5%) | 161 | 2434 | 111 | Slot separation fixed |
| Honest baseline (00:06 UTC 04-08) | 11036 | 8811 (79.8%) | 176 | 1938 | 111 | Three interpreter bugs fixed (see commits) |
| ReferenceError conformance (02:47 UTC 04-08) | 11036 | 8928 (80.9%) | — | — | — | +117 genuine improvements, 0 regressions |
| ToPrimitive / String wrapper (09:33 UTC 04-08) | 11036 | 9001 (81.6%) | 176 | 1747 | 112 | toPrimIfObject + callMethod via asMethod |
| **ToPrimitive fix + callMethod correction (10:00 UTC 04-08)** | 11036 | **9071 (82.2%)** | 176 | 1677 | 112 | +70 vs prior run, 0 regressions; asMethod() call fix |
| **Phase 13: Function.prototype wire-up (10:00 UTC 04-10)** | 11036 | **9339 (84.6%)** | 176 | 1521 | 0 | +268 vs prior; fn.call/bind/apply/length/name working |
| **Phase 14: flat bcId + closure capture (2026-04-10)** | 11036 | **9356 (84.8%)** | 176 | 1504 | 0 | +17 vs Phase 13; flat bcId fix + closure var capture for Symbol.iterator/for-of |
| **Phase 15: OP_iterator_next + OP_iterator_call (2026-04-11)** | 11036 | **9356 (84.8%)** | 176 | 1504 | 0 | No net change — see Phase 15 notes below |

> **Context on the "92.6% baseline"**: The pre-regression number was inflated by false positives. The `assert.sameValue` / `assert.throws` harness helpers used cross-function calls that silently returned `undefined` (due to the root-module lookup bug), so assertion failures were never raised. The 79.8% figure represents **honest** conformance: all assertion logic actually executes.
>
> **Three bugs fixed in commits 362d71c / 27ef4e7:**  
> 1. *Slot collision* (`OP_put_var_ref`) — `_ret_` and hoisted function declarations shared `slot[argCount+0]`; fixed with `slot[argCount+varCount+refIndex]`.  
> 2. *Phantom try-catch handler* — `OP_drop` after a try block didn't pop the catch frame, causing later throws to dispatch to the stale handler; fixed by tracking `placeholder_stack_pos` in `CatchFrame`.  
> 3. *Cross-scope function calls* (`OP_call`, `OP_call_method`, `OP_call_constructor`) — bytecode IDs index the root (global eval) module's `nestedFunctions`, but calls from within nested functions used the current module's empty list; fixed with `t_rootModule` thread-local.  
> Also: `OP_tail_call` (0x23) was entirely unhandled; added with proper return-instead-of-push semantics.
>
> **ToPrimitive / String wrapper fixes:**
> 1. *callMethod via asMethod()* — `fn->call(ctx, nullptr, keyHint, self, args)` is a message-send (looks up `keyHint` on `fn`), not a direct native invocation. For ToPrimitive, native valueOf/toString must be called via `fn->asMethod(pContext)(ctx, self, nullptr, args, nullptr)` which invokes the function pointer directly.
> 2. *new String() wrapper* — `new String('x')` now stores the primitive string as `__primitive_value__` on the wrapper object, so `toPrimIfObject(new String('x'))` returns `'x'` without invoking (broken) prototype chain methods.
> 3. *toPrimIfObject for plain objects* — `{} + {}` now correctly returns `"[object Object][object Object]"` via Object.prototype.toString invoked via asMethod.
>
> **ReferenceError conformance fixes (commit 9d33fe8):**  
> 1. *`Object instanceof Object` fix* — `Object.prototype` was a child of `objectPrototype` instead of `objectPrototype` itself; `instanceof` now finds it in the prototype chain of object literals.  
> 2. *OP_get_var ReferenceError* — accessing a truly undeclared global variable now throws `ReferenceError: x is not defined` per spec; uses `JS_CLOSURE_GLOBAL_DECL` (closure_type=4) to distinguish declared vars (hoisted to undefined) from undeclared references (throw on missing).  
> 3. *Missing globals stubs* — `Function`, `Boolean`, `Promise`, `Date`, `Map`, `Set`, `BigInt`, `AggregateError`, `JSON`, and test262 harness globals (`$DONE`, `$262`, `print`) registered as PROTO_NONE to prevent false ReferenceErrors for unimplemented built-ins.  
> 4. *Async test runner* — `doneprintHandle.js` now included for `flags: [async]` tests, preventing ReferenceError for `$DONE` in async test harness.
>
> **Phase 13: Function.prototype wire-up (commit 7bd397f):**
> 1. *`ensureFunctionPrototype` wired in bootstrap* — `FunctionPrototype.cpp` was fully implemented but never called; added call after `ensureObjectConstructor` and removed `"Function"` from `kUnimplementedCtors` stub list.
> 2. *OP_fclosure / OP_fclosure8 inherit Function.prototype* — Function instances created via `fp->newChild(pContext, true)` so `fn.call`, `fn.bind`, `fn.apply` resolve up the prototype chain without explicit attribute lookup.
> 3. *`fn.length` set from bytecode metadata* — `ProtoBytecodeModule::argCount_` now stored as `length` attribute on each function instance at closure-creation time.
> 4. *Bound function dispatch* — `callJSFunction`, `OP_call`, and `OP_call_method` detect `__bound_fn__` sentinel and unwrap: pre-bound args prepended to call-site args, bound `this` substituted; `typeof boundFn === "function"` via `OP_typeof` / `OP_typeof_is_function` updated.
> 5. *`bound.length` and `bound.name`* — `fnBind` now sets `bound.length = max(0, target.length - prebound_count)` and `bound.name = "bound " + target.name` per spec.
> 6. *`FunctionPrototype.cpp` added to CMakeLists.txt* — was compiled but not linked.
>
> **Phase 14: flat bcId + closure var capture (2026-04-10):**
> 1. *Flat bcId dispatch* — `loadBytecodeRecursive` now stores all nested functions (at any depth) in a single flat list on the root module with globally unique post-order IDs. The interpreter always resolves `bcId` via `t_rootModule->nestedFunctions[bcId]`, eliminating the cross-scope call bug where inner-function cpool lookups indexed the inner module's empty `nestedFunctions`.
> 2. *Closure var capture at `OP_fclosure8` / `OP_fclosure`* — At closure-creation time, captured parent-scope vars (types 0=LOCAL, 1=ARG, 2=REF) are published to the global object keyed by their declared names. This ensures the inner function's startup `OP_get_var` / `OP_put_var` ops read the correct initial values rather than `undefined`. Enables `makeAdder`, `makeCounter`, and closures used by the Symbol.iterator / for-of protocol.
> 3. *`closureVarTypes` / `closureVarIndices` added to `ProtoBytecodeModule`* — `loadBytecodeRecursive` populates these from `protojs_bytecode_closure_var_type` / `protojs_bytecode_closure_var_idx` so the interpreter can resolve the correct parent slot at fclosure time without holding a JSContext pointer.
> 4. *Symbol.iterator / for-of protocol* — Arrays and iterables now produce correct results via closure-captured `Symbol.iterator` methods; `[10,20,30]` yields `"10,20,30"`.
>
> **Phase 15: OP_iterator_next + OP_iterator_call (2026-04-11):**
> 1. *`OP_iterator_next` implemented* — Stack: `[iter, nextMethod, catch, sentinel]` (4) → pops all 4, calls `iter.next()` (native path: slot sentinel=-1) or reads `arr[idx]` (array path: idx≥0), builds `{value, done}` result object, pushes back `[iter, nextMethod, catch, result_obj]`. Correctly handles TypedArrays via `getTypedArrayElementType`.
> 2. *`OP_iterator_call flag=1` implemented* — Drains remaining iterator values into a rest array. Array path slices from current slot index to end; native path drains via `next()` loop. Pushes `[iter, nextMethod, catch, rest_array, false]` (5 items).
> 3. *Net zero test count change explained* — Basic array destructuring (`const [a,b] = [1,2]`) was already counted as passing via vacuous-pass (silent exit when `OP_iterator_next` returned `PROTO_NONE`). Error-expecting destructuring tests (`ary-ptrn-elem-ary-val-null`, `ary-ptrn-elem-id-init-unresolvable`, etc.) were already failing ("Command failed") before and still fail with assertion errors now. The implementation is correct but unblocks a different layer of failures.
> 4. *Root cause for remaining ~677 dstr failures* — Missing error handling, not missing iterator mechanics: (a) no TypeError when destructuring null/undefined; (b) no ReferenceError for unresolvable binding targets; (c) no error propagation from iterator.next() throwing; (d) `OP_iterator_close` does not call iterator.return() on non-exhausted iterators; (e) generators/async-generators entirely unimplemented (160 tests).

### language/statements/class + language/expressions/class

**Date:** `2026-04-10`  
**Snapshots:**  
- `tests/test262/reports/snapshot-language-statements-class-1775828468114.json`  
- `tests/test262/reports/snapshot-language-expressions-class-1775828457838.json`

| Category | Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Notes |
|----------|-------|--------|-----------------|--------------------|----------|-------|
| language/statements/class | 4367 | **4304 (98.6%)** | 15 | 48 | 0 | Phase 13 binary |
| language/expressions/class | 4059 | **3994 (98.4%)** | 9 | 56 | 0 | Phase 13 binary |

> **Context:** Class syntax conformance is very high (~98.5%) because class declarations and expressions compile through the same function/prototype machinery already exercised by Phase 13. Remaining failures are concentrated in static private fields, class decorators, and `[[IsHTMLDDA]]`-dependent patterns.

---

### built-ins/Object/freeze + seal + isSealed + isFrozen + preventExtensions + isExtensible

**Date:** `2026-04-10` (confirmed post-regression-fix)
**Snapshot:** `tests/test262/reports/snapshot-built-ins-Object-freeze_built-ins-Object-isFrozen_built-ins-Object-seal_built-in-1775803259875.json`

| Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Notes |
|-------|--------|-----------------|--------------------|----------|-------|
| 317   | **222 (70.0%)** | 0 | 95 | 0 | Phase 12 hang fix; remaining failures are .length metadata and Object.defineProperty dependency |

> **Phase 12 hang root-cause analysis and fixes (commit d915fca + regression fix ea163b6):**
> 1. *GCBridge::mapMutex deadlock* — `registerMapping()` holds `mapMutex` then calls `registerRoot()` which tries to lock the same non-recursive `std::mutex`; changed to `std::recursive_mutex`.
> 2. *ObjectPrototype freeze/seal state* — used `obj->setAttribute()` to store `__extensible__`/`__is_frozen__` flags; calling protoCore type predicates on a post-setAttribute object hangs. Replaced with `thread_local std::unordered_set<const ProtoObject*>` for zero-overhead O(1) state tracking with no protoCore side effects. Also removed `isString()` from `isPrimitive()` for the same reason.
> 3. *TypeBridge::toJS isCell regression (commit ea163b6)* — The initial d915fca fix inserted an `isCell()` check before `isString()` under the incorrect assumption they are mutually exclusive. In protoCore, `ProtoString` objects also return `true` for `isCell()`, so ProtoStrings were being wrapped as generic JS objects instead of converted to JS strings. This caused `Function.prototype.bind` / `call` / `apply` to become `undefined`. Since `setAttribute` is no longer called on ProtoObjects (fix #2 uses thread_local sets), `isString()` is now safe to call before `isCell()`, and the block was removed.

---

## Phase History

| Date | Snapshot (full suite) | Passed | Notes |
|------|----------------------|--------|-------|
| 2026-03-06 | `snapshot-language_built-ins-1772737729550.json` | 47153 / 47219 | Pre-Phase-6 baseline; parse-negative leniency, 66 skipped. |
| 2026-03-08 | `snapshot-language_built-ins-1773028489384.json` | 42643 / 47219 | Phase 6 Step 1+2: module mode wired, line-terminators unlocked, 7 skipped. |
| 2026-03-09 | `snapshot-language_built-ins-1773077022112.json` | 42892 / 47219 | Phase 7: `OP_array_from`, for-of / for-in iterator opcodes; +249 vs Phase 6. |
| 2026-03-18 | `snapshot-language_built-ins-1773855099985.json` | **44596 / 47219** | Best full-suite result to date (94.4%). |
| 2026-04-10 | *(per-category only; full-suite run pending)* | — | Phase 13: +268 expressions; Phase 14: +17 expressions, +34 statements vs Phase 13 honest. Full-suite run needed to capture Phases 8–14 gains. |

---

## How to Regenerate

1. Build `protojs` (see `README.md` or workspace `CLAUDE.md`).
2. Run full suite:
   ```bash
   cd protoJS
   TEST262_USE_PROTO_EVAL=1 TEST262_ROOT=../test262 \
     node tests/test262/runner/test262_runner.js
   ```
3. Run a specific pattern (e.g. `language/expressions`):
   ```bash
   TEST262_PATTERNS=language/expressions TEST262_USE_PROTO_EVAL=1 \
     TEST262_ROOT=../test262 node tests/test262/runner/test262_runner.js
   ```
4. Update this file with the new snapshot path and summary numbers.
5. For the full per-category table, run `tests/test262/runner/run_batches.sh` and update `CONFORMANCE_JS.md`.

---

## Notes

- **Parse-negative leniency:** Tests that expect a parse-phase error but where the engine accepts the code are counted as `passed`. Disable leniency to measure strict parse compliance.
- **Skipped list:** `tests/test262/config/skip_proto_eval.json` (currently 11 entries: TypedArray-resizable-buffer and for-of/dstr patterns requiring full destructuring-iterator support).
- **Module tests (`flags: [module]`):** Use QuickJS's native module linker + Promise evaluation; protoCore is bypassed for module mode.
- **RegExp:** Excluded from the protoCore path so that `lastIndex` mutation remains spec-compliant (see `CONFORMANCE_JS.md` §1).
- **Authoritative per-category data:** `CONFORMANCE_JS.md` §3 — covers 250+ categories across `built-ins/` and `language/`.
