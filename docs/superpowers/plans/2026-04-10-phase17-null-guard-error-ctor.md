# Phase 17: Null/Undefined Guard + Error Constructor Identity

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix ~300–400 failing test262 tests by (1) adding TypeError for null/undefined property access (OP_get_field, OP_to_object), (2) TypeError for calling non-functions (OP_call_method), and (3) wiring `constructor` on error prototypes so `thrown.constructor === TypeError` identity checks pass.

**Architecture:** Four targeted fixes to `ProtoInterpreter.cpp` — (1) `ensureBuiltinErrorConstructors` adds `constructor` on each error prototype; (2) `OP_to_object` throws TypeError for null/undefined (object destructuring); (3) `OP_get_field` / `OP_get_field2` throw TypeError for null/undefined receiver; (4) `OP_call_method` throws TypeError when func is PROTO_NONE.

**Tech Stack:** C++20, protoCore, QuickJS bytecode interpreter

---

## Context: Why These Fixes

After Phase 16 (destructuring error handling), test262 still shows ~2370 semantic failures because:
- `thrown.constructor` is `undefined` on all errors we create — `TypeError.prototype.constructor` is never set, so `thrown.constructor !== TypeError` always, breaking all `assert.throws(TypeError, ...)` checks
- `null.x` and `undefined.x` silently return `undefined` instead of throwing TypeError — `OP_get_field` / `OP_get_field2` do not guard against null/undefined receiver
- `const {} = null` silently succeeds — `OP_to_object` does nothing (passes null through)
- `x.foo()` where `foo` is undefined silently pushes PROTO_NONE — `OP_call_method` has no "not a function" guard

## Root Cause Analysis

Running `e.constructor === TypeError` after any `makeError(...)` call returns `false` because:
- `ensureBuiltinErrorConstructors` sets up `TypeError.prototype` with `name` and `toString`
- But never sets `TypeError.prototype.constructor = TypeError`
- Error instances inherit from `TypeError.prototype`, so `e.constructor` is `undefined`

For `null.x`:
- `OP_get_field` checks `obj ? obj->getAttribute(...) : PROTO_NONE`
- `t_nullSentinel` is a valid C++ pointer (truthy), so the check passes
- `t_nullSentinel->getAttribute(...)` returns PROTO_NONE → silently returns undefined

## File Map

| File | Changes |
|------|---------|
| `src/runtime/ProtoInterpreter.cpp` | All implementation changes (~line 695, ~2106, ~2132, ~2465, ~2484, ~2499, ~3072, ~3328) |

---

## Task 1: Error prototype.constructor identity

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` (~line 695, inside `ensureBuiltinErrorConstructors`)

**Purpose:** Set `TypeError.prototype.constructor = TypeError` (and same for all error classes) so that `thrown.constructor === TypeError` passes in `assert.throws` checks.

**Estimated impact:** ~300 tests — every test that throws an error and checks `thrown.constructor`

- [ ] **Step 1: Add `constructor` property after building proto and ctor**

Find the block in `ensureBuiltinErrorConstructors` that ends with:
```cpp
        ctor = ctor->setAttribute(ctx, protoKey, proto);
        if (!ctor) continue;
```

Replace it with:
```cpp
        ctor = ctor->setAttribute(ctx, protoKey, proto);
        if (!ctor) continue;
        // Set prototype.constructor = ctor so `e.constructor === TypeError` passes.
        const proto::ProtoString* ctorPropKey =
            ctx->fromUTF8String("constructor")
            ? ctx->fromUTF8String("constructor")->asString(ctx) : nullptr;
        if (ctorPropKey) {
            proto = proto->setAttribute(ctx, ctorPropKey, ctor);
            if (!proto) continue;
            // Re-link: ctor.prototype must point to the updated proto.
            ctor = ctor->setAttribute(ctx, protoKey, proto);
            if (!ctor) continue;
        }
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoJS/build --target protojs 2>&1 | tail -3
```
Expected: `Built target protojs`

- [ ] **Step 3: Verify fix with manual test**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
./build/protojs --minimal -e "
try { undeclaredVar; } catch(e) {
  console.log('ReferenceError.ctor===ReferenceError:', e.constructor === ReferenceError);
}
try { null[0]; } catch(e) { /* will fix next */ }
const te = new TypeError('test');
console.log('TypeError.ctor===TypeError:', te.constructor === TypeError);
console.log('TypeError.proto.ctor===TypeError:', TypeError.prototype.constructor === TypeError);
" 2>&1 | grep -v "^\[protojs\]\|^\[RegExp\]"
```
Expected:
```
ReferenceError.ctor===ReferenceError: true
TypeError.ctor===TypeError: true
TypeError.proto.ctor===TypeError: true
```

---

## Task 2: TypeError in OP_to_object for null/undefined

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` (`OP_to_object`, ~line 3072)

**Purpose:** `const {} = null` and `const {} = undefined` must throw TypeError. QuickJS emits `OP_to_object` before any object destructuring pattern — currently it is a no-op passthrough.

**Estimated impact:** ~100 tests — all `obj-init-null`, `obj-init-undefined` patterns across const/let/var/for/for-of/try contexts.

- [ ] **Step 1: Replace no-op OP_to_object with null guard**

Find:
```cpp
            case OP_to_object: {
                // ToObject: for primitive types, returns a wrapper object.
                // For objects, returns the value unchanged.
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext); stackPop(pContext);
                // In our system, primitives remain as-is (we don't have wrapper objects yet).
                stackPush(pContext, val ? val : PROTO_NONE);
                break;
            }
```

Replace with:
```cpp
            case OP_to_object: {
                // ToObject: null and undefined are not object-coercible — throw TypeError.
                // For any other value, push unchanged (primitives wrap lazily).
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext); stackPop(pContext);
                if (!val || val == PROTO_NONE || val == t_nullSentinel) {
                    const bool isNull = (val == t_nullSentinel);
                    pending_exception = makeError(pContext, "TypeError",
                        isNull ? "Cannot convert null to object"
                               : "Cannot convert undefined to object",
                        pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
                stackPush(pContext, val);
                break;
            }
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoJS/build --target protojs 2>&1 | tail -3
./build/protojs --minimal -e "
try { const {} = null; console.log('BAD'); } catch(e) { console.log('null dstr TypeError:', e.constructor === TypeError, e.message); }
try { const {} = undefined; console.log('BAD'); } catch(e) { console.log('undef dstr TypeError:', e.constructor === TypeError, e.message); }
const {x} = {x:42}; console.log('normal dstr:', x);
" 2>&1 | grep -v "^\[protojs\]\|^\[RegExp\]"
```
Expected:
```
null dstr TypeError: true Cannot convert null to object
undef dstr TypeError: true Cannot convert undefined to object
normal dstr: 42
```

---

## Task 3: TypeError in OP_get_field / OP_get_field2 for null/undefined receiver

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` (`OP_get_field` ~line 2099, `OP_get_field2` ~line 2129)

**Purpose:** `null.x` and `undefined.x` must throw TypeError. Currently `t_nullSentinel` passes the `obj ?` truthy check and returns PROTO_NONE silently. This breaks all tests that modify `Array.prototype[Symbol.iterator]` and expect subsequent calls to throw, plus any test that checks `e.constructor` (because `e.constructor` on a ProtoNone-based object traversal hits null).

**Estimated impact:** ~100 tests — `iter-get-err-array-prototype` pattern, null receiver property access

- [ ] **Step 1: Add null/undefined guard in OP_get_field (after stackPop)**

Find in `OP_get_field`:
```cpp
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (!key) { stackPush(pContext, PROTO_NONE); break; }
```

Replace with:
```cpp
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (!key) { stackPush(pContext, PROTO_NONE); break; }
                // Throw TypeError for null/undefined receiver.
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) {
                    std::string keyStr;
                    if (key) key->toUTF8String(pContext, keyStr);
                    std::string msg = "Cannot read properties of ";
                    msg += (!obj || obj == PROTO_NONE) ? "undefined" : "null";
                    msg += " (reading '"; msg += keyStr; msg += "')";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
```

- [ ] **Step 2: Add null/undefined guard in OP_get_field2 (peek, don't pop)**

Find in `OP_get_field2`:
```cpp
                const proto::ProtoObject* obj = stackTop(pContext);
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
```

Replace with:
```cpp
                const proto::ProtoObject* obj = stackTop(pContext);
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                // Throw TypeError for null/undefined receiver (OP_get_field2 keeps obj on stack).
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) {
                    stackPop(pContext); // consume obj from stack
                    std::string keyStr;
                    if (key) key->toUTF8String(pContext, keyStr);
                    std::string msg = "Cannot read properties of ";
                    msg += (!obj || obj == PROTO_NONE) ? "undefined" : "null";
                    msg += " (reading '"; msg += keyStr; msg += "')";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
```

- [ ] **Step 3: Build and verify**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoJS/build --target protojs 2>&1 | tail -3
./build/protojs --minimal -e "
try { null.x; } catch(e) { console.log('null.x:', e.constructor === TypeError, e.message); }
try { undefined.x; } catch(e) { console.log('undef.x:', e.constructor === TypeError, e.message); }
var obj = {x: 1}; console.log('obj.x:', obj.x);
" 2>&1 | grep -v "^\[protojs\]\|^\[RegExp\]"
```
Expected:
```
null.x: true Cannot read properties of null (reading 'x')
undef.x: true Cannot read properties of undefined (reading 'x')
obj.x: 1
```

---

## Task 4: TypeError in OP_get_array_el for null/undefined receiver

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` (`OP_get_array_el` ~line 2462, `OP_get_array_el2` ~line 2480, `OP_get_array_el3` ~line 2496)

**Purpose:** `null[0]` and `undefined[0]` must throw TypeError. Same root cause as OP_get_field.

**Estimated impact:** ~30 tests

- [ ] **Step 1: Add null guard in OP_get_array_el**

Find in `OP_get_array_el` after the two stackPop calls:
```cpp
                const proto::ProtoObject* val;
                uint8_t taType = getTypedArrayElementType(pContext, obj);
```

Insert before this line:
```cpp
                // Throw TypeError for null/undefined receiver.
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) {
                    std::string msg = "Cannot read properties of ";
                    msg += (!obj || obj == PROTO_NONE) ? "undefined" : "null";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
```

- [ ] **Step 2: Add null guard in OP_get_array_el2 and OP_get_array_el3**

`OP_get_array_el2` and `OP_get_array_el3` peek without popping. For these, after `const proto::ProtoObject* obj = stackAt(pContext, 1);` add:
```cpp
                if (!obj || obj == PROTO_NONE || obj == t_nullSentinel) {
                    stackPop(pContext); // pop index
                    stackPop(pContext); // pop obj
                    std::string msg = "Cannot read properties of ";
                    msg += (!obj || obj == PROTO_NONE) ? "undefined" : "null";
                    pending_exception = makeError(pContext, "TypeError", msg.c_str(), pGlobalRoot);
                    has_pending_exception = true;
                    break;
                }
```

- [ ] **Step 3: Build and verify**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoJS/build --target protojs 2>&1 | tail -3
./build/protojs --minimal -e "
try { null[0]; } catch(e) { console.log('null[0]:', e.constructor === TypeError, e.message); }
try { undefined[1]; } catch(e) { console.log('undef[1]:', e.constructor === TypeError, e.message); }
var arr = [1,2,3]; console.log('arr[1]:', arr[1]);
" 2>&1 | grep -v "^\[protojs\]\|^\[RegExp\]"
```
Expected:
```
null[0]: true Cannot read properties of null
undef[1]: true Cannot read properties of undefined
arr[1]: 2
```

---

## Task 5: TypeError in OP_call_method for non-callable

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` (`OP_call_method` ~line 3328)

**Purpose:** `x.foo()` when `foo` is undefined must throw `TypeError: x.foo is not a function`. Currently falls through to a silent PROTO_NONE push.

**Estimated impact:** ~50 tests

- [ ] **Step 1: Replace silent fallthrough with TypeError**

Find in `OP_call_method` the else branch at ~line 3328:
```cpp
                    } else {
                        for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);
                        /* Function not yet converted to ProtoMethod; push PROTO_NONE. */
                        if (opcode != OP_tail_call_method)
                            stackPush(pContext, PROTO_NONE);
                    }
```

Replace with:
```cpp
                    } else {
                        // func is neither bytecode, native, nor bound — throw TypeError.
                        for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);
                        if (!func || func == PROTO_NONE) {
                            pending_exception = makeError(pContext, "TypeError",
                                "is not a function", pGlobalRoot);
                            has_pending_exception = true;
                        } else {
                            // Non-null but unrecognized callable — best-effort PROTO_NONE.
                            if (opcode != OP_tail_call_method)
                                stackPush(pContext, PROTO_NONE);
                        }
                    }
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build /home/gamarino/Documentos/proyectos/protoJS/build --target protojs 2>&1 | tail -3
./build/protojs --minimal -e "
try { var x = {}; x.foo(); } catch(e) { console.log('not a fn:', e.constructor === TypeError, e.message); }
try { var f = function() { return 42; }; console.log('fn call:', f()); } catch(e) { console.log('BAD:', e.message); }
" 2>&1 | grep -v "^\[protojs\]\|^\[RegExp\]"
```
Expected:
```
not a fn: true is not a function
fn call: 42
```

---

## Task 6: Run test262 snapshots and update STATUS.md

- [ ] **Step 1: Run expressions + statements in parallel**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
TEST262_PATTERNS="language/expressions" TEST262_ROOT=../test262 \
  node tests/test262/runner/test262_runner.js &
TEST262_PATTERNS="language/statements" TEST262_ROOT=../test262 \
  node tests/test262/runner/test262_runner.js &
wait
```

- [ ] **Step 2: Read new snapshot files and extract pass counts**

```bash
ls -lt tests/test262/reports/ | head -4
node -e "
const e = require('./tests/test262/reports/<EXPR_SNAPSHOT>.json');
const s = require('./tests/test262/reports/<STMT_SNAPSHOT>.json');
console.log('expressions:', e.summary);
console.log('statements:', s.summary);
console.log('delta expressions:', e.summary.passed - 9401);
console.log('delta statements:', s.summary.passed - 8239);
"
```

- [ ] **Step 3: Update STATUS.md** with Phase 17 row in both tables and add Phase 17 notes

- [ ] **Step 4: Commit all changes**

```bash
git add src/runtime/ProtoInterpreter.cpp tests/test262/STATUS.md docs/superpowers/plans/2026-04-10-phase17-null-guard-error-ctor.md
git commit -m "feat(phase17): TypeError for null/undefined access + error constructor identity"
```
