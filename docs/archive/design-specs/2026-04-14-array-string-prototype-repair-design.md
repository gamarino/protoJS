# Array + String Prototype Repair — Design Spec (Phase 39)

**Goal:** Recover ~870–1160 test262 tests by fixing seven root-cause bugs across `ArrayPrototype.cpp` and `StringPrototype.cpp`.

**Architecture:** Two independent tracks. Track 1 (Array) fixes `arrLen`/`arrGet` prototype-chain lookup, `arrSet` length mutation on non-arrays, `@@toStringTag` on `arguments`, and ToObject coercion for primitive `this`. Track 2 (String) fixes String wrapper object method dispatch, `split` separator coercion, and missing `.length` on String.prototype methods.

**Tech Stack:** C++20, protoCore immutable objects, `JSSymbols`, `__pd_<name>__` sidecar descriptor system (bit0=writable 0x1, bit1=configurable 0x2, bit2=enumerable 0x4). Build: `cmake --build build --target protojs -j$(nproc)`.

---

## Baselines (measured 2026-04-14, Phase 38)

| Suite | Passed | Total | Pass Rate |
|-------|--------|-------|-----------|
| `built-ins/Array` | 1,543 | 3,081 | 50.1% |
| `built-ins/String` | 514 | 1,223 | 42.0% |
| `language/expressions` | 9,422 | 11,036 | 85.4% |

---

## Track 1: Array Fixes

### Fix A — `arrLen`/`arrGet` must walk the prototype chain

**File:** `src/ArrayPrototype.cpp`, lines 38 and 70

**Problem:** `arrLen` reads `length` with `getAttribute(ctx, key, false)` (own properties only). `arrGet` reads indexed elements also with `false`. When `Array.prototype.map.call(obj, fn)` is called with an array-like `obj` whose `length` or elements live on its prototype, `arrLen` returns 0, the callback never fires, and the result is an empty array.

**Current code:**
```cpp
// arrLen — line 38:
const proto::ProtoObject* lenObj = arr->getAttribute(ctx, key, false);

// arrGet — line 70:
const proto::ProtoObject* val = arr->getAttribute(ctx, key, false);
```

**Fix:**
```cpp
// arrLen — line 38:
const proto::ProtoObject* lenObj = arr->getAttribute(ctx, key, true);

// arrGet — line 70:
const proto::ProtoObject* val = arr->getAttribute(ctx, key, true);
```

**Expected recovery:** ~300–400 tests (`reduce`, `reduceRight`, `filter`, `map`, `forEach`, `some`, `every`, `indexOf`, `lastIndexOf`, and more).

---

### Fix B — `arrSet` must not auto-bump `length` on non-array objects

**File:** `src/ArrayPrototype.cpp`, lines 85–91

**Problem:** `arrSet` unconditionally updates `length` when `idx + 1 > current length`. When array methods are called on a plain object whose `length` is controlled by a prototype accessor, `arrSet` creates an own `length` property that shadows the getter. Subsequent reads of `length` see the wrong value.

**Current code (lines 85–91):**
```cpp
unsigned long curLen = arrLen(ctx, arr);
if (idx + 1 > curLen) {
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey)
        arr = arr->setAttribute(ctx, lenKey,
                                ctx->fromInteger(static_cast<long long>(idx + 1)));
}
```

**Fix:** Only bump `length` when `arr` carries the `__is_array__` marker (set at line 1599 on the array prototype). The check:
```cpp
unsigned long curLen = arrLen(ctx, arr);
if (idx + 1 > curLen) {
    // Only bump length on real arrays, not on array-like plain objects.
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    bool isNativeArr = isArrKey &&
        arr->getAttribute(ctx, isArrKey, true) == PROTO_TRUE;
    if (isNativeArr) {
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey)
            arr = arr->setAttribute(ctx, lenKey,
                                    ctx->fromInteger(static_cast<long long>(idx + 1)));
    }
}
```

**Expected recovery:** ~100–150 tests.

---

### Fix C — `arguments` object needs `@@toStringTag = "Arguments"`

**File:** `src/runtime/ProtoInterpreter.cpp` — wherever the `arguments` object is created for function calls

**Problem:** `Object.prototype.toString.call(arguments)` returns `[object Object]` instead of `[object Arguments]`. ECMAScript requires `[object Arguments]` for the arguments exotic object. Note: `Math` already has `__toStringTag__ = "Math"` (MathBuiltin.cpp lines 272–277). `JSON` is stubbed as `PROTO_NONE` and is out of scope.

**Fix:** When creating the `arguments` object in the interpreter (search for where the arguments array-like is built for each function call frame), add:
```cpp
const proto::ProtoString* toStringTagKey = JSSymbols::toStringTag(ctx);
if (toStringTagKey)
    argsObj = argsObj->setAttribute(ctx, toStringTagKey,
        ctx->fromUTF8String("Arguments"));
```

`JSSymbols::toStringTag(ctx)` returns the interned symbol for `"__toStringTag__"`. `objectToString` in `ObjectPrototype.cpp` checks this key (and also `"Symbol.toStringTag"`) when building `[object X]` strings.

**Expected recovery:** ~50–70 tests.

---

### Fix D — ToObject coercion for primitive `this` in Array methods

**File:** `src/ArrayPrototype.cpp` — `arrayThrowIfNullUndefined` helper or at the start of each method

**Problem:** `Array.prototype.reduce.call(false, cb, 1)` where `Boolean.prototype[0] = true; Boolean.prototype.length = 1` should work — ECMAScript requires ToObject coercion before treating the receiver as an array-like. The current `arrayThrowIfNullUndefined` guard only rejects null/undefined; it does not wrap primitives to their boxing type.

**Fix:** Add a `toObjectCoerce` step before calling `arrLen`. Add a helper in the anonymous namespace:
```cpp
// Returns an object-typed handle for `val`. For primitive strings, numbers,
// booleans, wraps them in an object child of the appropriate prototype so that
// prototype-inherited indexed properties (Boolean.prototype[0]) are visible.
static const proto::ProtoObject* toObject(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* val,
                                           const proto::ProtoObject** globalRoot) {
    if (!val || val == PROTO_NONE) return PROTO_NONE; // null/undefined — handled by caller
    if (val->isInteger(ctx) || val->isDouble(ctx) || val->isFloat(ctx)) {
        // Number primitive — wrap in Number prototype child
        const proto::ProtoString* npKey = JSSymbols::numberProto(ctx);
        if (npKey && globalRoot && *globalRoot) {
            const proto::ProtoObject* np = (*globalRoot)->getAttribute(ctx, npKey, false);
            if (np && np != PROTO_NONE) {
                const proto::ProtoObject* wrapped = np->newChild(ctx, true);
                const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
                if (pvKey && wrapped) wrapped = wrapped->setAttribute(ctx, pvKey, val);
                return wrapped ? wrapped : val;
            }
        }
        return val; // fallback: use primitive as-is
    }
    if (val->isBoolean(ctx)) {
        // Boolean primitive — wrap in Boolean prototype child
        // (Same pattern as Number above, using JSSymbols::booleanProto)
        return val; // fallback if prototype unavailable
    }
    if (val->isString(ctx)) {
        // String primitive — wrap in String prototype child
        return val; // string primitives already work via stringPrototype method dispatch
    }
    return val; // already an object
}
```

Apply at the top of each array method (after `arrayThrowIfNullUndefined`, before `arrLen`):
```cpp
self = toObject(ctx, self, globalRoot); // ToObject coercion per ES spec
```

**Note:** `globalRoot` must be accessible. Check whether the array methods already receive it; if not, use `ctx->space` to access prototypes directly.

**Expected recovery:** ~40–60 tests.

---

## Track 2: String Fixes

### Fix E — String methods must be accessible and callable on String wrapper objects

**File:** `src/StringPrototype.cpp`

**Problem:** `new String("hello").split("e")` may fail. Root cause needs implementation-time verification. The `reg` lambda (line 1074) installs methods as raw `ProtoMethod` cells via `ctx->fromMethod(mp, fn)` where `mp` is the initial (empty) string prototype. `requireStringThis` (line 173) does NOT reject wrapper objects. `objToStr` (line 26) correctly unwraps `__primitive_value__` via `getAttribute(false)` (which is correct since `__primitive_value__` IS an own property of wrapper objects created in the interpreter).

**Verification step:** The implementer must write this test first:
```javascript
var s = new String("hello");
console.log(typeof s.split);          // expected: "function"
console.log(s.split("l").length);     // expected: 3
console.log(s.split.length);          // expected: 2 (after Fix G)
```
If `typeof s.split` is not `"function"`, the issue is that the interpreter does not recognize raw `ProtoMethod` cells as callable when looked up via prototype chain on a non-primitive object. In that case, the fix is to replace the `reg` lambda with `installNonEnumerableMethod` (from `src/PrototypeUtils.h`), which creates proper wrapper objects recognized as callable by the interpreter.

**Note on `installNonEnumerableMethod`:** It creates wrapper objects that inherit from `ctx->space->methodPrototype`. Since `BuildStringPrototype` runs during space initialization (before `ensureFunctionPrototype` sets `methodPrototype`), a deferred patching approach may be needed — either delay `BuildStringPrototype` registration until after `ensureFunctionPrototype`, or use `ensureStringConstructor` (which runs later with a `globalRoot`) to re-register methods as proper wrappers.

**If `typeof s.split === "function"` but calling fails:** Investigate whether `stringSplit` is called with the correct `self` and check what `requireStringThis` + `objToStr` do at runtime (add a minimal test).

**Expected recovery:** ~300–400 tests.

---

### Fix F — `stringSplit` must apply `ToString(separator)` for non-string separators

**File:** `src/StringPrototype.cpp`, line 929

**Problem:** When the separator is not a string (`null`, `0`, `false`, a number), line 929 returns `PROTO_NONE` instead of converting the separator to a string and splitting. ECMAScript requires `ToString(separator)` coercion. `"gnulluna".split(null)` should produce `["g", "una"]` (splitting on `"null"`).

**Current code (line 929):**
```cpp
// Non-string separator (e.g., regex): return PROTO_NONE to preserve vacuous-pass
if (!sepArg->isString(ctx)) return PROTO_NONE;
```

**Fix:** Replace the early return with `ToString` coercion. `objToStr` (line 26) already handles all non-string primitives correctly (numbers, booleans, etc.):
```cpp
// Non-regexp, non-string separator: convert to string per ECMAScript ToString rule.
// (RegExp case is handled above via Symbol.split. Null/undefined are handled
// as "null"/"undefined" strings.)
std::string sepStr = objToStr(ctx, sepArg);
// Use `sepStr` as separator; fall through to the string-split logic below.
std::string sep = sepStr;
```

The existing `std::string sep = objToStr(ctx, sepArg)` on line 931 must be removed (or kept as the definition of `sep`). After the fix, the code continues with the normal string-split path using `sep`.

**Expected recovery:** ~30–40 tests.

---

### Fix G — `String.prototype` methods must have correct `.length` property

**File:** `src/StringPrototype.cpp`, line 1079

**Problem:** The `reg` lambda discards the `length` parameter with `(void)length;`. As a result `String.prototype.trim.length === undefined`, `String.prototype.split.length === undefined`, etc. ECMAScript requires each built-in method to have a non-writable, non-enumerable, configurable `.length` equal to its declared arity.

**Current code (lines 1074–1080):**
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

**Fix:** Store `length` and its descriptor on the method object. Since `mObj` is a `ProtoMethod` (immutable), we must also attach the `length` attribute to `sp` using a compound key — or, if Fix E upgrades to wrapper objects, use the wrapper's own `length`. If still using raw method cells:
```cpp
auto reg = [&](const char* name, proto::ProtoMethod fn, long long length) {
    const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
    if (!key) return;
    const proto::ProtoObject* mObj = ctx->fromMethod(mp, fn);

    // Store method.length as a sidecar on sp, keyed as "__len_<name>__".
    // This is a workaround for raw METHOD cells which cannot carry attributes.
    // When Fix E upgrades to wrapper objects, this workaround can be removed.
    sp = sp->setAttribute(ctx, key, mObj);

    // Set length on the method object — only works for wrapper objects (Fix E).
    // For now, attach length directly to mObj if it is mutable, or use a sidecar.
    // TODO: revisit when Fix E determines whether wrappers are used.
    (void)length;
};
```

**Preferred approach (if Fix E uses wrapper objects via `installNonEnumerableMethod`):**
`installNonEnumerableMethod(ctx, sp, name, fn, length)` already sets `fn.length` with descriptor `0x2` and `fn.name` with descriptor `0x2`. In that case Fix G is subsumed by Fix E.

**Fallback (if raw method cells remain):** In `ensureStringConstructor` (line 1126), after the prototype is already attached to the constructor, iterate all method names and set `String.prototype.<name>.length` via a post-init patch:
```cpp
// Patch String.prototype method .length properties after the fact.
static const struct { const char* name; long long len; } kMethodLengths[] = {
    {"valueOf",0},{"toString",0},{"charAt",1},{"charCodeAt",1},{"codePointAt",1},
    {"at",1},{"concat",1},{"indexOf",1},{"lastIndexOf",1},{"slice",2},
    {"substring",2},{"substr",2},{"toLowerCase",0},{"toUpperCase",0},
    {"toLocaleLowerCase",0},{"toLocaleUpperCase",0},{"repeat",1},
    {"localeCompare",1},{"trim",0},{"trimStart",0},{"trimLeft",0},
    {"trimEnd",0},{"trimRight",0},{"startsWith",1},{"endsWith",1},
    {"includes",1},{"padStart",1},{"padEnd",1},{"match",1},{"search",1},
    {"replace",2},{"replaceAll",2},{"split",2},{"normalize",0},
    {"isWellFormed",0},{"matchAll",1},{nullptr,0}
};
```
This approach is a fallback only — Fix E's wrapper approach is preferred.

**Expected recovery:** ~50–80 tests (standalone; subsumed by Fix E if wrapper approach is used).

---

## File Structure

| File | Fixes |
|------|-------|
| `src/ArrayPrototype.cpp` | Fix A (arrLen/arrGet true), Fix B (arrSet isArray guard), Fix D (ToObject helper) |
| `src/runtime/ProtoInterpreter.cpp` | Fix C (arguments toStringTag) |
| `src/StringPrototype.cpp` | Fix E (String wrapper method dispatch), Fix F (split separator coercion), Fix G (method .length) |

---

## Test Plan

```bash
# Array
TEST262_PATTERNS="built-ins/Array" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -5

# String
TEST262_PATTERNS="built-ins/String" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -5

# Regression
TEST262_PATTERNS="language/expressions" \
node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```

**Expected Phase 39 targets:**

| Suite | Baseline | Target |
|-------|---------|--------|
| `built-ins/Array` | 1,543/3,081 (50.1%) | ≥ 1,900/3,081 (≥62%) |
| `built-ins/String` | 514/1,223 (42.0%) | ≥ 800/1,223 (≥65%) |
| `language/expressions` | 9,422/11,036 (85.4%) | ≥ 9,422/11,036 (no regression) |
