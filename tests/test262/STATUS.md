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

**Date:** `2026-04-12`  
**Most recent snapshot:** `tests/test262/reports/snapshot-language_built-ins-1775967903551.json` (full language+built-ins run; statements extracted: 7933/9337)

| Run | Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Notes |
|-----|-------|--------|-----------------|--------------------|----------|-------|
| Pre-Phase-13 baseline (2026-04-04) | 9337 | 9053 (96.9%) | 133 | 139 | 1 | **False positive** — taken with buggy Phase 11/12 build; assert.throws/sameValue did not propagate failures correctly |
| Phase 13 honest baseline (2026-04-10) | 9337 | 8133 (87.1%) | 176 | 1017 | 0 | Phase 13 binary (Function.prototype wired); honest conformance |
| **Phase 14: flat bcId + closure capture (2026-04-10)** | 9337 | **8167 (87.5%)** | 176 | 983 | 0 | +34 vs Phase 13 honest; flat bcId fix + closure var capture (LOCAL/ARG/REF) |
| **Phase 15: OP_iterator_next + OP_iterator_call (2026-04-11)** | 9337 | **8167 (87.5%)** | 176 | 983 | 0 | No net change — see Phase 15 notes below |
| **Phase 16: destructuring error handling (2026-04-11)** | 9337 | **8239 (88.2%)** | 176 | 911 | 0 | +72 vs Phase 15; TypeError for null, exception propagation from callbacks, iterator.return() on close |
| **Phase 17: TypeError null/undef + error constructor identity (2026-04-11)** | 9337 | **6453 (69.1%)** | 177 | 2696 | 0 | +357 genuine (+357 P16-fail→pass), −2143 false-positives removed; see Phase 17 notes |
| **Phase 18: OP_in stack order + OP_put_field net effect (2026-04-11)** | 9337 | **6459 (69.2%)** | 177 | 2690 | 0 | +6 vs Phase 17; fixed OP_in always-true + OP_put_field stack over-push |
| **Phase 19: methodPrototype = Function.prototype (2026-04-11)** | 9337 | **7239 (77.5%)** | 177 | 1910 | 0 | +780 vs Phase 18; native fn.bind/call/apply now work via methodPrototype |
| **Phase 20: object spread/rest, for-in, delete, Object.keys/values/entries/assign (2026-04-11)** | 9337 | **7208 (77.2%)** | 176 | 1942 | 0 | −31 apparent (−64 false-positives exposed, +33 genuine); see Phase 20 notes |
| **Phase 21: TDZ sentinel fix, OP_append (spread in array literals), instanceof TypeError (2026-04-11)** | 9337 | **7229 (77.4%)** | 176 | 1921 | 0 | +21 net (+21 genuine); see Phase 21 notes |
| **Phase 22: Function.prototype→Object.prototype chain, non-enumerable fn.name/length/prototype (2026-04-11)** | 9337 | **7258 (77.7%)** | 176 | 1892 | 0 | +29 vs Phase 21; see Phase 22 notes |
| **Phase 23: OP_set_name/OP_set_name_computed fn.name descriptor + OP_put_array_el writable check (2026-04-11)** | 9337 | **7286 (78.0%)** | 176 | 1864 | 0 | +28 vs Phase 22; see Phase 23 notes |
| **Phase 24: strict mode directive placement + TDZ check in OP_get_var_ref0/1/2/3 (2026-04-11)** | 9337 | **7286 (78.0%)** | 176 | 1864 | 0 | +0 vs Phase 23 (gains in expressions only); see Phase 24 notes |
| **Phase 25: NaN equality, accessor property getter/setter, String.concat toString (2026-04-11)** | 9337 | **7318 (78.4%)** | 176 | — | — | +32 vs Phase 24; see Phase 25 notes |
| **Phase 26: Object.create prototype chain, Object.getPrototypeOf, GOPD own-only+accessor, Object.defineProperties (2026-04-11)** | 9337 | **7356 (78.8%)** | — | — | — | +38 vs Phase 25; see Phase 26 notes |
| **Phase 27: Synchronous Promise + async/await opcodes (2026-04-12)** | 9337 | **7933 (84.9%)** | — | — | 1 | +577 vs Phase 26; see Phase 27 notes |
| **Phase 28+28b: Array/Object TypeError propagation + native fn .length/.name + callback exception propagation (2026-04-12)** | 9337 | **TBD** | — | — | — | Full snapshot pending; Phase 28 changes primarily affect built-ins; see Phase 28 notes |
| **Phase 29: Object.prototype.toString tags + String/Number null guards + defineProperty validation (2026-04-12)** | 9337 | **TBD** | — | — | — | Targeted snapshot for String/Number/Object.defineProperty/Function.prototype areas (3354 tests: 838 passing); see Phase 29 notes |

### language/module-code

**Date:** `2026-04-03`  
**Snapshot:** `tests/test262/reports/snapshot-language-module-code-1775233582940.json`

| Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Skipped |
|-------|--------|-----------------|--------------------|----------|---------|
| 592   | **555 (93.8%)** | 8 | 29 | 0 | 0 |

### language/expressions

**Date:** `2026-04-12`  
**Most recent snapshot:** `tests/test262/reports/snapshot-language_built-ins-1775967903551.json` (full language+built-ins run; expressions extracted: 9295/11036)

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
| **Phase 16: destructuring error handling (2026-04-11)** | 11036 | **9401 (85.2%)** | 176 | 1459 | 0 | +45 vs Phase 15; TypeError for null, exception propagation from callbacks, iterator.return() on close |
| **Phase 17: TypeError null/undef + error constructor identity (2026-04-11)** | 11036 | **8315 (75.3%)** | 176 | 2545 | 0 | +261 genuine (+261 P16-fail→pass), −1347 false-positives removed; see Phase 17 notes |
| **Phase 18: OP_in stack order + OP_put_field net effect (2026-04-11)** | 11036 | **8343 (75.6%)** | 176 | 2517 | 0 | +28 vs Phase 17; fixed OP_in always-true + OP_put_field stack over-push |
| **Phase 19: methodPrototype = Function.prototype (2026-04-11)** | 11036 | **9125 (82.7%)** | 176 | 2735 | 0 | +782 vs Phase 18; native fn.bind/call/apply now work via methodPrototype |
| **Phase 20: object spread/rest, for-in, delete, Object.keys/values/entries/assign (2026-04-11)** | 11036 | **9108 (82.5%)** | 176 | 1752 | 0 | −17 apparent (−64 false-positives exposed, +47 genuine); see Phase 20 notes |
| **Phase 21: TDZ sentinel fix, OP_append (spread in array literals), instanceof TypeError (2026-04-11)** | 11036 | **9129 (82.7%)** | 176 | 1731 | 0 | +21 net (+25 genuine, −4 false-positives exposed); see Phase 21 notes |
| **Phase 22: Function.prototype→Object.prototype chain, non-enumerable fn.name/length/prototype (2026-04-11)** | 11036 | **9159 (83.0%)** | 176 | 1701 | 0 | +30 vs Phase 21; see Phase 22 notes |
| **Phase 23: OP_set_name/OP_set_name_computed fn.name descriptor + OP_put_array_el writable check (2026-04-11)** | 11036 | **9176 (83.1%)** | 176 | 1684 | 0 | +17 vs Phase 22; see Phase 23 notes |
| **Phase 24: strict mode directive placement + TDZ check in OP_get_var_ref0/1/2/3 (2026-04-11)** | 11036 | **9194 (83.3%)** | 176 | 1666 | 0 | +18 vs Phase 23; see Phase 24 notes |
| **Phase 25: NaN equality, accessor property getter/setter, String.concat toString (2026-04-11)** | 11036 | **9243 (83.8%)** | 176 | — | — | +49 vs Phase 24; see Phase 25 notes |
| **Phase 26: Object.create prototype chain, Object.getPrototypeOf, GOPD own-only+accessor, Object.defineProperties (2026-04-11)** | 11036 | **9263 (83.9%)** | — | — | — | +20 vs Phase 25; see Phase 26 notes |
| **Phase 27: Synchronous Promise + async/await opcodes (2026-04-12)** | 11036 | **9295 (84.2%)** | — | — | 0 | +32 vs Phase 26; see Phase 27 notes |
| **Phase 28+28b: Array/Object TypeError propagation + native fn .length/.name + callback exception propagation (2026-04-12)** | 11036 | **TBD** | — | — | — | Full snapshot pending; see Phase 28 notes |
| **Phase 29: Object.prototype.toString tags + String/Number null guards + defineProperty validation (2026-04-12)** | 11036 | **TBD** | — | — | — | Targeted snapshot; see Phase 29 notes |
| **Phase 40 (full snapshot 2026-04-15)** | 11036 | **9410 (85.27%)** | 176 | 1450 | 0 | Pre-migration baseline; snapshot `snapshot-language-expressions-1776233007636.json` |
| **Post-perf-and-migration baseline (2026-04-27)** | 11036 | **9171 (83.10%)** | 176 | 1689 | 0 | **−239 vs Phase 40 (−2.17 pp)**: 271 pass→fail, 32 fail→pass.  Snapshot `snapshot-language-expressions-1777325299257.json`.  Bisect points to commit `e2e6eaa` ("switch interpreter slot/stack from ProtoSparseList to flat array", 2026-04-26): default parameters in generator functions stop firing, so dstr/ tests that use generator wrappers all regress.  271 regressed tests cluster as: generators 99, function 44, arrow-function 44, assignment 27, compound-assignment 22, array 19.  Module migrations (http/worker_threads/Buffer/net committed 2026-04-27) do not touch the interpreter and are not implicated. |
| **Generator slot save/restore fix (2026-04-28)** | 11036 | **9229 (83.62%)** | 176 | 1631 | 0 | **+58 vs prior post-migration**, 0 new regressions.  Snapshot `snapshot-language-expressions-1777381509313.json`.  Recovered tests: 57 generators + 1 await.  Fix in commit `efe08748`: generator yield save sites now snapshot `automaticLocals` (the flat slot array) into `__gen_slots__` on the iterator, and runBytecode's resume branch restores those slots into the fresh childCtx — recovering default-bound parameters that were lost when the slot region moved off `closureLocals`.  Remaining gap vs Phase 40 is **−181 tests / −1.65 pp**: arrow-function and plain-function dstr/ tests where the formal parameter is itself a destructuring pattern (e.g. `([a,b,c]) => …`) still see undefined, an unrelated argument-binding bug also introduced by `e2e6eaa` and tracked separately. |

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
> **Phase 17: TypeError for null/undefined access + error constructor identity (2026-04-11):**
> 1. *`OP_get_field` / `OP_get_field2` null guard* — Reading a property on `undefined` or `null` now throws `TypeError: Cannot read properties of undefined/null (reading 'x')`. Previously the runtime silently returned `PROTO_NONE` for `undefined.x` because `t_nullSentinel` is a valid C++ pointer (truthy), bypassing the old `obj ?` ternary guard.
> 2. *`OP_get_array_el` / `OP_get_array_el2` / `OP_get_array_el3` null guard* — Same fix for bracket notation: `undefined[0]` and `null[0]` now throw TypeError.
> 3. *`OP_to_object` null guard* — Object destructuring `const {} = null` previously silently succeeded. The opcode now throws `TypeError: Cannot convert null/undefined to object` per spec.
> 4. *`OP_call_method` TypeError for non-callable* — `x.foo()` where `foo` is `undefined` now throws `TypeError: is not a function`. Previously it pushed `PROTO_NONE` onto the stack as a silent no-op return.
> 5. *Error constructor identity* — `ensureBuiltinErrorConstructors` now sets `ErrorClass.prototype.constructor = ErrorClass` for all built-in error types, so `e.constructor === TypeError` identity checks pass.
>
> **Phase 17 false-positive removal (−3490 tests vs Phase 16):**  
> The stricter TypeError checks exposed three categories of tests that were silently passing in Phase 16:
> - *Async/generator false positives (≈1300 tests)* — `async function f() {}; f()` returns `PROTO_NONE` (async not implemented). Calling `.then()` on this result via `OP_get_field` now throws `TypeError: Cannot read properties of undefined (reading 'then')` instead of silently returning undefined. Affected: `for-await-of` (1100), `async-generator` (~200), `async-function` (~30), `async-arrow-function` (~50).
> - *Class/propertyHelper false positives (≈1452 tests)* — The `propertyHelper.js` test262 harness captures `Function.prototype.call.bind(Array.prototype.join)` etc. at module load time. Since `.bind()` lookup returns `PROTO_NONE` on some function objects in this runtime, the bound helpers (`__join`, `__hasOwnProperty`, etc.) were all `PROTO_NONE`. In Phase 16, calling `PROTO_NONE(...)` via `OP_call_method` silently returned `undefined`, so `verifyProperty()` skipped all checks and tests "passed". In Phase 17, `OP_call_method` with a `PROTO_NONE` method throws TypeError, causing the tests to correctly fail. Affected: `class` (739 statements, 713 expressions), plus `dynamic-import` (93) and others.
> - *Other false positives* — Various tests that called methods on `undefined` results (e.g. from `Object.getOwnPropertyDescriptor` returning `undefined` for unimplemented cases) now throw instead of silently returning `undefined`.
>
> **Net assessment:** Phase 17 adds 618 genuine improvements (261 expressions + 357 statements) for tests that correctly verify TypeError behavior for `null.x`, `undefined.x`, `const {} = null`, and error constructor identity. The −3490 false-positive removals represent tests that were never truly passing — they were accepted by the old lax runtime even though the JS semantics were wrong. The next priority should be: (1) implement `Function.prototype.bind` fully on all function instances so `propertyHelper.js` harness works (recovers ~1452 class tests); (2) implement Promise/async so async tests pass for real.
>
> **Phase 26: Object.create prototype chain, Object.getPrototypeOf, GOPD own-only+accessor, Object.defineProperties (2026-04-11):**
> 1. *`Object.create(proto[, descriptors])` prototype chain* — `objectCreate` previously ignored its first argument and always called `ctx->newObject(true)`, creating an object with no parent. Now reads the first arg: if null/undefined → `ctx->newObject(true)` (null prototype); if a valid object → `proto->newChild(ctx, true)` (inherits from proto). Second argument (property descriptors) is now applied by iterating own enumerable properties and calling `objectDefineProperty` for each. Enables all `Object.create(SomeClass.prototype)` patterns used in inheritance and prototype-based OOP tests.
> 2. *`Object.getPrototypeOf(obj)` implemented* — Previously a stub returning `PROTO_NONE`. Now reads `obj->getPrototype(ctx)` from protoCore; returns the null sentinel if no prototype exists. Enables `Object.getPrototypeOf(obj) === proto` identity checks and `isPrototypeOf` patterns.
> 3. *`Object.getOwnPropertyDescriptor` own-only check* — Previously called `target->getAttribute(ctx, pk, false)` which traverses the prototype chain, so GOPD returned a descriptor for inherited properties (spec requires undefined for non-own). Now uses `target->hasOwnAttribute(ctx, pk)` first; returns `PROTO_NONE` (undefined) unless `PROTO_TRUE`. Also supports accessor descriptors: checks for `__get_<prop>__` and `__set_<prop>__` sidecars as own properties and returns `{get, set, enumerable, configurable}` instead of `{value, writable, ...}` for accessor properties.
> 4. *Property name coercion (`coercePropNameToString`)* — `Object.defineProperty` and `Object.getOwnPropertyDescriptor` previously returned early when the key was `undefined`, `null`, a boolean, or a floating-point number. Added `coercePropNameToString` helper (used by both) that converts: `undefined`→"undefined", `null`→"null", `boolean`→"true"/"false", `integer`→decimal string, `double`→decimal string. Enables `Object.defineProperty(obj, undefined, {...})` to create a property named "undefined".
> 5. *`Object.defineProperties(target, props)` implemented* — Was entirely missing (not registered). Iterates own enumerable properties of `props` and calls `objectDefineProperty` for each. Registered alongside `defineProperty`. Fixes 576+ `Object.defineProperties` failures and enables `Object.create(proto, descriptors)` second-arg support.
> 6. *Net gain* — +20 expressions, +38 statements, +358 overall vs Phase 25. Snapshot: `tests/test262/reports/snapshot-language_built-ins-1775962316298.json`.
>
> **Phase 27: Synchronous Promise + async/await opcodes (2026-04-12):**
> 1. *Synchronous `Promise` constructor and static methods* — `PromisePrototype.cpp` (new file) implements a fully synchronous Promise model compatible with the immutable protoCore object system. A pending promise is allocated with a unique cell key (`__promise_cell_N__`) stored in the global root. The executor is called synchronously; the resolve/reject callbacks read the current cell key from a thread-local stack (`t_activeCellKeyStack`) and write the settled state (`__promise_state__` = 1 or 2) and value (`__promise_value__`) into the cell. After the executor returns, the cell is read back and the settled promise replaces the pending placeholder. Implements: `new Promise(executor)`, `Promise.resolve(v)`, `Promise.reject(r)`, `Promise.all([...])`, `Promise.allSettled([...])`, `Promise.race([...])`, `Promise.any([...])`, `.then(onFulfilled, onRejected)`, `.catch(onRejected)`, `.finally(onFinally)`.
> 2. *`func_kind` exposed from QuickJS bytecode* — Added `protojs_bytecode_func_kind()` accessor in `quickjs.c` and `QuickJSBytecodeExport.h`. `ProtoBytecodeLoader.cpp` now populates `ProtoBytecodeModule::isAsync` (func_kind bit 2) and `isGenerator` (func_kind bit 1) from this accessor. `OP_fclosure8` and `OP_fclosure` mark async function closures with `__is_async__ = true`.
> 3. *`OP_initial_yield` skipped for async non-generators* — Async functions (non-generator) receive `OP_initial_yield` as a function preamble opcode. Previously this opcode returned `PROTO_NONE` (generator stub), terminating async function bodies immediately. Now, if `mod->isAsync && !mod->isGenerator`, the opcode is a no-op (`break`), allowing the function body to continue executing.
> 4. *`OP_await` implemented (synchronous unwrap)* — Pops the top-of-stack value; if it is a settled Promise with state=1 (fulfilled), replaces it with the fulfillment value; if state=2 (rejected), converts to a pending exception. Non-promise values are pushed back unchanged. This enables `await somePromise` and `await nonPromise` to produce the correct result in synchronous test scenarios.
> 5. *`OP_return_async` implemented* — Wraps the return value in a resolved Promise via `makeResolvedPromise()`, enabling `async function f() { return 42; }` to return `Promise { 42 }`.
> 6. *`__construct__` generic dispatch in `OP_call_constructor`* — Added fallback in the constructor dispatch path: if no specific constructor is recognized, looks up `__construct__` on the function object and calls it as a native method. This allows `ensurePromiseConstructor` to register the Promise constructor without adding a special-cased string check in the interpreter.
> 7. *`getCurrentGlobalRoot()` accessor added to `ProtoInterpreter.h`* — Exposes the thread-local `t_currentGlobalRoot` pointer so `PromisePrototype.cpp` can write settled state back to the global object from within native resolve/reject callbacks.
> 8. *Promise + async/await test counts (Phase 27 snapshot):* Promise: 461/656 (70.3%), async-function + async-generator: 666/1405 (47.4%), await-expression: 1305/2147 (60.8%). Major blockers for remaining failures: real microtask queue (`.then()` chains that require deferred callbacks), async generators (full generator suspension/resumption needed), and `for-await-of` (requires async iteration protocol).
> 9. *Net gain* — +32 expressions, +577 statements, +837 overall vs Phase 26. Full-suite pass rate: 28241/46963 (60.1%). Snapshot: `tests/test262/reports/snapshot-language_built-ins-1775967903551.json`.
>
> **Phase 29: Object.prototype.toString tags + String/Number prototype null guards + defineProperty validation (2026-04-12):**
> 1. *`Object.prototype.toString` null sentinel fix* — `toString.call(null)` now returns `"[object Null]"` instead of `"[object Object]"`. The null sentinel is a unique `ProtoObject*`; `isNone(ctx)` returns false for it (it is a non-None object), so the old code fell through to the default branch. Added explicit `self == getNullSentinel()` check before the type checks.
> 2. *`Object.prototype.toString` wrapped/bound function fix* — `toString.call(fn.bind({}))` and `toString.call(wrapNativeFunction(...))` now return `"[object Function]"`. Bound functions have `__bound_fn__` but no `__bytecode_id__` and `isMethod()` returns false. Wrapped native functions have `__native_fn__`. Both sentinel keys are now checked in the function branch.
> 3. *`Symbol.toStringTag` / `__toStringTag__` support in `objectToString`* — After array/function checks, the function looks up `__toStringTag__` via `JSSymbols::toStringTag(ctx)` with prototype chain traversal. If the value is a non-empty string, returns `"[object <tag>]"`. Enables `Object.prototype.toString.call(Math)` → `"[object Math]"`, `toString.call(new RegExp())` → `"[object RegExp]"`, and custom tags via `obj.__toStringTag__ = "Custom"`.
> 4. *`JSSymbols::toStringTag` added* — New symbol `__toStringTag__` registered in `JSSymbols.h`/`.cpp` with `DEFINE_SYMBOL` and `REGISTER` entries.
> 5. *`Math.__toStringTag__ = "Math"` set in `ensureMathObject`* — After all method registrations.
> 6. *`RegExp.prototype.__toStringTag__ = "RegExp"` set in `BuildRegExpPrototype`* — After all method registrations.
> 7. *`Object.defineProperty` primitive arg TypeError* — The existing null/undefined check was extended to include boolean, integer, double, float, and string primitives. `Object.defineProperty(5, 'x', {})` now throws `TypeError: Object.defineProperty called on non-object` per ES5 §15.2.3.6 step 1.
> 8. *`Object.defineProperty` accessor/data descriptor conflict TypeError* — After extracting getter/setter, if `isAccessor` is true, checks for `"value"` or `"writable"` keys in the descriptor using own-attribute lookup. If both accessor and data fields are present, throws `TypeError: Invalid property descriptor. Cannot both specify accessors and a value or writable attribute` per ES5 §8.10.5 step 9.
> 9. *`Object.defineProperties` non-object first arg TypeError* — The existing silent-skip was replaced with a TypeError throw for null, undefined, boolean, integer, double, float, or string first argument. Also throws if the Properties argument (second arg) is null or undefined.
> 10. *`String.prototype` null/undefined this guards* — All ~33 instance methods now call `requireStringThis()` at entry (helper added at top of `StringPrototype.cpp`, includes `ProtoInterpreter.h`). `String.prototype.trim.call(null)` throws `TypeError: String.prototype method called on null or undefined` per ECMA-262 §21.1 RequireObjectCoercible.
> 11. *`Number.prototype` null/undefined/non-numeric this guards* — `numberValueOf`, `numberToString`, `numberToFixed`, `numberToExponential`, `numberToPrecision` now call `requireNumberThis()` at entry. Throws TypeError for null, undefined, boolean, string, and plain objects that are not Number wrapper objects. Number wrappers (have `__primitive_value__` that is numeric) are accepted.
> 12. *Targeted snapshot results (3354 tests: String/Number/Object.defineProperty/defineProperties/Function.prototype/Object.prototype.toString):*
>     - `built-ins/String/prototype`: 405/1073 (37.7%) vs baseline 356/1073 (33.2%) → **+49**
>     - `built-ins/Number/prototype`: 17/168 (10.1%) → no change (tests focus on numeric output format, not this-guards)
>     - `built-ins/Object/defineProperty`: 164/1131 (14.5%) vs baseline 157/1131 (13.9%) → **+7**
>     - `built-ins/Object/defineProperties`: 106/632 (16.8%) → new area
>     - `built-ins/Function/prototype`: 137/309 (44.3%) → included for regression check
>     - `built-ins/Object/prototype/toString`: 9/41 (22.0%) → remaining failures need full Symbol.toStringTag + Date/Map/Set/Promise
>     - **Total: 838/3354 (25.0%)**
>     - Snapshot: `tests/test262/reports/snapshot-built-ins-String-prototype_built-ins-Number-prototype_built-ins-Object-definePro-1776000892445.json`
> 13. *Commits* — feat(phase29): `c027118`. fix toString: `59a934a`. __toStringTag__ on Math/RegExp: `30f194b`. defineProperty validation: `968953f`. defineProperties validation: `2481816`. String.prototype guards: `361da9d`. Number.prototype guards: `cd83c02`.
>
> **Phase 28 + 28b: Array/Object TypeError propagation, native fn .length/.name, callback exception fix (2026-04-12):**
> 1. *Native exception signaling infrastructure* — Added `signalNativeException()` / `makeNativeError()` / `hasCallException()` exported from `ProtoInterpreter.h`. The interpreter now checks `t_hasCallException` after every native-method call in `OP_call_method` and `OP_call`, so exceptions signaled from native code propagate correctly to JS instead of being silently discarded.
> 2. *Array method TypeError on null/undefined `this`* — All ~25 `Array.prototype` methods (forEach, map, filter, reduce, every, some, find, findIndex, sort, etc.) now call `arrayThrowIfNullUndefined()` at entry. Calling an array method on `null` or `undefined` throws `TypeError: Array method called on null or undefined`. Fixes tests like `Array.prototype.reduce.call(null, fn)`.
> 3. *Native function `.length` and `.name`* — New `wrapNativeFunction()` helper in `FunctionPrototype.cpp` creates a `ProtoObjectCell` child of `Function.prototype` with `__native_fn__`, `length`, and `name` attributes. All Array, Object, Number, and String constructor methods are now registered as wrappers so `Array.prototype.reduce.length === 1`, `Object.defineProperty.length === 3`, `typeof reduce === "function"`, etc. Bootstrap ordering was updated: `ensureFunctionPrototype` runs before all other prototype registrations so wrappers can use Function.prototype as their parent. Dispatch unwrapping added in `OP_call_method`, `OP_call`, `callJSFunction`, `OP_typeof`, and `OP_typeof_is_function`.
> 4. *`Object.defineProperty(null/undefined)` TypeError* — Added null/undefined check in `objectDefineProperty`; throws `TypeError: Object.defineProperty called on non-object`.
> 5. *Callback exception propagation in array iteration methods (Phase 28b)* — Array methods that invoke user callbacks previously swallowed exceptions set by `callJSFunction`'s `t_hasCallException` thread-local, leaving a stale flag that Phase 28's new `OP_call_method`/`OP_call` checks would pick up as spurious exceptions. Added `hasCallException()` check after every `callJSFunction` call inside forEach, map, filter, find, findIndex, findLast, findLastIndex, some, every, reduce, reduceRight. This eliminates 7 false-positive passes from Phase 27 and ensures callback exceptions propagate correctly per spec.
> 6. *False-positive analysis (Phase 28b)* — The 7 tests corrected: `every/15.4.4.16-7-c-ii-16.js` and `forEach/15.4.4.18-7-c-ii-16.js` (primitive `this.valueOf()` — Boolean/Number.prototype gap), `Error.prototype.toString/invalid-receiver.js` (toString non-object receiver not yet throwing), `Atomics/notify/count-from-nans.js` (Atomics not implemented), `DataView/prototype/setBigInt64` and `setFloat16` (TypedArray/DataView gaps), `Array.prototype.concat_small-typed-array.js` (Symbol.isConcatSpreadable + TypedArray). All were silently passing in Phase 27 because array method callbacks that threw had their exceptions swallowed.
> 7. *Targeted snapshot results (built-ins/Array/prototype + built-ins/Object, 6221 tests)* — 2183/6221 (35.1%) vs Phase 27 baseline 2131/6221 (34.3%), net +52 improvement. Snapshot: `snapshot-built-ins-Array-prototype_built-ins-Object-1775973470405.json`. Partial-run measurements from first 5781/46963 tests: +37 in built-ins/Array, +10 in built-ins/Function, −7 false-positive corrections (net +40). Full-suite snapshot pending.
> 8. *Commits* — Phase 28: `51d1287`. Phase 28b: `7fcb24d`.
>
> **Phase 25: NaN equality, accessor property getter/setter, String.concat toString (2026-04-11):**
> 1. *NaN equality fix in `jsAbstractEquals`* — The Abstract Equality Comparison (§7.2.13) was using `x->compare(ctx, y) == 0` for numeric comparisons, which returned 0 (equal) when both sides were NaN because `compare` delegates to the underlying double comparison where `NaN == NaN` is implementation-defined (IEEE 754: false, but protoCore returns 0). Added explicit `std::isnan` checks: if either operand is a NaN double/float, return `false` immediately per spec. Fixes `NaN == NaN → false`, `NaN != NaN → true`, `NaN == 1 → false`, and compound-assignment tests like `x == true` for NaN values.
> 2. *Accessor property getter support via `Object.defineProperty`* — `objectDefineProperty` in `ObjectPrototype.cpp` previously ignored `get` and `set` fields in the descriptor object, only writing the `value`. Now extracts getter and setter functions and stores them under sidecar keys: `__get_<propName>__` for the getter and `__set_<propName>__` for the setter. For accessor descriptors (those with a `get` or `set` field), the `value` field is not stored. Two lambda helpers added to `runBytecode`: `invokeGetterIfPresent` and `invokeSetterIfPresent`. `invokeGetterIfPresent` is called as a fallback in `OP_get_field`, `OP_get_field2`, `OP_get_array_el`, `OP_get_array_el2`, `OP_get_array_el3`, `OP_for_of_next`, and `OP_iterator_get_value_done` when `getAttribute` returns `PROTO_NONE`. This enables `Object.defineProperty(obj, 'x', { get() { return 42; } })` to correctly return 42 when `obj.x` is read. `invokeSetterIfPresent` is applied on property writes in strict mode (throws TypeError for getter-only accessors).
> 3. *`String.prototype.concat` / template literal `toString`* — `objToStr` in `StringPrototype.cpp` previously returned `""` for non-primitive objects (the `isBoolean` check was the last guard, no object branch). Template literals like `` `${obj}` `` compile to `"".concat(obj)`, which calls `objToStr`, which returned `""` instead of `"[object Object]"` or the object's custom `toString()`. Fixed by adding an object branch that: (a) looks up `toString` on the prototype chain via `getAttribute(ctx, tsKey, true)`; (b) if found as a native method (`isMethod`), calls it via `asMethod(ctx)`; (c) if the result is a string, uses it; (d) otherwise falls back to `"[object Object]"`. Enables `` `${{}}` → "[object Object]"` `` and `` `${new Date()}` → date string` `` patterns.
> 4. *Net gain* — +49 expressions, +32 statements, +105 overall. Snapshot: `tests/test262/reports/snapshot-language_built-ins-1775947922622.json`.
>
> **Phase 24: Strict mode directive placement + TDZ check in OP_get_var_ref0/1/2/3 (2026-04-11):**
> 1. *`"use strict"` placement in test runner* — When a test has the `onlyStrict` flag, the runner previously appended `"use strict";` after all harness scripts (200+ lines of code). JavaScript only recognises a `"use strict"` directive if it is the first statement in the enclosing script or function body; placed after harness code it is inert. Fixed by using `parts.unshift('"use strict";')` instead of `parts.push(...)`, so the directive appears as the very first token of the combined file. Fixes 18 strict-mode expression tests (compound-assignment non-writable puts, assignment non-writable property, logical-assignment non-writable, tagged-template strict context). The same fix applies to `language/statements` tests but produced no net gain there, indicating those tests fail for other reasons on top of strict mode.
> 2. *TDZ sentinel check added to `OP_get_var_ref0/1/2/3`* — The fast-path variants that read closure variable slots (index 0–3) now compare the slot value against `tdzSentinel` before pushing and throw `ReferenceError: Cannot access before initialization` if the slot is still in the TDZ state. This mirrors the existing behaviour in `OP_get_var_ref_check`. In practice, closure var slots are not pre-initialized to `tdzSentinel` in the current bootstrap path, so this fix is a correctness improvement for future work but did not change pass counts in this run.
> 3. *Net gain* — +18 expressions (strict-mode assignment/compound-assignment fixes), +0 statements. Combined: 16480/20373 (80.9%), +18 vs Phase 23.
>
> **Phase 23: OP_set_name/OP_set_name_computed fn.name descriptor + OP_put_array_el writable check (2026-04-11):**
> 1. *`OP_set_name` / `OP_set_name_computed` now set `__pd_name__=0x2` after writing the name* — QuickJS emits these opcodes (via `SetFunctionName`) when an anonymous function is assigned to a named variable (e.g. `arrow = () => {}` or destructuring patterns). Previously, the name attribute was updated but the descriptor sidecar was omitted, so `Object.getOwnPropertyDescriptor(fn, 'name').writable` returned `true` even after the name was set by OP_set_name. Fixed by calling `setNWCDescriptor(pContext, newFunc, "name")` immediately after `setAttribute` in both opcodes, and updating the mapping before pushing the result back to the stack.
> 2. *`OP_put_array_el` now enforces non-writable descriptors* — Computed-key property writes (`obj[key] = val`) bypassed the `__pd_<key>__` writable flag check that was already present in `OP_put_field` (literal-key writes). The test262 harness's `isWritable()` helper checks writability by doing `obj[name] = "unlikelyValue"` (a computed-key write), so it returned `true` for properties with `{writable:false}` descriptors. Added the same frozen-flag and `__pd__` writable-bit check to `OP_put_array_el`, with silent-fail in sloppy mode and TypeError in strict mode, matching JS spec semantics.
> 3. *Root cause of the test failure* — `verifyProperty(fn, 'name', {writable: false, ...})` calls `isWritable(fn, 'name')` which does `fn['name'] = "unlikelyValue"` (a `OP_put_array_el` operation). Before this fix, that write succeeded even though `Object.getOwnPropertyDescriptor` correctly reported `writable: false`, because the descriptor check was only in `OP_put_field`. The test then concluded the property was writable and failed with "obj['name'] descriptor should not be writable".
> 4. *Impact* — 45 combined tests fixed (+17 expressions, +28 statements). Primarily `dstr/assignment`, `dstr/function`, `dstr/for-of`, `dstr/generators`, `dstr/const`, `dstr/let`, `dstr/try`, `dstr/variable` patterns for fn-name assignment in destructuring contexts with arrow, regular, generator, and cover function variants.
>
> **Phase 22: Function.prototype → Object.prototype chain, property descriptor attributes (2026-04-11):**
> 1. *`Function.prototype` now inherits from `Object.prototype`* — `FunctionPrototype.cpp` previously created `fp = ctx->newObject(false)` (no parent). Changed to `fp = ctx->space->objectPrototype->newChild(ctx, false)` so the prototype chain is: `fn instance → Function.prototype → Object.prototype`. This gives all function instances access to `hasOwnProperty`, `toString` (Object's), `valueOf`, etc. Fixes 60+ `forbidden-ext` tests that called `fn.hasOwnProperty("caller")` / `fn.hasOwnProperty("arguments")`.
> 2. *`fn.name`, `fn.length`, `fn.prototype` non-enumerable descriptors* — `OP_fclosure8` and `OP_fclosure` now store `__pd_name__=0x2`, `__pd_length__=0x2` (configurable only: not writable, not enumerable) and `__pd_prototype__=0x1` (writable only: not configurable, not enumerable) immediately after setting those attributes on the new function instance. Matches spec: `name` and `length` must be `{writable:false, enumerable:false, configurable:true}`; `prototype` must be `{writable:true, enumerable:false, configurable:false}`. Fixes `fn.name`-related `verifyProperty` checks and ensures `for (var k in fn)` iterates nothing.
> 3. *Enumerable filtering in `OP_for_in_start`* — The key-collection loop now checks `__pd_<key>__` for each property; if bit 2 (0x4) is absent, the key is skipped. Ensures `for...in` on functions (or any object with non-enumerable properties) correctly omits them.
> 4. *Enumerable filtering in `collectOwnKeys` (ObjectPrototype.cpp)* — The helper used by `Object.keys`, `Object.values`, and `Object.entries` now applies the same `__pd__` check, so `Object.keys(fn)` returns `[]` when all own props are non-enumerable.
> 5. *Enumerable + internal-key filtering in `OP_copy_data_properties`* — Object spread `{...obj}` previously copied all own properties including internal `__*__` bookkeeping keys. Now skips both internal-pattern keys and non-enumerable properties (checked via `__pd__`). This is spec-correct: spread and `Object.assign` copy only own enumerable string-keyed properties.
>
> **Phase 21: TDZ sentinel fix, OP_append (array spread), instanceof TypeError (2026-04-11):**
> 1. *TDZ sentinel correctness fix* — The previous sentinel was created via `fromUTF8String("\x00__protojs_tdz_sentinel__")`. C strings terminate at the first null byte, so the sentinel was indistinguishable from the empty string `""`. Any binding whose initial value was `''` (e.g. `f([null, 0, false, ''])`) matched the sentinel check and spuriously threw `ReferenceError: Cannot access before initialization`. Fixed using the same `newObject(false)` pattern as `t_nullSentinel`: a unique `ProtoObject*` stored at `__js_tdz_sentinel__` in the global root, impossible to collide with any JS value including empty string. This was a `thread_local` stored in both the declaration and the per-invocation initialization path.
> 2. *`OP_append` (opcode 0x4F) implemented* — `DEF(append, 1, 3, 2, none)` is emitted for array spread: `[a, ...x, b]`. Stack contract: `[array, index, iterable]` → `[array, new_index]`. Implementation handles two paths: (a) array-like operands (have a numeric `length`) copy elements by index; (b) general iterables call `Symbol.iterator()` then loop `next()` with exception propagation via `t_hasCallException`/`t_callException`. The `updateMapping` helper keeps the immutable array slot current after each `setAttribute`. Enables `[...arr]`, `[...str]`, rest-in-spread patterns, and `Array.from`-equivalent spreads.
> 3. *`instanceof` TypeError per §13.10.2* — When the right-hand side of `instanceof` is a primitive (boolean, integer, double, string that is not a method), the spec requires `TypeError: Right-hand side of 'instanceof' is not callable`. Previously the check was absent; `true instanceof true` silently returned `false`. Now throws TypeError correctly. When `F.prototype` is a primitive (excluding `PROTO_NONE`), throws `TypeError: Function has non-object prototype in instanceof check` per §13.10.2 step 7.
> 4. *False-positive exposure (4 tests: S15.3.5.3_A1_T1–T4)* — These tests verify `instanceof` throws TypeError for non-callable RHS. They use `FACTORY = Function("name","this.name=name;")`, which returns `undefined` since the `Function()` constructor is not implemented. Previously `undefined instanceof undefined` silently returned `false` (no error check). Now it correctly throws TypeError. The tests were already "expecting TypeError to be thrown" — but they were previously passing because the implicit vacuous return path was reached instead. In Phase 20 snapshots all 4 show `passed` confirming they were false positives. Net: 4 false-positive exposures, but +25 genuine new passes (21 net + 4 exposed).
>
> **Phase 20: object spread/rest, for-in, delete, Object.keys/values/entries/assign (2026-04-11):**
> 1. *`OP_copy_data_properties` implemented* — Handles the 3-field mask (bits 0–1 = targetDepth, bits 2–4 = sourceDepth, bits 5–7 = exclDepth; 0 = no exclusion list). Iterates source's own attributes via `getOwnAttributes()`/`ProtoSparseListIterator`, skipping keys present in the exclusion object. Uses a save/pop/push pattern to update the immutable target slot in-place. Enables `{ ...obj }` spreads and `const { a, ...rest }` rest patterns.
> 2. *`OP_for_in_start` / `OP_for_in_next` implemented* — `OP_for_in_start` collects all own non-internal, non-array-index-length keys from the target object into a `__iter_arr__` list stored inside an iterator ProtoObject (with `__iter_idx__ = 0`); `OP_for_in_next` advances the index and pushes `(updatedIter, key, done)`. Previously both opcodes had stubs that caused any function containing `for...in` (including the `verifyProperty` harness helper) to terminate early. This restores all `verifyProperty` calls that use `enumerable: true`.
> 3. *`OP_delete` truly removes properties* — Previously `setAttribute(key, PROTO_NONE)` stored a PROTO_NONE entry in the sparse list BST, leaving the key visible to `hasOwnAttribute` and `getOwnAttributes` iteration. Discovered that `setAttribute(key, nullptr)` calls `implRemoveAt` in protoCore, physically removing the BST node. Fixed by passing `nullptr` instead of `PROTO_NONE`. `hasOwnProperty` and `Object.keys` now correctly report deleted properties as absent.
> 4. *`Object.keys` / `Object.values` / `Object.entries` / `Object.assign` implemented* — All four were empty stubs returning `[]` or `undefined`. Now use `getOwnAttributes()` + `ProtoSparseListIterator` via the shared `collectOwnKeys` helper. The helper filters internal `__name__` keys, array `length` properties, and truly-deleted (null-valued) entries. `Object.assign` copies own non-internal properties from each source to the target.
> 5. *False-positive exposure (−64 tests vs Phase 19)* — When `OP_copy_data_properties` was unimplemented it returned `PROTO_NONE` from `runBytecode` immediately (exit code 0), causing test assertions to never run. 64 tests silently "passed" in Phase 19 via this vacuous exit. With Phase 20 they execute fully; many fail on genuinely unimplemented features (async/generator patterns). Net apparent: −48 combined, but correctness strictly improved.
>
> **Phase 19: `space->methodPrototype = Function.prototype` (2026-04-11):**
> 1. *Root cause* — `ProtoObject::getPrototype()` for `POINTER_TAG_METHOD` objects returns `context->space->methodPrototype`. This was `nullptr` (never set), so any property lookup on a native function object (e.g. `Array.prototype.join`, `Function.prototype.call`) returned `PROTO_NONE` immediately. This meant `fn.bind`, `fn.call`, `fn.apply`, and `fn.toString` were all `undefined` on any native function.
> 2. *Fix* — In `ensureFunctionPrototype` (called during bootstrap), after building the complete `fp` object with all four methods, set `ctx->space->methodPrototype = fp`. From that point forward, every `ProtoMethodCell` (`POINTER_TAG_METHOD`) object inherits from Function.prototype via `getPrototype()` → `methodPrototype` → attribute walk.
> 3. *Primary beneficiary — `propertyHelper.js`* — The test262 harness does `var __join = Function.prototype.call.bind(Array.prototype.join)` at module load time. Previously `.bind` on `Function.prototype.call` returned `undefined`, making all `verifyProperty()` helpers silently null, and causing TypeError in Phase 17. Now `Function.prototype.call.bind(...)` returns a proper bound function, `verifyProperty()` runs its checks, and class/elements tests that have correct property semantics pass.
> 4. *+1562 tests recovered* — 782 expressions (mostly class/elements + arrow-function tests) + 780 statements (mostly class/elements). The previous Phase 17 "false positives" (which threw TypeError when verifyProperty() tried to call a null helper) are now genuine passes where the property checks succeed.
>
> **Phase 18: OP_in stack order + OP_put_field net effect (2026-04-11):**
> 1. *`OP_in` always returned `true`* — Two compounding bugs: (a) QuickJS pushes `key` first then `obj`, so stack top is `obj` and second is `key` — our implementation had these swapped; (b) `obj->hasAttribute(pContext, key)` returns `PROTO_TRUE` or `PROTO_FALSE` (both are non-null `ProtoObject*`), so `bool has = (bool)obj->hasAttribute(...)` was always `true` even when `hasAttribute` returned `PROTO_FALSE`. Fixed by reading `obj` from `stackTop` (not second), and comparing the result with `== PROTO_TRUE`. Also added TypeError for non-object operands per spec.
> 2. *`OP_put_field` stack net effect was −1 instead of −2* — QuickJS specifies `DEF(put_field, 5, 2, 0, atom)` (n_pop=2, n_push=0). Our implementation popped `val` (top) and `obj` (second) correctly but then pushed `obj` or the updated object back, leaving net −1. This caused `f([,])` (function with an elision hole parameter) to never execute the function body: QuickJS compiles `[,]` as `OP_array_from 0` + `OP_dup` + `OP_push_i32 1` + `OP_put_field "length"`, then the peephole optimizer elides `insert2 + put_field + drop → put_field`. Our +1 push made `OP_call` read the array from the wrong stack position (one slot off), silently no-oping the call. Fixed by removing all `stackPush` calls from `OP_put_field`.
>
> **Phase 16: destructuring error handling (2026-04-11):**
> 1. *TypeError for null in `OP_for_of_start`* — When the destructuring target is `t_nullSentinel` (JS `null`), the opcode now sets `pending_exception` to `TypeError: null is not iterable` and `break`s (not `return PROTO_NONE`) so the exception is catchable by JS `try/catch` via the dispatch loop at line ~4712.
> 2. *Exception propagation from JS callbacks via thread-locals* — `callJSFunction` previously silently suppressed `childEx`. It now stores exceptions in `t_callException`/`t_hasCallException` thread-locals. All four iterator call sites (`OP_for_of_start` Case C, `OP_for_of_next`, `OP_iterator_next`, `OP_iterator_call` drain loop) check these thread-locals immediately after return and convert them to `pending_exception`.
> 3. *`OP_iterator_close` calls `iterator.return()` per spec* — When a `for-of` loop exits early (via `break`, `throw`, or `return`), the spec requires calling `return()` on the iterator to allow cleanup. The close opcode now reads the iterator from its slot, checks the iterator's `return` property, and calls it (native or JS). A done-tracking flag in slot `bs+2` (initialized at `OP_for_of_start`, set to 1 when `done: true` is received) prevents calling `return()` on already-exhausted iterators, fixing 13 `iter-no-close` regressions from Phase 16 v1.
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
| 2026-04-10 | *(per-category only; full-suite run pending)* | — | Phase 13: +268 expressions; Phase 14: +17 expressions, +34 statements vs Phase 13 honest. Full-suite run needed to capture Phases 8–16 gains. |
| 2026-04-11 | *(per-category only)* | 17640 / 20373 (86.6%) | Phase 16: +45 expressions, +72 statements vs Phase 15 (language/expressions + language/statements only). |
| 2026-04-11 | *(per-category only)* | 14768 / 20373 (72.5%) | Phase 17: −2872 net (618 genuine improvements, 3490 false-positives unmasked). Stricter TypeError checking exposed tests that were silently passing due to absent error propagation. |
| 2026-04-11 | *(per-category only)* | 14802 / 20373 (72.7%) | Phase 18: +34 net (+28 expressions, +6 statements). Fixed `OP_in` always-true and `OP_put_field` stack over-push. |
| 2026-04-11 | *(per-category only)* | 16364 / 20373 (80.3%) | Phase 19: +1562 net (+782 expressions, +780 statements). Set `space->methodPrototype = Function.prototype` so all native functions inherit call/bind/apply. |
| 2026-04-11 | *(per-category only)* | 16316 / 20373 (80.1%) | Phase 20: −48 apparent net (−17 expressions, −31 statements). 64 false-positives exposed by implementing `OP_copy_data_properties`; +80 genuine (spread/rest, for-in, delete, Object.keys/values/entries). |
| 2026-04-11 | *(per-category only)* | 16358 / 20373 (80.3%) | Phase 21: +42 net (+21 expressions, +21 statements). TDZ sentinel correctness fix, `OP_append` (array spread `[...x]`) implemented, `instanceof` TypeError per §13.10.2. 4 false-positives exposed (S15.3.5.3_A1_T1–T4). |
| 2026-04-11 | *(per-category only)* | 16417 / 20373 (80.6%) | Phase 22: +59 net (+30 expressions, +29 statements). `Function.prototype → Object.prototype` chain; `fn.name`/`length`/`prototype` non-enumerable descriptors; enumerable filtering in `for...in`, `Object.keys/values/entries`, and `{...spread}`. |
| 2026-04-11 | `snapshot-language_built-ins-1775962316298.json` | 27404 / 46963 (58.4%) | Phase 26 full-suite: Phases 8–26 captured. First honest full-suite run on protoCore path. |
| 2026-04-12 | `snapshot-language_built-ins-1775967903551.json` | **28241 / 46963 (60.1%)** | Phase 27: Synchronous Promise + async/await opcodes. +837 vs Phase 26. Promise: 461/656 (70.3%), async/await: 1305/2147 (60.8%). |

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
