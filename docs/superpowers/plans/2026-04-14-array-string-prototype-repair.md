# Array + String Prototype Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recover ~870–1160 test262 tests by fixing seven root-cause bugs in `ArrayPrototype.cpp`, `StringPrototype.cpp`, and `ProtoInterpreter.cpp`.

**Architecture:** Seven independent fixes across three files. Fix A (arrLen/arrGet prototype chain) is the single highest-yield change — two characters (`false→true`) that affect every array method at once. Fix E (String wrapper method dispatch) requires an upfront diagnostic step before implementing. All other fixes are surgical one-location changes.

**Tech Stack:** C++20, protoCore immutable objects (`setAttribute` returns new pointer), `JSSymbols` for interned string keys, `__pd_<name>__` sidecar descriptor system (bit0=writable 0x1, bit1=configurable 0x2, bit2=enumerable 0x4). Build: `cmake --build build --target protojs -j$(nproc)` from `/home/gamarino/Documentos/proyectos/protoJS`.

---

## File Map

| File | Lines | Changes |
|------|-------|---------|
| `src/ArrayPrototype.cpp` | 38, 70, 85–91, top of methods | Fix A (arrLen/arrGet true), Fix B (arrSet isArray guard), Fix D (ToObject coercion) |
| `src/runtime/ProtoInterpreter.cpp` | 1487–1498 | Fix C (arguments toStringTag) |
| `src/StringPrototype.cpp` | 929, 1074–1081 | Fix E (String wrapper), Fix F (split separator), Fix G (method .length) |
| `docs/TEST262_STATUS.md` | top | Phase 39 snapshot |

---

## Task 1: Fix A — `arrLen`/`arrGet` must walk the prototype chain

**Files:**
- Modify: `src/ArrayPrototype.cpp:38` and `:70`

**Context:** `arrLen` reads `length` from the object's OWN properties only (`getAttribute(false)`). When `Array.prototype.map.call(obj, fn)` is called with an array-like `obj` whose `length` lives on its prototype, `arrLen` returns 0 and the callback never fires. Same for `arrGet` and indexed elements. This is the single highest-yield fix — it affects every array method simultaneously.

- [ ] **Step 1: Write failing test**

Create `/tmp/test_p39_a.js`:
```javascript
// length on prototype — arrLen must walk chain
var proto = { length: 3 };
proto[0] = 'a'; proto[1] = 'b'; proto[2] = 'c';
var obj = Object.create(proto);
var result = Array.prototype.map.call(obj, function(x) { return x.toUpperCase(); });
if (result.length !== 3) throw new Error('FAIL map: expected length 3, got ' + result.length);
if (result[0] !== 'A') throw new Error('FAIL map[0]: expected A, got ' + result[0]);

// elements on prototype — arrGet must walk chain
var base = {}; base[0] = 10; base[1] = 20; base[2] = 30;
var child = Object.create(base);
child.length = 3;
var sum = Array.prototype.reduce.call(child, function(acc, v) { return acc + v; }, 0);
if (sum !== 60) throw new Error('FAIL reduce: expected 60, got ' + sum);

throw new Error('PASS');
```

- [ ] **Step 2: Run test — confirm FAIL**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_a.js 2>&1 | grep "Error:"
```
Expected: `Error: FAIL map: expected length 3, got 0`

- [ ] **Step 3: Apply fix**

In `src/ArrayPrototype.cpp` line 38, change `false` to `true`:
```cpp
// FROM:
    const proto::ProtoObject* lenObj = arr->getAttribute(ctx, key, false);
// TO:
    const proto::ProtoObject* lenObj = arr->getAttribute(ctx, key, true);
```

In `src/ArrayPrototype.cpp` line 70, change `false` to `true`:
```cpp
// FROM:
    const proto::ProtoObject* val = arr->getAttribute(ctx, key, false);
// TO:
    const proto::ProtoObject* val = arr->getAttribute(ctx, key, true);
```

- [ ] **Step 4: Rebuild and run test**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3 && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_a.js 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 5: Quick regression check**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
TEST262_PATTERNS="built-ins/Array/prototype/map,built-ins/Array/prototype/reduce,built-ins/Array/prototype/forEach" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -3
```
Record the pass counts (expect improvement vs prior runs).

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add src/ArrayPrototype.cpp && \
git commit -m "fix: arrLen/arrGet walk prototype chain (getAttribute true) for array-like objects"
```

---

## Task 2: Fix B — `arrSet` must not auto-bump `length` on non-array objects

**Files:**
- Modify: `src/ArrayPrototype.cpp:85–91`

**Context:** `arrSet` unconditionally writes `length = idx + 1` when a new index is set. When an array method is called on a plain object whose `length` is controlled by a prototype accessor, `arrSet` creates an own `length` property that shadows the prototype getter. Subsequent reads of `obj.length` see the wrong value. The fix guards `length` bumping with a check for the `__is_array__` marker (`JSSymbols::isArray(ctx)` returns the key for `"__is_array__"`; array prototypes stamp this marker at line 1599 of `ArrayPrototype.cpp`).

- [ ] **Step 1: Write failing test**

Create `/tmp/test_p39_b.js`:
```javascript
// arrSet must NOT create own `length` on a plain object
var counts = [];
var obj = { length: 0 };
Object.defineProperty(obj, 'length', {
    get: function() { return counts.length; },
    set: function(v) { /* ignore */ },
    configurable: true
});
// Call map on obj that has length 0 (getter) — map should produce empty result
// because the getter returns 0, not whatever arrSet writes.
var result = Array.prototype.map.call(obj, function(x) { return x; });
// The key assertion: obj must not have an own 'length' created by arrSet
if (Object.prototype.hasOwnProperty.call(obj, 'length')) {
    // Check if the own length was wrongly set
    var ownDesc = Object.getOwnPropertyDescriptor(obj, 'length');
    if (ownDesc && !ownDesc.get) {
        throw new Error('FAIL: arrSet created own length property on non-array: ' + ownDesc.value);
    }
}

// Simpler case: arrSet should bump length only on real arrays
var arr = [1, 2, 3];
arr[5] = 6;
if (arr.length !== 6) throw new Error('FAIL: real array length should be 6, got ' + arr.length);

throw new Error('PASS');
```

- [ ] **Step 2: Run test — confirm behavior**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_b.js 2>&1 | grep "Error:"
```
Expected: `Error: FAIL: arrSet created own length property on non-array: ...` or `Error: PASS` if the bug is already masked by another mechanism. If PASS, proceed anyway (the fix is still correct per spec).

- [ ] **Step 3: Apply fix**

In `src/ArrayPrototype.cpp`, find lines 85–91 inside `arrSet`:
```cpp
    unsigned long curLen = arrLen(ctx, arr);
    if (idx + 1 > curLen) {
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey)
            arr = arr->setAttribute(ctx, lenKey,
                                    ctx->fromInteger(static_cast<long long>(idx + 1)));
    }
```

Replace with:
```cpp
    unsigned long curLen = arrLen(ctx, arr);
    if (idx + 1 > curLen) {
        // Only update length on real arrays (those carrying the __is_array__ marker),
        // not on plain objects used as array-likes — that would shadow prototype getters.
        const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
        const proto::ProtoObject* isArrVal = isArrKey
            ? arr->getAttribute(ctx, isArrKey, true) : nullptr;
        if (isArrVal == PROTO_TRUE) {
            const proto::ProtoString* lenKey = JSSymbols::length(ctx);
            if (lenKey)
                arr = arr->setAttribute(ctx, lenKey,
                                        ctx->fromInteger(static_cast<long long>(idx + 1)));
        }
    }
```

- [ ] **Step 4: Rebuild and run test**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3 && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_b.js 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 5: Verify real array still works**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs -e "var a=[1,2,3]; a[5]=6; if(a.length!==6) throw new Error('FAIL '+a.length); throw new Error('PASS');" 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add src/ArrayPrototype.cpp && \
git commit -m "fix: arrSet only bumps length on real arrays, not plain array-like objects"
```

---

## Task 3: Fix F — `stringSplit` must coerce non-string separator via `ToString`

**Files:**
- Modify: `src/StringPrototype.cpp:929`

**Context:** ECMAScript requires `ToString(separator)` coercion. When the separator is `null`, a number, or a boolean, line 929 returns `PROTO_NONE` (undefined) instead of converting to `"null"`, `"0"`, `"false"` etc. `objToStr(ctx, sepArg)` (already available in the same file, line 26) handles all non-string primitives correctly.

- [ ] **Step 1: Write failing test**

Create `/tmp/test_p39_f.js`:
```javascript
// null separator → split on "null"
var r1 = "gnulluna".split(null);
if (!Array.isArray(r1)) throw new Error('FAIL split(null) not array');
if (r1.length !== 2) throw new Error('FAIL split(null) expected 2 parts, got ' + r1.length);
if (r1[0] !== 'g' || r1[1] !== 'una') throw new Error('FAIL split(null) got ' + JSON.stringify(r1));

// number separator → split on "1"
var r2 = "a1b1c".split(1);
if (!Array.isArray(r2)) throw new Error('FAIL split(1) not array');
if (r2.length !== 3) throw new Error('FAIL split(1) expected 3 parts, got ' + r2.length);

// boolean false separator → split on "false"
var r3 = "afalsebc".split(false);
if (!Array.isArray(r3)) throw new Error('FAIL split(false) not array');
if (r3.length !== 2) throw new Error('FAIL split(false) expected 2 parts, got ' + r3.length);

throw new Error('PASS');
```

- [ ] **Step 2: Run test — confirm FAIL**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_f.js 2>&1 | grep "Error:"
```
Expected: `Error: FAIL split(null) not array` or similar.

- [ ] **Step 3: Apply fix**

In `src/StringPrototype.cpp`, find lines 927–931:
```cpp
    // Non-string separator (e.g., regex): return PROTO_NONE to preserve vacuous-pass
    if (!sepArg->isString(ctx)) return PROTO_NONE;

    std::string sep = objToStr(ctx, sepArg);
```

Replace with (remove the early return; `objToStr` handles all types):
```cpp
    // Non-regexp, non-string separator: coerce to string via ToString (ECMAScript step 8).
    // objToStr handles null→"null", numbers, booleans, etc. correctly.
    std::string sep = objToStr(ctx, sepArg);
```

- [ ] **Step 4: Rebuild and run test**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3 && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_f.js 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 5: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add src/StringPrototype.cpp && \
git commit -m "fix: stringSplit coerces non-string separator via ToString instead of returning undefined"
```

---

## Task 4: Fix G — `String.prototype` methods must expose correct `.length`

**Files:**
- Modify: `src/StringPrototype.cpp:1074–1081`

**Context:** The `reg` lambda in `BuildStringPrototype` discards its `length` parameter with `(void)length`. So `String.prototype.trim.length === undefined`, `String.prototype.split.length === undefined`, etc. ECMAScript requires each built-in to have a non-writable, non-enumerable, configurable `.length`. Since these are raw `ProtoMethod` cells (not wrapper objects), we must attach `.length` and its descriptor sidecar **directly on the string prototype** as compound keys `__len_<name>__` (with the length value) and `__pd___len_<name>__` (with bits 0x2). Wait — that approach is too complex. The simpler approach: in `ensureStringConstructor` (line 1126), after the prototype is set on the constructor, patch each method object on the prototype with a `.length` property. But raw ProtoMethod cells cannot carry attributes.

The correct approach: replace `ctx->fromMethod(mp, fn)` in the `reg` lambda with a small wrapper object. Each method becomes a child object of `mp` that stores `__native_fn__` (the raw method), `length`, `name`, and their descriptors. This is what `wrapNativeFunction` does, but `wrapNativeFunction` requires `globalRoot` (unavailable at `BuildStringPrototype` time). Instead, create a minimal wrapper inline:

- [ ] **Step 1: Write failing test**

Create `/tmp/test_p39_g.js`:
```javascript
// String.prototype methods must have .length
if (String.prototype.trim.length !== 0)
    throw new Error('FAIL trim.length: expected 0, got ' + String.prototype.trim.length);
if (String.prototype.split.length !== 2)
    throw new Error('FAIL split.length: expected 2, got ' + String.prototype.split.length);
if (String.prototype.indexOf.length !== 1)
    throw new Error('FAIL indexOf.length: expected 1, got ' + String.prototype.indexOf.length);
if (String.prototype.slice.length !== 2)
    throw new Error('FAIL slice.length: expected 2, got ' + String.prototype.slice.length);

// .length descriptor: non-writable, configurable, non-enumerable
var desc = Object.getOwnPropertyDescriptor(String.prototype.trim, 'length');
if (!desc) throw new Error('FAIL trim.length descriptor missing');
if (desc.writable !== false) throw new Error('FAIL trim.length.writable should be false');
if (desc.configurable !== true) throw new Error('FAIL trim.length.configurable should be true');
if (desc.enumerable !== false) throw new Error('FAIL trim.length.enumerable should be false');

throw new Error('PASS');
```

- [ ] **Step 2: Run test — confirm FAIL**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_g.js 2>&1 | grep "Error:"
```
Expected: `Error: FAIL trim.length: expected 0, got undefined`

- [ ] **Step 3: Apply fix — replace `reg` lambda with wrapper-based install**

In `src/StringPrototype.cpp`, replace lines 1074–1081:
```cpp
    auto reg = [&](const char* name, proto::ProtoMethod fn, long long length) {
        const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
        if (key) {
            const proto::ProtoObject* mObj = ctx->fromMethod(mp, fn);
            sp = sp->setAttribute(ctx, key, mObj);
            (void)length; // length unavailable for raw METHOD cells
        }
    };
```

With:
```cpp
    auto reg = [&](const char* name, proto::ProtoMethod fn, long long length) {
        const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
        if (!key) return;

        // Build a minimal wrapper object: child of mp carrying __native_fn__, length, name.
        // This mirrors wrapNativeFunction but without needing globalRoot.
        const proto::ProtoObject* wrapper = ctx->newObject(true);
        if (!wrapper) return;

        // __native_fn__ — raw callable
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        const proto::ProtoObject* rawMethod = ctx->fromMethod(mp, fn);
        if (nfKey && rawMethod) wrapper = wrapper->setAttribute(ctx, nfKey, rawMethod);

        // length: {writable:false, enumerable:false, configurable:true} → 0x2
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey) {
            wrapper = wrapper->setAttribute(ctx, lenKey, ctx->fromInteger(length));
            const proto::ProtoObject* pdlko = ctx->fromUTF8String("__pd_length__");
            const proto::ProtoString* pdlk = pdlko ? pdlko->asString(ctx) : nullptr;
            if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2));
        }

        // name: {writable:false, enumerable:false, configurable:true} → 0x2
        const proto::ProtoString* nmKey = JSSymbols::name(ctx);
        if (nmKey) {
            wrapper = wrapper->setAttribute(ctx, nmKey, ctx->fromUTF8String(name));
            const proto::ProtoObject* pdnko = ctx->fromUTF8String("__pd_name__");
            const proto::ProtoString* pdnk = pdnko ? pdnko->asString(ctx) : nullptr;
            if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2));
        }

        sp = sp->setAttribute(ctx, key, wrapper);
    };
```

**Important:** `mp` is defined at line 1067 as `proto::ProtoObject* mp = const_cast<proto::ProtoObject*>(sp)`. The wrapper's parent is `mp` (initial sp). The `__native_fn__` approach means the interpreter dispatches via the `__native_fn__` code path (same as `wrapNativeFunction` wrappers), so these will be fully callable.

- [ ] **Step 4: Rebuild and run test**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3 && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_g.js 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 5: Verify String methods still work**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs -e "var r='hello'.split('l'); if(r.length!==3) throw new Error('FAIL '+r.length); throw new Error('PASS');" 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add src/StringPrototype.cpp && \
git commit -m "fix: String.prototype methods get correct .length and .name via __native_fn__ wrappers"
```

---

## Task 5: Fix C — `arguments` object needs `@@toStringTag = "Arguments"`

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp:1487–1498`

**Context:** `Object.prototype.toString.call(arguments)` returns `[object Object]` instead of `[object Arguments]`. The `arguments` object is created at lines 1487–1498 of `ProtoInterpreter.cpp` inside `OP_special_object` (opcode handler for `soKind == 0 || soKind == 1`). The fix adds `__toStringTag__ = "Arguments"` as an own property. `JSSymbols::toStringTag(ctx)` returns the interned `"__toStringTag__"` key that `objectToString` checks.

- [ ] **Step 1: Write failing test**

Create `/tmp/test_p39_c.js`:
```javascript
function f() {
    var tag = Object.prototype.toString.call(arguments);
    if (tag !== '[object Arguments]')
        throw new Error('FAIL: expected [object Arguments], got ' + tag);
    throw new Error('PASS');
}
f(1, 2, 3);
```

- [ ] **Step 2: Run test — confirm FAIL**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_c.js 2>&1 | grep "Error:"
```
Expected: `Error: FAIL: expected [object Arguments], got [object Object]`

- [ ] **Step 3: Apply fix**

In `src/runtime/ProtoInterpreter.cpp`, find lines 1495–1498:
```cpp
                    const proto::ProtoString* lenKey2 = JSSymbols::length(pContext);
                    if (lenKey2 && argsObj)
                        argsObj = argsObj->setAttribute(pContext, lenKey2, pContext->fromInteger(static_cast<long long>(argc2)));
                    stackPush(pContext, argsObj ? argsObj : PROTO_NONE);
```

Replace with:
```cpp
                    const proto::ProtoString* lenKey2 = JSSymbols::length(pContext);
                    if (lenKey2 && argsObj)
                        argsObj = argsObj->setAttribute(pContext, lenKey2, pContext->fromInteger(static_cast<long long>(argc2)));
                    // @@toStringTag: Object.prototype.toString.call(arguments) === "[object Arguments]"
                    const proto::ProtoString* tagKey = JSSymbols::toStringTag(pContext);
                    if (tagKey && argsObj)
                        argsObj = argsObj->setAttribute(pContext, tagKey, pContext->fromUTF8String("Arguments"));
                    stackPush(pContext, argsObj ? argsObj : PROTO_NONE);
```

- [ ] **Step 4: Rebuild and run test**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3 && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_c.js 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 5: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add src/runtime/ProtoInterpreter.cpp && \
git commit -m "fix: arguments object gets @@toStringTag = \"Arguments\" for Object.prototype.toString"
```

---

## Task 6: Fix E — Diagnose and fix String method dispatch on wrapper objects

**Files:**
- Modify: `src/StringPrototype.cpp` (investigation determines exact lines)

**Context:** `new String("hello")` creates a wrapper object that is a child of `space->stringPrototype`. The methods on `stringPrototype` were installed via `ctx->fromMethod(mp, fn)` (raw ProtoMethod cells) before Task 4 changed them to `__native_fn__` wrappers. After Task 4 the `reg` lambda creates proper wrapper objects, so method dispatch via prototype chain lookup should work. This task verifies whether `new String("hello").split("l")` works after Task 4 and fixes any remaining issues.

- [ ] **Step 1: Write diagnostic test**

Create `/tmp/test_p39_e.js`:
```javascript
// Part 1: new String() prototype chain
var s = new String("hello");
var typeofSplit = typeof s.split;
if (typeofSplit !== 'function')
    throw new Error('FAIL typeof split on wrapper: ' + typeofSplit);

// Part 2: calling method on wrapper
var parts = s.split("l");
if (!Array.isArray(parts))
    throw new Error('FAIL split on wrapper not array');
if (parts.length !== 3)
    throw new Error('FAIL split on wrapper: expected 3 parts, got ' + parts.length);

// Part 3: String.prototype.trim.call(wrapper)
var padded = new String("  hi  ");
var trimmed = padded.trim();
if (trimmed !== 'hi')
    throw new Error('FAIL trim on wrapper: got ' + JSON.stringify(trimmed));

// Part 4: length of wrapper
if (s.length !== 5)
    throw new Error('FAIL wrapper.length: expected 5, got ' + s.length);

throw new Error('PASS');
```

- [ ] **Step 2: Run diagnostic — check actual failure**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_e.js 2>&1 | grep "Error:"
```

**Read the actual error carefully.**

- [ ] **Step 3: Apply targeted fix based on diagnostic result**

**Case A — `Error: PASS`:** Task 4 already fixed this. No further action needed for Fix E. Skip to Step 5.

**Case B — `FAIL typeof split on wrapper: object`:** The wrapper objects created by Task 4's `reg` lambda don't carry `__native_fn__` in a form the interpreter's "is callable" check recognizes. Verify that the interpreter's `OP_call_method` checks for `__native_fn__` attribute, and that `ctx->newObject(true)` creates a mutable object that can store attributes. If the wrapper check is correct, the issue may be that `JSSymbols::nativeFn(ctx)` returns a different key than the interpreter expects — read `src/FunctionPrototype.cpp` function `wrapNativeFunction` (which IS known to work) and mirror its exact attribute-setting code.

**Case C — `FAIL split on wrapper: expected 3 parts`:** The method is callable but `self` inside `stringSplit` is the wrapper object. Verify that `requireStringThis` passes (it should — it only rejects null/undefined). Then verify `objToStr(ctx, self)` returns `"hello"` for the wrapper (it should — it reads `__primitive_value__` at line 47). If `objToStr` is returning `"[object Object]"` or `""`, check whether `__primitive_value__` is correctly set on the wrapper at `OP_new` time (ProtoInterpreter.cpp ~line 4088). If `__primitive_value__` uses `getAttribute(false)` (own only) it should find the value since `OP_new` sets it as own property.

**Case D — `FAIL wrapper.length: expected 5`:** String wrapper objects need a `length` property equal to the string length. This is normally set via `String.prototype.length` getter. If there is no such getter, add one during `BuildStringPrototype` or `ensureStringConstructor`.

For Cases B/C/D: implement the minimal fix described above, test, then proceed.

- [ ] **Step 4: Rebuild and run diagnostic**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3 && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_e.js 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 5: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add src/StringPrototype.cpp && \
git commit -m "fix: String wrapper objects (new String()) correctly dispatch String.prototype methods"
```
*(Adjust staged files based on what was modified.)*

---

## Task 7: Fix D — ToObject coercion for primitive `this` in Array methods

**Files:**
- Modify: `src/ArrayPrototype.cpp` — add helper after `arrSet`, apply in each method after `arrayThrowIfNullUndefined`

**Context:** `Array.prototype.reduce.call(false, cb, 1)` where `Boolean.prototype[0] = true; Boolean.prototype.length = 1` fails because `false` (a primitive) is passed as `self`. ECMAScript requires `ToObject(thisArg)` before treating it as an array-like. `ctx->space->booleanPrototype` is available (verified in `protoCore.h` line 681). There is no `numberPrototype` in `ProtoSpace` — for number primitives, fall back to treating the raw value as the receiver (Fix A's getAttribute(true) may already handle some cases).

- [ ] **Step 1: Write failing test**

Create `/tmp/test_p39_d.js`:
```javascript
// Boolean primitive this — Boolean.prototype has indexed elements
Boolean.prototype[0] = true;
Boolean.prototype.length = 1;
var result = Array.prototype.reduce.call(false, function(acc, val, idx, obj) {
    return val === true;
}, false);
if (result !== true) throw new Error('FAIL Boolean.prototype this: ' + result);

// Also test forEach
var seen = [];
Array.prototype.forEach.call(false, function(v) { seen.push(v); });
if (seen.length !== 1 || seen[0] !== true)
    throw new Error('FAIL forEach on Boolean this: ' + JSON.stringify(seen));

throw new Error('PASS');
```

- [ ] **Step 2: Run test — confirm FAIL**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_d.js 2>&1 | grep "Error:"
```
Expected: `Error: FAIL Boolean.prototype this: false` (or similar). If Fix A already fixed this (because `getAttribute(true)` on a boolean primitive walks the chain via `booleanPrototype`), expect `Error: PASS` — skip to Step 5.

- [ ] **Step 3: Add `toObjectCoerce` helper after `arrSet` in `src/ArrayPrototype.cpp`**

After line 93 (end of `arrSet`), add:
```cpp
// Coerces a primitive receiver to an object for array-method dispatch (ECMAScript ToObject).
// For boolean primitives: wraps in a child of space->booleanPrototype.
// For string/number primitives: returns the original value (getAttribute(true) handles their
// prototype chains directly in most engines; full boxing deferred if not needed).
// Returns PROTO_NONE for null/undefined — callers already guard against those via
// arrayThrowIfNullUndefined.
static const proto::ProtoObject* arrayToObject(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* val) {
    if (!val || val == PROTO_NONE) return PROTO_NONE;
    if (val->isBoolean(ctx) && ctx->space && ctx->space->booleanPrototype) {
        const proto::ProtoObject* boxed =
            reinterpret_cast<const proto::ProtoObject*>(ctx->space->booleanPrototype)
                ->newChild(ctx, true);
        if (boxed) {
            const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
            if (pvKey) boxed = boxed->setAttribute(ctx, pvKey, val);
            return boxed;
        }
    }
    return val; // already an object, or unboxable primitive
}
```

- [ ] **Step 4: Apply `arrayToObject` in every method that takes array-like `this`**

In `src/ArrayPrototype.cpp`, find every method that has the pattern:
```cpp
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
```

After each such line, add:
```cpp
    self = arrayToObject(ctx, self);
```

Methods to update: `arrayJoin`, `arrayPush`, `arrayPop`, `arrayShift`, `arrayUnshift`, `arraySlice`, `arraySplice`, `arrayReverse`, `arraySort`, `arrayIndexOf`, `arrayLastIndexOf`, `arrayForEach`, `arrayMap`, `arrayFilter`, `arrayReduce`, `arrayReduceRight`, `arraySome`, `arrayEvery`, `arrayFind`, `arrayFindIndex`, `arrayFlat`, `arrayFlatMap`, `arrayFill`, `arrayCopyWithin`. Use Grep to find all occurrences of `arrayThrowIfNullUndefined` and add the line after each one. Do NOT modify `arrayIsArray` or array constructor functions (they don't take array-like `this`).

- [ ] **Step 5: Rebuild and run test**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3 && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p39_d.js 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 6: Verify no regression in basic array operations**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs -e "
var a = [1,2,3];
if (a.map(function(x){return x*2;})[1] !== 4) throw new Error('FAIL map');
if (a.filter(function(x){return x>1;}).length !== 2) throw new Error('FAIL filter');
if (a.reduce(function(s,x){return s+x;},0) !== 6) throw new Error('FAIL reduce');
throw new Error('PASS');
" 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 7: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add src/ArrayPrototype.cpp && \
git commit -m "fix: Array methods apply ToObject coercion to primitive this (boolean boxing)"
```

---

## Task 8: Full test262 sweep and `TEST262_STATUS.md` update

**Files:**
- Modify: `docs/TEST262_STATUS.md`

- [ ] **Step 1: Run Array suite**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
TEST262_PATTERNS="built-ins/Array" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```
Read the generated snapshot JSON for `summary.passed`.

- [ ] **Step 2: Run String suite**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
TEST262_PATTERNS="built-ins/String" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```
Read the generated snapshot JSON for `summary.passed`.

- [ ] **Step 3: Run regression check**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
TEST262_PATTERNS="language/expressions" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```
Must be ≥ 9,422 (Phase 38 baseline). If it drops, investigate before committing.

- [ ] **Step 4: Update `docs/TEST262_STATUS.md`**

Read the current `docs/TEST262_STATUS.md`. Add a new Phase 39 snapshot entry at the top (before the Phase 38 entry), following the existing format:

```markdown
## Phase 39 Snapshot — 2026-04-14  ✅ CURRENT

> **Phase 39 target:** Array + String prototype repair — ...
> Snapshot files: (list the three snapshot file names)

### Results

| Area | Total | Passed | Pass % | Phase 38 Baseline | Delta |
|------|------:|-------:|-------:|------------------:|-------|
| `built-ins/Array` | 3,081 | <actual> | <actual%> | 1,543 (50.1%) | <delta> |
| `built-ins/String` | 1,223 | <actual> | <actual%> | 514 (42.0%) | <delta> |
| `language/expressions` | 11,036 | <actual> | <actual%> | 9,422 (85.4%) | <delta> |

### Key implementations delivered

| Feature | Files | Tests recovered |
|---------|-------|----------------|
| `arrLen`/`arrGet` walk prototype chain | `src/ArrayPrototype.cpp` | <actual> |
| `arrSet` only bumps length on real arrays | `src/ArrayPrototype.cpp` | <actual> |
| `stringSplit` coerces separator via ToString | `src/StringPrototype.cpp` | <actual> |
| `String.prototype` methods get correct `.length` via `__native_fn__` wrappers | `src/StringPrototype.cpp` | <actual> |
| `arguments` object gets `@@toStringTag = "Arguments"` | `src/runtime/ProtoInterpreter.cpp` | <actual> |
| String wrapper objects dispatch String.prototype methods | `src/StringPrototype.cpp` | <actual> |
| Array methods apply ToObject coercion for boolean primitive `this` | `src/ArrayPrototype.cpp` | <actual> |

### Notes

(Write 2-3 sentences per area: what passed, what still fails, dominant failure category.)

---
```

Mark the old `## Phase 38 Snapshot` as `(superseded by Phase 39)` by removing `✅ CURRENT` from its header.

- [ ] **Step 5: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add docs/TEST262_STATUS.md && \
git commit -m "docs: Phase 39 snapshot — Array + String prototype repair"
```

---

## Self-Review

**Spec coverage:**
- [x] Fix A (arrLen/arrGet true) → Task 1
- [x] Fix B (arrSet isArray guard) → Task 2
- [x] Fix C (arguments toStringTag) → Task 5
- [x] Fix D (ToObject coercion) → Task 7
- [x] Fix E (String wrapper dispatch) → Task 6
- [x] Fix F (stringSplit separator coercion) → Task 3
- [x] Fix G (String.prototype method .length) → Task 4

**Task ordering rationale:** Tasks 1–2 (Array) and 3–4 (String) first — these are the clearest fixes. Task 5 (arguments) is small and independent. Task 6 (Fix E) is investigative but may be largely solved by Task 4's wrapper upgrade. Task 7 (Fix D) is most complex and lower priority; it can be skipped if tests confirm primitive `this` already works via Fix A's chain walking.

**Placeholder scan:** Task 6 Step 3 contains conditional branches (Cases A–D) — this is intentional for an investigative task, not a placeholder. All branches have concrete implementation guidance.

**Type consistency:** `arrayToObject` defined in Task 7 Step 3 matches usage in Task 7 Step 4. `JSSymbols::toStringTag(pContext)` used in Task 5 matches `JSSymbols::toStringTag(ctx)` — same function, different parameter name by convention.
