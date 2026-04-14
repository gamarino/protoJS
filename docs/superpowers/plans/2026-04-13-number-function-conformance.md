# Number + Function Conformance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recover ~80-110 test262 tests by fixing property descriptors on Number constants and prototype methods, adding RangeError validation in `Number.prototype.toString`, fixing `Function.prototype.toString` to include the function name, and correcting `name`/`length` descriptors in `wrapNativeFunction`.

**Architecture:** Four independent fixes across two files. `installNonEnumerableMethod` (from `PrototypeUtils.h`, introduced in Phase 35) is reused for Number prototype methods. All fixes follow the established `__pd_<name>__` sidecar descriptor pattern (bit0=writable 0x1, bit1=configurable 0x2, bit2=enumerable 0x4).

**Tech Stack:** C++20, protoCore ProtoSparseList/ProtoObject, JSSymbols, CMake build (`cmake --build build --target protojs -j$(nproc)`).

---

## Baselines (measured 2026-04-13)

| Suite | Passed | Total | Pass Rate |
|-------|--------|-------|-----------|
| `built-ins/Number` | 68 | 338 | 20.1% |
| `built-ins/Function` | 209 | 509 | 41.1% |
| `language/expressions` | 9418 | 11036 | 85.3% |

---

## File Structure

| File | Changes |
|------|---------|
| `src/NumberPrototype.cpp` | Task 1 (radix RangeError), Task 2 (constant descriptors), Task 3 (prototype method descriptors) |
| `src/FunctionPrototype.cpp` | Task 4 (toString name), Task 5 (wrapNativeFunction descriptors) |

---

## Background: Descriptor Sidecar System

protoJS stores property descriptor flags in a sidecar attribute `__pd_<propertyName>__` as a packed integer:
- `0x0` = not writable, not configurable, not enumerable (constants)
- `0x2` = not writable, configurable, not enumerable (function `name`/`length`)
- `0x3` = writable, configurable, not enumerable (prototype methods)
- `0x7` = writable, configurable, enumerable (default when no sidecar exists)

When no `__pd_<name>__` exists, all bits default to true (enumerable). Built-in prototype methods must be non-enumerable (`0x3`). Constants must be non-writable, non-configurable, non-enumerable (`0x0`).

The helper `installNonEnumerableMethod(ctx, proto, "name", fn, argc)` from `src/PrototypeUtils.h` creates a method wrapper with `0x3` on the property, and `0x2` on `fn.length` / `fn.name`. Use it for prototype methods.

---

## Task 1: Fix `Number.prototype.toString` — throw RangeError for invalid radix

**Files:**
- Modify: `src/NumberPrototype.cpp:105-107`

- [ ] **Step 1: Write failing test**

Create `/tmp/test_p37_t1.js`:
```javascript
// Test 1: radix 1 must throw RangeError
var threw = false;
try { (42).toString(1); } catch(e) { if (e instanceof RangeError) threw = true; }
if (!threw) throw new Error('FAIL: toString(1) should throw RangeError');

// Test 2: radix 37 must throw RangeError
threw = false;
try { (42).toString(37); } catch(e) { if (e instanceof RangeError) threw = true; }
if (!threw) throw new Error('FAIL: toString(37) should throw RangeError');

// Test 3: radix 2 must NOT throw (valid)
var r = (10).toString(2);
if (r !== '1010') throw new Error('FAIL: (10).toString(2) should be "1010", got ' + r);

// Test 4: radix 36 must NOT throw (valid)
var r36 = (255).toString(36);
if (typeof r36 !== 'string' || r36.length === 0) throw new Error('FAIL: toString(36) should return string');

throw new Error('PASS');
```

- [ ] **Step 2: Confirm test fails**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p37_t1.js 2>&1 | grep "Error:"
```
Expected: `Error: FAIL: toString(1) should throw RangeError`

- [ ] **Step 3: Implement fix**

In `src/NumberPrototype.cpp`, find lines 105-107:
```cpp
    if (radix < 2 || radix > 36) {
        radix = 10;
    }
```

Replace with:
```cpp
    if (radix < 2 || radix > 36) {
        signalNativeException(makeNativeError(context, "RangeError",
            "Number.prototype.toString() radix must be between 2 and 36"));
        return PROTO_NONE;
    }
```

- [ ] **Step 4: Rebuild and run test**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3 && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p37_t1.js 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 5: Run targeted test262**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
TEST262_PATTERNS="built-ins/Number/prototype/toString" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```
Expected: improvement over previous count.

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add src/NumberPrototype.cpp && \
git commit -m "fix: Number.prototype.toString — throw RangeError for invalid radix (< 2 or > 36)"
```

---

## Task 2: Fix Number constructor constants — add non-writable, non-configurable, non-enumerable descriptors

**Files:**
- Modify: `src/NumberPrototype.cpp:492-503`

Constants `EPSILON`, `MAX_VALUE`, `MIN_VALUE`, `MAX_SAFE_INTEGER`, `MIN_SAFE_INTEGER`, `POSITIVE_INFINITY`, `NEGATIVE_INFINITY`, `NaN` must have descriptor `{writable: false, enumerable: false, configurable: false}` → bits = `0x0`.

- [ ] **Step 1: Write failing test**

Create `/tmp/test_p37_t2.js`:
```javascript
// Test: Number.EPSILON must be non-writable, non-enumerable, non-configurable
var desc = Object.getOwnPropertyDescriptor(Number, 'EPSILON');
if (!desc) throw new Error('FAIL: EPSILON descriptor is undefined');
if (desc.writable !== false) throw new Error('FAIL: EPSILON.writable should be false, got ' + desc.writable);
if (desc.enumerable !== false) throw new Error('FAIL: EPSILON.enumerable should be false, got ' + desc.enumerable);
if (desc.configurable !== false) throw new Error('FAIL: EPSILON.configurable should be false, got ' + desc.configurable);

// Test: MAX_SAFE_INTEGER must be non-writable
var desc2 = Object.getOwnPropertyDescriptor(Number, 'MAX_SAFE_INTEGER');
if (!desc2) throw new Error('FAIL: MAX_SAFE_INTEGER descriptor is undefined');
if (desc2.writable !== false) throw new Error('FAIL: MAX_SAFE_INTEGER.writable should be false');

// Test: Number.NaN must be non-writable
var desc3 = Object.getOwnPropertyDescriptor(Number, 'NaN');
if (!desc3) throw new Error('FAIL: NaN descriptor is undefined');
if (desc3.writable !== false) throw new Error('FAIL: Number.NaN.writable should be false');

throw new Error('PASS');
```

- [ ] **Step 2: Confirm test fails**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p37_t2.js 2>&1 | grep "Error:"
```
Expected: `Error: FAIL: EPSILON.writable should be false` (or similar — constants currently have no descriptor sidecar, so they default to writable=true).

- [ ] **Step 3: Implement fix**

In `src/NumberPrototype.cpp`, find the `setConst` lambda (~line 492):
```cpp
    auto setConst = [&](const char* name, double val) {
        const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
        if (key) ctor = ctor->setAttribute(ctx, key, ctx->fromDouble(val));
    };
```

Replace with:
```cpp
    auto setConst = [&](const char* name, double val) {
        const proto::ProtoObject* keyObj = ctx->fromUTF8String(name);
        const proto::ProtoString* key = keyObj ? keyObj->asString(ctx) : nullptr;
        if (!key) return;
        ctor = ctor->setAttribute(ctx, key, ctx->fromDouble(val));
        // Constants: {writable: false, enumerable: false, configurable: false} → bits = 0x0
        std::string pdKeyStr = "__pd_";
        pdKeyStr += name;
        pdKeyStr += "__";
        const proto::ProtoObject* pdko = ctx->fromUTF8String(pdKeyStr.c_str());
        const proto::ProtoString* pdk = pdko ? pdko->asString(ctx) : nullptr;
        if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x0));
    };
```

- [ ] **Step 4: Rebuild and run test**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3 && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p37_t2.js 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 5: Run targeted test262**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
TEST262_PATTERNS="built-ins/Number/EPSILON" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -3
```
Expected: all EPSILON descriptor tests pass.

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add src/NumberPrototype.cpp && \
git commit -m "fix: Number constants — add non-writable/non-enumerable/non-configurable descriptor (bits=0x0)"
```

---

## Task 3: Fix `Number.prototype` methods — add non-enumerable descriptors

**Files:**
- Modify: `src/NumberPrototype.cpp:376-403`

`toString`, `toFixed`, `toExponential`, `toPrecision`, `valueOf` must be non-enumerable (`0x3`). `installNonEnumerableMethod` from `src/PrototypeUtils.h` handles this correctly.

Note: `BuildNumberPrototype` uses `objectProto->newChild(ctx, false)` for the prototype (immutable by default) then casts to mutable. `installNonEnumerableMethod` returns the updated pointer — assign it back each time.

- [ ] **Step 1: Write failing test**

Create `/tmp/test_p37_t3.js`:
```javascript
// Number.prototype methods must be non-enumerable
var desc = Object.getOwnPropertyDescriptor(Number.prototype, 'toString');
if (!desc) throw new Error('FAIL: Number.prototype.toString descriptor is undefined');
if (desc.enumerable !== false) throw new Error('FAIL: toString should be non-enumerable, got ' + desc.enumerable);
if (desc.writable !== true) throw new Error('FAIL: toString should be writable, got ' + desc.writable);
if (desc.configurable !== true) throw new Error('FAIL: toString should be configurable, got ' + desc.configurable);

var desc2 = Object.getOwnPropertyDescriptor(Number.prototype, 'toFixed');
if (!desc2) throw new Error('FAIL: toFixed descriptor is undefined');
if (desc2.enumerable !== false) throw new Error('FAIL: toFixed should be non-enumerable');

// Verify methods still work
if ((255).toString(16) !== 'ff') throw new Error('FAIL: toString(16) broken');
if ((1.5).toFixed(0) !== '2') throw new Error('FAIL: toFixed(0) broken');

throw new Error('PASS');
```

- [ ] **Step 2: Confirm test fails**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p37_t3.js 2>&1 | grep "Error:"
```
Expected: `Error: FAIL: toString should be non-enumerable, got true`

- [ ] **Step 3: Implement fix**

In `src/NumberPrototype.cpp`, add the include at the top (after existing includes):
```cpp
#include "PrototypeUtils.h"
```

Then in `BuildNumberPrototype`, replace lines 389-398:
```cpp
    numberProto = numberProto->setAttribute(ctx, keyValueOf,
        ctx->fromMethod(mutableProto, numberValueOf));
    numberProto = numberProto->setAttribute(ctx, keyToString,
        ctx->fromMethod(mutableProto, numberToString));
    numberProto = numberProto->setAttribute(ctx, keyToFixed,
        ctx->fromMethod(mutableProto, numberToFixed));
    numberProto = numberProto->setAttribute(ctx, keyToExponential,
        ctx->fromMethod(mutableProto, numberToExponential));
    numberProto = numberProto->setAttribute(ctx, keyToPrecision,
        ctx->fromMethod(mutableProto, numberToPrecision));
```

With:
```cpp
    numberProto = installNonEnumerableMethod(ctx, numberProto, "valueOf",       numberValueOf,       0);
    numberProto = installNonEnumerableMethod(ctx, numberProto, "toString",      numberToString,      1);
    numberProto = installNonEnumerableMethod(ctx, numberProto, "toFixed",       numberToFixed,       1);
    numberProto = installNonEnumerableMethod(ctx, numberProto, "toExponential", numberToExponential, 1);
    numberProto = installNonEnumerableMethod(ctx, numberProto, "toPrecision",   numberToPrecision,   1);
```

Also remove the now-unused JSSymbols key variables (lines 383-387) to keep the code clean:
```cpp
    // These lines can be removed since installNonEnumerableMethod uses string names directly:
    // const proto::ProtoString* keyValueOf      = JSSymbols::valueOf(ctx);
    // const proto::ProtoString* keyToString     = JSSymbols::toString(ctx);
    // const proto::ProtoString* keyToFixed      = JSSymbols::toFixed(ctx);
    // const proto::ProtoString* keyToExponential= JSSymbols::toExponential(ctx);
    // const proto::ProtoString* keyToPrecision  = JSSymbols::toPrecision(ctx);
```

Actually — do NOT remove those JSSymbols variables unless they are truly unused. Read the function first to confirm they are only used in the lines being replaced.

- [ ] **Step 4: Rebuild and run test**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3 && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p37_t3.js 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 5: Run targeted test262**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
TEST262_PATTERNS="built-ins/Number" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```
Expected: improvement over 68/338 baseline.

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add src/NumberPrototype.cpp && \
git commit -m "fix: Number.prototype methods — install with non-enumerable descriptors via installNonEnumerableMethod"
```

---

## Task 4: Fix `Function.prototype.toString` — include function name

**Files:**
- Modify: `src/FunctionPrototype.cpp:150-158`

The current `fnToString` always returns `"function () { [native code] }"`. It must return `"function <name>() { [native code] }"` using the `name` attribute of `self`.

- [ ] **Step 1: Write failing test**

Create `/tmp/test_p37_t4.js`:
```javascript
// Test 1: named function must include name in toString
function myFunc() {}
var s = Function.prototype.toString.call(myFunc);
// Must contain "myFunc" somewhere in the string
if (s.indexOf('myFunc') === -1) {
    throw new Error('FAIL: toString should include function name, got: ' + s);
}

// Test 2: anonymous function (name='') returns "function () { [native code] }"
var anonFn = function(){};
// In protoJS user functions have a name set from variable assignment or empty
// We only check that it is a string starting with "function"
var s2 = Function.prototype.toString.call(anonFn);
if (typeof s2 !== 'string' || s2.indexOf('function') !== 0) {
    throw new Error('FAIL: toString should return string starting with "function", got: ' + s2);
}

// Test 3: built-in native function (Array.isArray) must include its name
var s3 = Function.prototype.toString.call(Array.isArray);
if (s3.indexOf('isArray') === -1) {
    throw new Error('FAIL: built-in toString should include name "isArray", got: ' + s3);
}

// Test 4: TypeError if called on non-callable
var threw = false;
try { Function.prototype.toString.call(42); } catch(e) { if (e instanceof TypeError) threw = true; }
if (!threw) throw new Error('FAIL: toString on non-callable should throw TypeError');

throw new Error('PASS');
```

- [ ] **Step 2: Confirm test fails**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p37_t4.js 2>&1 | grep "Error:"
```
Expected: `Error: FAIL: toString should include function name, got: function () { [native code] }`

- [ ] **Step 3: Implement fix**

In `src/FunctionPrototype.cpp`, replace `fnToString` (lines 150-158):

```cpp
static const proto::ProtoObject* fnToString(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    return ctx->fromUTF8String("function () { [native code] }");
}
```

With:

```cpp
static const proto::ProtoObject* fnToString(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    // Per ES2019 §19.2.3.5: must be called on a callable; otherwise TypeError.
    if (!self || self == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Function.prototype.toString requires a callable"));
        return PROTO_NONE;
    }

    // Extract the function's name attribute.
    std::string fnName;
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) {
        const proto::ProtoObject* nameVal = self->getAttribute(ctx, nameKey, true);
        if (nameVal && nameVal != PROTO_NONE && nameVal->isString(ctx)) {
            nameVal->asString(ctx)->toUTF8String(ctx, fnName);
        }
    }

    std::string result = "function " + fnName + "() { [native code] }";
    return ctx->fromUTF8String(result.c_str());
}
```

- [ ] **Step 4: Rebuild and run test**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3 && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p37_t4.js 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 5: Run targeted test262**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
TEST262_PATTERNS="built-ins/Function/prototype/toString" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```
Expected: improvement over previous count.

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add src/FunctionPrototype.cpp && \
git commit -m "fix: Function.prototype.toString — include function name; throw TypeError for non-callables"
```

---

## Task 5: Fix `wrapNativeFunction` — add `{writable:false, enumerable:false, configurable:true}` to `name` and `length`

**Files:**
- Modify: `src/FunctionPrototype.cpp:252-255`

`name` and `length` on function wrapper objects must have descriptor bits `0x2` (configurable but not writable, not enumerable).

- [ ] **Step 1: Write failing test**

Create `/tmp/test_p37_t5.js`:
```javascript
// Test: function name and length must be non-writable and non-enumerable
function myFn(a, b, c) {}
var descName = Object.getOwnPropertyDescriptor(myFn, 'name');
if (!descName) throw new Error('FAIL: fn.name descriptor is undefined');
if (descName.writable !== false) throw new Error('FAIL: fn.name.writable should be false, got ' + descName.writable);
if (descName.enumerable !== false) throw new Error('FAIL: fn.name.enumerable should be false, got ' + descName.enumerable);
if (descName.configurable !== true) throw new Error('FAIL: fn.name.configurable should be true, got ' + descName.configurable);

var descLen = Object.getOwnPropertyDescriptor(myFn, 'length');
if (!descLen) throw new Error('FAIL: fn.length descriptor is undefined');
if (descLen.writable !== false) throw new Error('FAIL: fn.length.writable should be false, got ' + descLen.writable);
if (descLen.enumerable !== false) throw new Error('FAIL: fn.length.enumerable should be false, got ' + descLen.enumerable);
if (descLen.configurable !== true) throw new Error('FAIL: fn.length.configurable should be true, got ' + descLen.configurable);

// Test: built-in function (Array.isArray) name/length descriptors
var descBuiltinLen = Object.getOwnPropertyDescriptor(Array.isArray, 'length');
if (!descBuiltinLen) throw new Error('FAIL: Array.isArray.length descriptor is undefined');
if (descBuiltinLen.writable !== false) throw new Error('FAIL: Array.isArray.length.writable should be false');

throw new Error('PASS');
```

- [ ] **Step 2: Confirm test fails**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p37_t5.js 2>&1 | grep "Error:"
```
Expected: `Error: FAIL: fn.name.writable should be false` (or `fn.length.writable`).

- [ ] **Step 3: Implement fix**

In `src/FunctionPrototype.cpp`, find `wrapNativeFunction` lines 252-255:
```cpp
    const proto::ProtoObject* rawMethod = ctx->fromMethod(nullptr, fn);
    if (nfKey && rawMethod) wrapper = wrapper->setAttribute(ctx, nfKey, rawMethod);
    if (lenKey) wrapper = wrapper->setAttribute(ctx, lenKey, ctx->fromInteger(length));
    if (nmKey)  wrapper = wrapper->setAttribute(ctx, nmKey,  ctx->fromUTF8String(name ? name : ""));
```

Replace with:
```cpp
    const proto::ProtoObject* rawMethod = ctx->fromMethod(nullptr, fn);
    if (nfKey && rawMethod) wrapper = wrapper->setAttribute(ctx, nfKey, rawMethod);

    if (lenKey) {
        wrapper = wrapper->setAttribute(ctx, lenKey, ctx->fromInteger(length));
        // length: {writable: false, enumerable: false, configurable: true} → bits = 0x2
        const proto::ProtoObject* pdlko = ctx->fromUTF8String("__pd_length__");
        const proto::ProtoString* pdlk = pdlko ? pdlko->asString(ctx) : nullptr;
        if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2));
    }

    if (nmKey) {
        wrapper = wrapper->setAttribute(ctx, nmKey, ctx->fromUTF8String(name ? name : ""));
        // name: {writable: false, enumerable: false, configurable: true} → bits = 0x2
        const proto::ProtoObject* pdnko = ctx->fromUTF8String("__pd_name__");
        const proto::ProtoString* pdnk = pdnko ? pdnko->asString(ctx) : nullptr;
        if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2));
    }
```

- [ ] **Step 4: Rebuild and run test**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3 && \
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_p37_t5.js 2>&1 | grep "Error:"
```
Expected: `Error: PASS`

- [ ] **Step 5: Run targeted test262**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
TEST262_PATTERNS="built-ins/Function" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```
Expected: improvement over 209/509 baseline.

- [ ] **Step 6: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add src/FunctionPrototype.cpp && \
git commit -m "fix: wrapNativeFunction — add non-writable/non-enumerable/configurable descriptors to name and length"
```

---

## Task 6: Run full test262 sweep and update TEST262_STATUS.md

**Files:**
- Modify: `docs/TEST262_STATUS.md`

- [ ] **Step 1: Run all affected suites**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
TEST262_PATTERNS="built-ins/Number" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
TEST262_PATTERNS="built-ins/Function" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
TEST262_PATTERNS="language/expressions" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```

Record passed/total from each run.

- [ ] **Step 2: Update `docs/TEST262_STATUS.md`**

Read the file and add a Phase 37 entry following the same format as Phase 36. Add it as the most recent entry. Include the actual numbers from Step 1 and the delta from baseline.

Expected Phase 37 targets:
| Suite | Baseline | Target |
|-------|---------|--------|
| `built-ins/Number` | 68/338 (20.1%) | ≥ 100/338 (≥29.6%) |
| `built-ins/Function` | 209/509 (41.1%) | ≥ 255/509 (≥50.1%) |
| `language/expressions` | 9418/11036 | ≥ 9418/11036 (no regression) |

- [ ] **Step 3: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS && \
git add docs/TEST262_STATUS.md && \
git commit -m "docs: Phase 37 snapshot — Number/Function descriptors, Number toString RangeError, Function toString name"
```

---

## Self-Review

**Spec coverage:**
- [x] Fix 1a (radix RangeError) → Task 1
- [x] Fix 1b (Number constant descriptors 0x0) → Task 2
- [x] Fix 1c (Number.prototype method descriptors 0x3 via installNonEnumerableMethod) → Task 3
- [x] Fix 2a (Function.prototype.toString name extraction + TypeError guard) → Task 4
- [x] Fix 2b (wrapNativeFunction name/length descriptors 0x2) → Task 5
- [x] test262 sweep + TEST262_STATUS.md update → Task 6

**Placeholder scan:** No TBDs, no "similar to Task N" shortcuts, no vague steps. All code blocks show exact old/new text.

**Type consistency:** `proto::ProtoContext* ctx`, `proto::ProtoString*`, `proto::ProtoObject*` used consistently throughout. `JSSymbols::name(ctx)` used in both Task 4 and Task 5.
