# Phase 16: Destructuring Error Handling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix ~150–200 failing test262 destructuring tests by implementing proper error propagation for iterators: TypeError for null/undefined iterables, exception propagation from JS callbacks via thread-local, and `iterator.return()` on `OP_iterator_close`.

**Architecture:** Four targeted fixes to `ProtoInterpreter.cpp` — (1) add `t_callException`/`t_hasCallException` thread-locals; (2) set them in `callJSFunction` instead of suppressing exceptions; (3) check them at iterator-related `callJSFunction` call sites; (4) throw TypeError for null in `OP_for_of_start`; (5) call `iterator.return()` in `OP_iterator_close`.

**Tech Stack:** C++20, protoCore, QuickJS bytecode interpreter

---

## Context: Why These Fixes

After Phase 15 (OP_iterator_next + OP_iterator_call), test262 destructuring tests still fail because:
- `null is not iterable` — `OP_for_of_start` returns `PROTO_NONE` (vacuous pass) for null instead of throwing TypeError
- JS iterator callbacks that throw (e.g., `Symbol.iterator` returning a non-object) are silently swallowed by `callJSFunction`
- `OP_iterator_close` pops the stack without calling `iterator.return()`, causing abrupt-completion tests to fail

## File Map

| File | Changes |
|------|---------|
| `src/runtime/ProtoInterpreter.cpp` | All implementation changes (lines ~62, ~4083, ~4123, ~4200, ~4272, ~4317, ~4420, ~4906) |

---

## Task 1: Thread-local exception propagation channel

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` (~line 62)

**Purpose:** Add `t_callException`/`t_hasCallException` thread-locals to allow `callJSFunction` to signal exception state back to `runBytecode` call sites.

- [x] **Step 1: Add thread-locals after `t_genIterator` declaration**

```cpp
// ---------------------------------------------------------------------------
// Iterator callback exception propagation.
// callJSFunction() cannot set pending_exception (it has no access to the
// local variables inside runBytecode).  Instead it stores the exception here
// and iterator-related call sites check this flag immediately after return.
// ---------------------------------------------------------------------------
thread_local const proto::ProtoObject*              t_callException     = nullptr;
thread_local bool                                   t_hasCallException  = false;
```

- [x] **Step 2: Fix `callJSFunction` to set t_callException (line ~4906)**

Replace:
```cpp
childCtx.returnValue = result;
// Exceptions from callbacks are silently suppressed at this level.
return result ? result : PROTO_NONE;
```

With:
```cpp
childCtx.returnValue = result;
// Propagate exceptions from JS callbacks via thread-local so that
// iterator-related call sites inside runBytecode can set pending_exception.
if (childEx && childEx != PROTO_NONE) {
    t_callException    = childEx;
    t_hasCallException = true;
    return PROTO_NONE;
}
return result ? result : PROTO_NONE;
```

- [x] **Step 3: Build and verify no compile errors**

```bash
cmake --build build --target protojs 2>&1 | tail -5
```
Expected: `Built target protojs`

---

## Task 2: TypeError for null in OP_for_of_start

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` (OP_for_of_start, ~line 4083)

**Purpose:** Throw TypeError when destructuring target is null. `t_nullSentinel` represents JS null unambiguously (PROTO_NONE is used for undefined and as a generator sentinel).

- [x] **Step 1: Add null guard before PROTO_NONE guard**

After `stackPop(pContext)` at the start of `OP_for_of_start`, add:
```cpp
// Null is not iterable — throw TypeError.
if (iterable == t_nullSentinel) {
    pending_exception = makeError(pContext, "TypeError",
        "null is not iterable", pGlobalRoot);
    has_pending_exception = true;
    break;
}
```

Note: Must use `break` (not `return PROTO_NONE`) so the exception goes through the `has_pending_exception` dispatch at line 4712, making it catchable by `try/catch`.

- [x] **Step 2: Build and run manual test**

```bash
cmake --build build --target protojs 2>&1 | tail -3
./build/protojs tests/manual/test_destructuring.js 2>&1 | grep "PASS\|FAIL\|Error"
```
Expected: All PASS lines, no errors.

---

## Task 3: Check t_hasCallException at iterator call sites

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` (4 call sites)

**Purpose:** After each `callJSFunction` inside iterator opcodes, check if an exception was thrown and convert it to `pending_exception` so JS try/catch can intercept it.

**Pattern for each call site:**
```cpp
result = callJSFunction(pContext, fn, thisVal, args);
if (t_hasCallException) {
    pending_exception  = t_callException;
    has_pending_exception = true;
    t_hasCallException = false;
    t_callException    = nullptr;
    break; // go through exception dispatch
}
```

- [x] **Step 1: OP_for_of_start Case C — Symbol.iterator call (~line 4123)**
- [x] **Step 2: OP_for_of_next — JS-defined next() call (~line 4200)**
- [x] **Step 3: OP_iterator_next — JS-defined next() call (~line 4317)**

For OP_iterator_next the break is preceded by re-pushing the 3 iterator items so OP_iterator_close can clean them up on the exception path:
```cpp
stackPush(pContext, iterObjIN);
stackPush(pContext, nextMethodIN);
stackPush(pContext, catchOffIN ? catchOffIN : pContext->fromInteger(0LL));
break;
```

- [x] **Step 4: OP_iterator_call drain loop — JS-defined next() (~line 4420)**

Uses a `bool icCallException = false` flag because the check is inside a `while` loop; after the loop exits, `if (icCallException) break;` before the push-back.

- [x] **Step 5: Build and run manual test**

```bash
cmake --build build --target protojs 2>&1 | tail -3
./build/protojs tests/manual/test_destructuring.js 2>&1 | grep "ALL TESTS PASSED"
```

---

## Task 4: Fix OP_iterator_close to call iterator.return()

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` (OP_iterator_close, ~line 4272)

**Purpose:** When a loop is exited early (break, throw, return), the spec requires calling `iterator.return()` so the iterator can clean up. Without this, tests that verify the close protocol fail.

- [x] **Step 1: Replace simple 3-pop with return() call**

```cpp
case OP_iterator_close: {
    // Pop catch_offset and nextMethod; keep iterObj to inspect.
    if (!stackEmpty(pContext)) stackPop(pContext); // catch_offset
    if (!stackEmpty(pContext)) stackPop(pContext); // nextMethod
    const proto::ProtoObject* iterObjCL = PROTO_NONE;
    if (!stackEmpty(pContext)) {
        iterObjCL = stackTop(pContext);
        stackPop(pContext); // iterObj wrapper
    }
    // If this was a native iterator (sentinel -1), call .return() for cleanup.
    if (iterObjCL && iterObjCL != PROTO_NONE) {
        const proto::ProtoString* slotKeyCL = JSSymbols::iterSlot(pContext);
        const proto::ProtoObject* slotValCL =
            slotKeyCL ? iterObjCL->getAttribute(pContext, slotKeyCL, false) : PROTO_NONE;
        if (slotValCL && slotValCL != PROTO_NONE && slotValCL->isInteger(pContext)) {
            uint32_t bsCL = static_cast<uint32_t>(slotValCL->asLong(pContext));
            const proto::ProtoObject* actualIterCL = getSlot(pContext, bsCL);
            const proto::ProtoObject* idxObjCL     = getSlot(pContext, bsCL + 1);
            long long idxCL = (idxObjCL && idxObjCL->isInteger(pContext))
                              ? idxObjCL->asLong(pContext) : 0LL;
            if (idxCL == -1LL && actualIterCL && actualIterCL != PROTO_NONE) {
                const proto::ProtoObject* retKeyObj =
                    pContext->fromUTF8String("return");
                const proto::ProtoString* retKey =
                    retKeyObj ? retKeyObj->asString(pContext) : nullptr;
                const proto::ProtoObject* retFn = retKey
                    ? actualIterCL->getAttribute(pContext, retKey, false) : PROTO_NONE;
                if (retFn && retFn != PROTO_NONE) {
                    if (retFn->isMethod(pContext)) {
                        proto::ProtoMethod nativeRet = retFn->asMethod(pContext);
                        if (nativeRet)
                            nativeRet(pContext, actualIterCL, nullptr, nullptr, nullptr);
                    } else {
                        const proto::ProtoList* emptyRetArgs = pContext->newList();
                        callJSFunction(pContext, retFn, actualIterCL, emptyRetArgs);
                        // Clear any exception from return() — close is best-effort.
                        t_hasCallException = false;
                        t_callException    = nullptr;
                    }
                }
            }
        }
    }
    break;
}
```

- [x] **Step 2: Build and run manual test**

```bash
cmake --build build --target protojs 2>&1 | tail -3
./build/protojs tests/manual/test_destructuring.js 2>&1 | grep "ALL TESTS PASSED"
```

---

## Task 5: Run test262 snapshots and update STATUS.md

- [x] **Step 1: Run expressions + statements in parallel**

```bash
TEST262_PATTERNS="language/expressions" TEST262_ROOT=../test262 \
  node tests/test262/runner/test262_runner.js &
TEST262_PATTERNS="language/statements" TEST262_ROOT=../test262 \
  node tests/test262/runner/test262_runner.js &
wait
```

- [x] **Step 2: Read new snapshot files and extract pass counts**

```bash
ls -lt tests/test262/reports/ | head -5
node -e "const r = require('./tests/test262/reports/<SNAPSHOT>.json'); console.log(r.summary)"
```

Results — Phase 16 v2:
- expressions: 9401 / 11036 (+45 vs Phase 15 baseline of 9356)
- statements:  8239 / 9337  (+72 vs Phase 15 baseline of 8167)
- combined: 17640 / 20373 (86.6%)

- [x] **Step 3: Update STATUS.md** with Phase 16 row in both tables

- [x] **Step 4: Commit all changes**

```bash
git add src/runtime/ProtoInterpreter.cpp tests/test262/STATUS.md docs/superpowers/plans/2026-04-11-phase16-dstr-errors.md
git commit -m "feat(phase16): fix destructuring error handling — TypeError for null, exception propagation from callbacks, iterator.return() on close"
```
