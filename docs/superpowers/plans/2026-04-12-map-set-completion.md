# Map/Set Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recover ~300 test262 tests by adding non-enumerable property descriptors to Map/Set prototype methods, fixing type guards, normalizing -0 keys, and implementing missing methods (getOrInsert, getOrInsertComputed, groupBy, Symbol.iterator, Symbol.toStringTag, Set collection methods).

**Architecture:** New `PrototypeUtils` helper centralizes non-enumerable method installation. Map and Set are refactored to use it, gaining correct `length`/`name` descriptors. Type guards (`requireMapThis`/`requireSetThis`) are added to all methods. Missing methods are appended to the respective prototype files.

**Tech Stack:** C++20, protoCore ProtoSparseList/ProtoSet, JSSymbols, `__pd_<name>__` sidecar descriptor system (bit 0 = writable, bit 1 = configurable, bit 2 = enumerable).

---

## File Structure

| File | Change |
|------|--------|
| `src/PrototypeUtils.h` | New — declares `installNonEnumerableMethod` |
| `src/PrototypeUtils.cpp` | New — implements `installNonEnumerableMethod` |
| `src/MapPrototype.cpp` | Add guards, fix descriptors, add getOrInsert/getOrInsertComputed/groupBy/Symbol.iterator/Symbol.toStringTag/-0 normalization |
| `src/SetPrototype.cpp` | Add guards, fix descriptors, add 7 collection methods/Symbol.iterator/Symbol.toStringTag/-0 normalization |
| `CMakeLists.txt` | Add `src/PrototypeUtils.cpp` |

---

## Background: Key Concepts

**Descriptor sidecar:** `Object.getOwnPropertyDescriptor` reads `__pd_<name>__` as a packed byte:
- `0x1` = writable
- `0x2` = configurable
- `0x4` = enumerable
- missing sidecar = all true (default)

**Non-enumerable method:** `{writable:true, enumerable:false, configurable:true}` → bits `0x3`

**Non-writable property (length/name on functions):** `{writable:false, enumerable:false, configurable:true}` → bits `0x2`

**Throwing TypeError from native code:**
```cpp
signalNativeException(makeNativeError(ctx, "TypeError", "message"));
return PROTO_NONE;
```

**Installing method as non-enumerable (pattern used by `installNonEnumerableMethod`):**
```cpp
// Create mutable wrapper inheriting from methodPrototype
const proto::ProtoObject* parent = ctx->space->methodPrototype;
const proto::ProtoObject* methodObj = parent->newChild(ctx, true);
// Store raw function
const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
methodObj = methodObj->setAttribute(ctx, nfKey, ctx->fromMethod(nullptr, fn));
// Set length with descriptor
const proto::ProtoString* lenKey = JSSymbols::length(ctx);
methodObj = methodObj->setAttribute(ctx, lenKey, ctx->fromInteger(argc));
const proto::ProtoString* pdLen = ctx->fromUTF8String("__pd_length__")->asString(ctx);
methodObj = methodObj->setAttribute(ctx, pdLen, ctx->fromInteger(0x2LL));
// Set name with descriptor
const proto::ProtoString* nmKey = JSSymbols::name(ctx);
methodObj = methodObj->setAttribute(ctx, nmKey, ctx->fromUTF8String(methodName));
const proto::ProtoString* pdNm = ctx->fromUTF8String("__pd_name__")->asString(ctx);
methodObj = methodObj->setAttribute(ctx, pdNm, ctx->fromInteger(0x2LL));
// Install on prototype as non-enumerable
proto = proto->setAttribute(ctx, methodKey, methodObj);
proto = proto->setAttribute(ctx, pdMethodKey, ctx->fromInteger(0x3LL));
```

---

## Task 1: PrototypeUtils helper

**Files:**
- Create: `src/PrototypeUtils.h`
- Create: `src/PrototypeUtils.cpp`
- Modify: `CMakeLists.txt` (line 114, after `src/BooleanPrototype.cpp`)

- [ ] **Step 1: Write a quick smoke test**

```bash
cat > /tmp/test_proto_utils.js << 'EOF'
// After the fix: Map.prototype.has should have correct descriptors
var d = Object.getOwnPropertyDescriptor(Map.prototype, 'has');
if (d.enumerable !== false) throw new Error('enumerable should be false, got: ' + d.enumerable);
if (d.writable !== true) throw new Error('writable should be true');
if (d.configurable !== true) throw new Error('configurable should be true');
var ld = Object.getOwnPropertyDescriptor(Map.prototype.has, 'length');
if (ld.value !== 1) throw new Error('length.value should be 1, got: ' + ld.value);
if (ld.enumerable !== false) throw new Error('length.enumerable should be false');
if (ld.writable !== false) throw new Error('length.writable should be false');
console.log('PASS: property descriptors correct');
EOF
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_proto_utils.js 2>/dev/null
```
Expected: `Exception` or wrong descriptor (test confirms the current broken state)

- [ ] **Step 2: Create `src/PrototypeUtils.h`**

```cpp
#ifndef PROTOJS_PROTOTYPEUTILS_H
#define PROTOJS_PROTOTYPEUTILS_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Install fn as a non-enumerable, configurable, writable method on proto.
 *
 * Creates a mutable wrapper object (child of methodPrototype) carrying:
 *   __native_fn__  → the raw ProtoMethod pointer
 *   length         → argc  (descriptor: writable=false, enumerable=false, configurable=true)
 *   name           → methodName (same descriptor)
 *
 * Then installs the wrapper on proto under key methodName with descriptor:
 *   writable=true, enumerable=false, configurable=true
 *
 * Returns the updated proto pointer (must be captured by caller).
 */
const proto::ProtoObject* installNonEnumerableMethod(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proto,
    const char* methodName,
    proto::ProtoMethod fn,
    int argc);

} // namespace protojs

#endif // PROTOJS_PROTOTYPEUTILS_H
```

- [ ] **Step 3: Create `src/PrototypeUtils.cpp`**

```cpp
#include "PrototypeUtils.h"
#include "JSSymbols.h"
#include <string>

namespace protojs {

const proto::ProtoObject* installNonEnumerableMethod(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proto,
    const char* methodName,
    proto::ProtoMethod fn,
    int argc)
{
    if (!ctx || !proto || !methodName || !fn) return proto;

    // Create a mutable wrapper inheriting from methodPrototype so that
    // .call/.apply/.bind resolve via prototype chain.
    const proto::ProtoObject* parent = (ctx->space && ctx->space->methodPrototype)
        ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* methodObj = parent
        ? parent->newChild(ctx, true)
        : ctx->newObject(true);
    if (!methodObj) return proto;

    // Store raw ProtoMethod as __native_fn__ (dispatch checks this).
    const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
    if (nfKey) {
        const proto::ProtoObject* rawMethod = ctx->fromMethod(nullptr, fn);
        if (rawMethod) methodObj = methodObj->setAttribute(ctx, nfKey, rawMethod);
    }

    // Set length: {value: argc, writable: false, enumerable: false, configurable: true}
    // bits = 0x2 (configurable=true, writable=false, enumerable=false)
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey) {
        methodObj = methodObj->setAttribute(ctx, lenKey,
                                            ctx->fromInteger(static_cast<long long>(argc)));
        const proto::ProtoObject* pdLenObj = ctx->fromUTF8String("__pd_length__");
        const proto::ProtoString* pdLen = pdLenObj ? pdLenObj->asString(ctx) : nullptr;
        if (pdLen) methodObj = methodObj->setAttribute(ctx, pdLen, ctx->fromInteger(0x2LL));
    }

    // Set name: {value: methodName, writable: false, enumerable: false, configurable: true}
    const proto::ProtoString* nmKey = JSSymbols::name(ctx);
    if (nmKey) {
        methodObj = methodObj->setAttribute(ctx, nmKey, ctx->fromUTF8String(methodName));
        const proto::ProtoObject* pdNmObj = ctx->fromUTF8String("__pd_name__");
        const proto::ProtoString* pdNm = pdNmObj ? pdNmObj->asString(ctx) : nullptr;
        if (pdNm) methodObj = methodObj->setAttribute(ctx, pdNm, ctx->fromInteger(0x2LL));
    }

    // Install on proto: {writable: true, enumerable: false, configurable: true}
    // bits = 0x3 (configurable=true, writable=true, enumerable=false)
    const proto::ProtoObject* mko = ctx->fromUTF8String(methodName);
    const proto::ProtoString* mk = mko ? mko->asString(ctx) : nullptr;
    if (mk) {
        proto = proto->setAttribute(ctx, mk, methodObj);
        std::string pdStr = std::string("__pd_") + methodName + "__";
        const proto::ProtoObject* pdko = ctx->fromUTF8String(pdStr.c_str());
        const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
        if (pdks) proto = proto->setAttribute(ctx, pdks, ctx->fromInteger(0x3LL));
    }

    return proto;
}

} // namespace protojs
```

- [ ] **Step 4: Add `src/PrototypeUtils.cpp` to CMakeLists.txt**

In `CMakeLists.txt`, find the line `src/BooleanPrototype.cpp` (line 114) and add after it:

```cmake
    src/PrototypeUtils.cpp
```

The block should look like:
```cmake
    src/BooleanPrototype.cpp
    src/PrototypeUtils.cpp
    src/MapPrototype.cpp
    src/SetPrototype.cpp
```

- [ ] **Step 5: Build to verify PrototypeUtils compiles**

```bash
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -5
```
Expected: `[100%] Built target protojs` (no errors)

- [ ] **Step 6: Run smoke test to confirm still broken (baseline)**

```bash
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_proto_utils.js 2>/dev/null
```
Expected: Still reports wrong descriptor (Map not yet updated)

- [ ] **Step 7: Commit PrototypeUtils**

```bash
git add src/PrototypeUtils.h src/PrototypeUtils.cpp CMakeLists.txt
git commit -m "feat(phase35): add installNonEnumerableMethod helper in PrototypeUtils"
```

---

## Task 2: Map — guards, descriptor fix, -0 normalization, Symbol properties

**Files:**
- Modify: `src/MapPrototype.cpp`

This task replaces the `installMethod` lambda in `BuildMapPrototype` with `installNonEnumerableMethod`, adds type guards to every method, adds -0 key normalization, and installs `Symbol.iterator` and `Symbol.toStringTag`.

- [ ] **Step 1: Add normalizeMapKey and requireMapThis helpers**

In `src/MapPrototype.cpp`, inside the anonymous namespace, add after `sameValueZero` (around line 68):

```cpp
// Normalize -0 to +0 per SameValueZero spec (Map keys use +0 for -0).
static const proto::ProtoObject* normalizeMapKey(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* key)
{
    if (key && (key->isDouble(ctx) || key->isFloat(ctx))) {
        double d = key->asDouble(ctx);
        if (d == 0.0 && std::signbit(d))
            return ctx->fromInteger(0LL);
    }
    return key;
}

// Returns true if self is a valid Map receiver (has __map_keys__ slot).
// Signals TypeError and returns false otherwise.
static bool requireMapThis(proto::ProtoContext* ctx, const proto::ProtoObject* self)
{
    if (!self || self == PROTO_NONE || self == PROTO_TRUE || self == PROTO_FALSE ||
        self->isInteger(ctx) || self->isDouble(ctx) || self->isFloat(ctx) ||
        self->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Map operation called on non-Map"));
        return false;
    }
    const proto::ProtoObject* ko = ctx->fromUTF8String("__map_keys__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    const proto::ProtoObject* v  = ks ? self->getAttribute(ctx, ks, false) : nullptr;
    if (!v || v == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Map operation called on non-Map"));
        return false;
    }
    return true;
}
```

Also add the include at the top of the file (after existing includes):
```cpp
#include "PrototypeUtils.h"
#include "runtime/ProtoInterpreter.h"
```

(Note: `ProtoInterpreter.h` is likely already included; just ensure `PrototypeUtils.h` is added.)

- [ ] **Step 2: Add requireMapThis guard to every Map method**

In each of `mapSet`, `mapGet`, `mapHas`, `mapDelete`, `mapClear`, `mapForEach`, `mapKeys`, `mapValues`, `mapEntries`, replace the first guard line:

**Before (example in mapHas):**
```cpp
if (!self || self == PROTO_NONE) return PROTO_FALSE;
```

**After:**
```cpp
if (!requireMapThis(ctx, self)) return PROTO_NONE;
```

Do this for ALL nine methods: `mapSet`, `mapGet`, `mapHas`, `mapDelete`, `mapClear`, `mapForEach`, `mapSizeGetter`, `mapKeys`, `mapValues`, `mapEntries`.

For `mapSizeGetter`, replace:
```cpp
if (!self || self == PROTO_NONE) return ctx->fromInteger(0LL);
```
with:
```cpp
if (!requireMapThis(ctx, self)) return PROTO_NONE;
```

- [ ] **Step 3: Add -0 normalization in mapSet and mapFind**

In `mapFind` (around line 129), add normalization right after the function opens:
```cpp
static bool mapFind(proto::ProtoContext* ctx,
                    const proto::ProtoObject* mapObj,
                    const proto::ProtoObject* key,
                    unsigned long& foundIdx)
{
    key = normalizeMapKey(ctx, key);   // ← ADD THIS LINE
    const proto::ProtoSparseList* keysList = getMapList(ctx, mapObj, "__map_keys__");
    // ... rest unchanged
```

In `mapSet` (around line 161), add normalization after extracting `key`:
```cpp
    const proto::ProtoObject* key = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* val = (argc > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!key) key = PROTO_NONE;
    if (!val) val = PROTO_NONE;
    key = normalizeMapKey(ctx, key);   // ← ADD THIS LINE
```

- [ ] **Step 4: Replace installMethod lambda in BuildMapPrototype**

In `BuildMapPrototype`, replace the entire `auto installMethod = [&](...) { ... };` lambda and all `installMethod(...)` calls with `installNonEnumerableMethod` calls.

Replace from `auto installMethod = [&]` through `installMethod("entries", mapEntries);` with:

```cpp
    auto install = [&](const char* n, proto::ProtoMethod fn, int argc) {
        setProto = installNonEnumerableMethod(ctx, setProto, n, fn, argc);
    };
    // Wait — this is MapPrototype, variable name is mapProto, not setProto.
```

Correction — in MapPrototype.cpp the variable is `mapProto`. Replace with:

```cpp
    mapProto = installNonEnumerableMethod(ctx, mapProto, "set",     mapSet,     2);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "get",     mapGet,     1);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "has",     mapHas,     1);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "delete",  mapDelete,  1);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "clear",   mapClear,   0);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "forEach", mapForEach, 1);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "keys",    mapKeys,    0);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "values",  mapValues,  0);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "entries", mapEntries, 0);
```

- [ ] **Step 5: Add Symbol.iterator and Symbol.toStringTag to Map prototype**

After the `installNonEnumerableMethod` calls, and after the `__get_size__` block, add:

```cpp
    // Symbol.iterator = entries (Map iterates as [key, value] pairs)
    // JSSymbols::symbolIterator returns "Symbol.iterator"; sidecar key = "__pd_Symbol.iterator__"
    {
        const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(ctx);
        if (symIterKey) {
            const proto::ProtoString* entriesKey = ctx->fromUTF8String("entries")->asString(ctx);
            const proto::ProtoObject* entriesFn = entriesKey
                ? mapProto->getAttribute(ctx, entriesKey, false) : nullptr;
            if (entriesFn && entriesFn != PROTO_NONE) {
                mapProto = mapProto->setAttribute(ctx, symIterKey, entriesFn);
                const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_Symbol.iterator__");
                const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
                if (pdks) mapProto = mapProto->setAttribute(ctx, pdks, ctx->fromInteger(0x3LL));
            }
        }
    }

    // Symbol.toStringTag = "Map": {writable:false, enumerable:false, configurable:true} = 0x2
    // JSSymbols::toStringTag returns "__toStringTag__"; sidecar key = "__pd___toStringTag____"
    {
        const proto::ProtoString* tstKey = JSSymbols::toStringTag(ctx);
        if (tstKey) {
            mapProto = mapProto->setAttribute(ctx, tstKey, ctx->fromUTF8String("Map"));
            const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd___toStringTag____");
            const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
            if (pdks) mapProto = mapProto->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
        }
    }
```

**NOTE:** `JSSymbols::toStringTag` returns the `ProtoString*` for `"__toStringTag__"` (the internal key). Verify by checking JSSymbols.cpp that `toStringTag` returns the string `"__toStringTag__"`. The `Symbol.toStringTag` check in JS checks `obj[Symbol.toStringTag]` which the runtime maps to `__toStringTag__`.

- [ ] **Step 6: Build and run smoke test**

```bash
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_proto_utils.js 2>/dev/null
```
Expected:
```
[100%] Built target protojs
PASS: property descriptors correct
```

- [ ] **Step 7: Run Map test suite**

```bash
TEST262_PATTERNS="built-ins/Map" node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```
Expected: passed count significantly higher than 50 (target ≥120)

- [ ] **Step 8: Commit Map descriptor + guard fixes**

```bash
git add src/MapPrototype.cpp
git commit -m "feat(phase35): Map — non-enumerable descriptors, type guards, -0 normalization, Symbol.iterator/toStringTag"
```

---

## Task 3: Map — getOrInsert, getOrInsertComputed, groupBy

**Files:**
- Modify: `src/MapPrototype.cpp`

- [ ] **Step 1: Add getOrInsert and getOrInsertComputed implementations**

In `src/MapPrototype.cpp`, inside the anonymous namespace, add after `mapEntries`:

```cpp
// ---------------------------------------------------------------------------
// map.getOrInsert(key, defaultValue) → value
// Returns existing value for key; if absent, inserts defaultValue and returns it.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapGetOrInsert(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    const proto::ProtoObject* key = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* defVal = (argc > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!key) key = PROTO_NONE;
    if (!defVal) defVal = PROTO_NONE;
    key = normalizeMapKey(ctx, key);

    unsigned long foundIdx = 0;
    if (mapFind(ctx, self, key, foundIdx)) {
        const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
        if (!valsList) return PROTO_NONE;
        const proto::ProtoObject* v = valsList->has(ctx, foundIdx)
            ? valsList->getAt(ctx, foundIdx) : PROTO_NONE;
        return v ? v : PROTO_NONE;
    }
    // Insert new entry.
    const proto::ProtoSparseList* kl = getMapList(ctx, self, "__map_keys__");
    const proto::ProtoSparseList* vl = getMapList(ctx, self, "__map_vals__");
    const proto::ProtoSparseList* hl = getMapList(ctx, self, "__map_hash__");
    if (!kl || !vl || !hl) return PROTO_NONE;
    long sz = getMapSize(ctx, self);
    unsigned long ni = static_cast<unsigned long>(sz);
    kl = kl->setAt(ctx, ni, key);
    vl = vl->setAt(ctx, ni, defVal);
    unsigned long h = szvHash(ctx, key);
    if (!hl->has(ctx, h))
        hl = hl->setAt(ctx, h, ctx->fromInteger(static_cast<long long>(ni)));
    setMapListInPlace(ctx, self, "__map_keys__", kl);
    setMapListInPlace(ctx, self, "__map_vals__", vl);
    setMapListInPlace(ctx, self, "__map_hash__", hl);
    setMapSizeInPlace(ctx, self, sz + 1);
    return defVal;
}

// ---------------------------------------------------------------------------
// map.getOrInsertComputed(key, callbackFn) → value
// Returns existing value; if absent, calls callbackFn() for the default and inserts it.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapGetOrInsertComputed(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    const proto::ProtoObject* key = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* callbackFn = (argc > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!key) key = PROTO_NONE;
    key = normalizeMapKey(ctx, key);

    unsigned long foundIdx = 0;
    if (mapFind(ctx, self, key, foundIdx)) {
        const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
        if (!valsList) return PROTO_NONE;
        const proto::ProtoObject* v = valsList->has(ctx, foundIdx)
            ? valsList->getAt(ctx, foundIdx) : PROTO_NONE;
        return v ? v : PROTO_NONE;
    }
    // Call callback() to compute default value.
    if (!callbackFn || callbackFn == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoList* cbArgs = ctx->newList();
    const proto::ProtoObject* defVal = callJSFunction(ctx, callbackFn, PROTO_NONE, cbArgs);
    if (hasCallException()) return PROTO_NONE;
    if (!defVal) defVal = PROTO_NONE;
    // Insert new entry.
    const proto::ProtoSparseList* kl = getMapList(ctx, self, "__map_keys__");
    const proto::ProtoSparseList* vl = getMapList(ctx, self, "__map_vals__");
    const proto::ProtoSparseList* hl = getMapList(ctx, self, "__map_hash__");
    if (!kl || !vl || !hl) return PROTO_NONE;
    long sz = getMapSize(ctx, self);
    unsigned long ni = static_cast<unsigned long>(sz);
    kl = kl->setAt(ctx, ni, key);
    vl = vl->setAt(ctx, ni, defVal);
    unsigned long h = szvHash(ctx, key);
    if (!hl->has(ctx, h))
        hl = hl->setAt(ctx, h, ctx->fromInteger(static_cast<long long>(ni)));
    setMapListInPlace(ctx, self, "__map_keys__", kl);
    setMapListInPlace(ctx, self, "__map_vals__", vl);
    setMapListInPlace(ctx, self, "__map_hash__", hl);
    setMapSizeInPlace(ctx, self, sz + 1);
    return defVal;
}
```

- [ ] **Step 2: Add groupBy static method**

Add after `mapGetOrInsertComputed`, still in the anonymous namespace:

```cpp
// ---------------------------------------------------------------------------
// Map.groupBy(iterable, keyFn) → Map  (static method on constructor)
// Groups elements of iterable by keyFn(element, index); returns a Map of arrays.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapGroupBy(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    const proto::ProtoObject* iterable = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* keyFn    = (argc > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!iterable) iterable = PROTO_NONE;
    if (!keyFn)    keyFn    = PROTO_NONE;

    // Create a result Map.
    const proto::ProtoObject* result = s_mapPrototype
        ? s_mapPrototype->newChild(ctx, true) : ctx->newObject(true);
    if (!result) return PROTO_NONE;
    {
        const proto::ProtoSparseList* empty = ctx->newSparseList();
        setMapListInPlace(ctx, result, "__map_keys__", empty);
        setMapListInPlace(ctx, result, "__map_vals__", empty);
        setMapListInPlace(ctx, result, "__map_hash__", empty);
        setMapSizeInPlace(ctx, result, 0L);
    }

    // Iterate the iterable using array-like length+index protocol.
    if (iterable == PROTO_NONE) return result;
    const proto::ProtoString* lenKs = JSSymbols::length(ctx);
    if (!lenKs) return result;
    const proto::ProtoObject* lenObj = iterable->getAttribute(ctx, lenKs, true);
    if (!lenObj || lenObj == PROTO_NONE || !lenObj->isInteger(ctx)) return result;
    long len = lenObj->asLong(ctx);

    for (long i = 0; i < len; i++) {
        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        if (!ik) continue;
        const proto::ProtoObject* elem = iterable->getAttribute(ctx, ik, true);
        if (!elem) elem = PROTO_NONE;

        // Call keyFn(element, index).
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, elem);
        cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(static_cast<long long>(i)));
        const proto::ProtoObject* groupKey = callJSFunction(ctx, keyFn, PROTO_NONE, cbArgs);
        if (hasCallException()) return PROTO_NONE;
        if (!groupKey) groupKey = PROTO_NONE;
        groupKey = normalizeMapKey(ctx, groupKey);

        // Find or create the group array for this key.
        unsigned long foundIdx = 0;
        if (mapFind(ctx, result, groupKey, foundIdx)) {
            // Append elem to existing array.
            const proto::ProtoSparseList* vl = getMapList(ctx, result, "__map_vals__");
            if (vl && vl->has(ctx, foundIdx)) {
                const proto::ProtoObject* arr = vl->getAt(ctx, foundIdx);
                if (arr && arr != PROTO_NONE) {
                    const proto::ProtoString* lk = JSSymbols::length(ctx);
                    const proto::ProtoObject* arrLen = lk ? arr->getAttribute(ctx, lk, false) : nullptr;
                    long al = (arrLen && arrLen->isInteger(ctx)) ? arrLen->asLong(ctx) : 0L;
                    const proto::ProtoString* newIdx = JSSymbols::indexKey(ctx, static_cast<uint32_t>(al));
                    if (newIdx) arr = arr->setAttribute(ctx, newIdx, elem);
                    if (lk) arr = arr->setAttribute(ctx, lk, ctx->fromInteger(al + 1));
                    setMapListInPlace(ctx, result, "__map_vals__",
                        vl->setAt(ctx, foundIdx, arr));
                }
            }
        } else {
            // Create new array [elem] and insert into result map.
            const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
            const proto::ProtoString* k0  = JSSymbols::indexKey(ctx, 0);
            const proto::ProtoString* lk  = JSSymbols::length(ctx);
            const proto::ProtoString* ia  = JSSymbols::isArray(ctx);
            if (k0) arr = arr->setAttribute(ctx, k0, elem);
            if (lk) arr = arr->setAttribute(ctx, lk, ctx->fromInteger(1LL));
            if (ia) arr = arr->setAttribute(ctx, ia, ctx->fromInteger(1LL));

            const proto::ProtoSparseList* kl = getMapList(ctx, result, "__map_keys__");
            const proto::ProtoSparseList* vl = getMapList(ctx, result, "__map_vals__");
            const proto::ProtoSparseList* hl = getMapList(ctx, result, "__map_hash__");
            if (!kl || !vl || !hl) continue;
            long sz = getMapSize(ctx, result);
            unsigned long ni = static_cast<unsigned long>(sz);
            kl = kl->setAt(ctx, ni, groupKey);
            vl = vl->setAt(ctx, ni, arr);
            unsigned long h = szvHash(ctx, groupKey);
            if (!hl->has(ctx, h))
                hl = hl->setAt(ctx, h, ctx->fromInteger(static_cast<long long>(ni)));
            setMapListInPlace(ctx, result, "__map_keys__", kl);
            setMapListInPlace(ctx, result, "__map_vals__", vl);
            setMapListInPlace(ctx, result, "__map_hash__", hl);
            setMapSizeInPlace(ctx, result, sz + 1);
        }
    }
    return result;
}
```

- [ ] **Step 3: Install getOrInsert, getOrInsertComputed in BuildMapPrototype**

After the existing `installNonEnumerableMethod` calls for the nine methods, add:

```cpp
    mapProto = installNonEnumerableMethod(ctx, mapProto, "getOrInsert",         mapGetOrInsert,         2);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "getOrInsertComputed",  mapGetOrInsertComputed, 2);
```

- [ ] **Step 4: Install groupBy on the Map constructor in ensureMapConstructor**

In `ensureMapConstructor`, after the `ctor = ctor->setAttribute(ctx, protoKey, s_mapPrototype)` line, add:

```cpp
    // Map.groupBy static method
    const proto::ProtoObject* groupByKey = ctx->fromUTF8String("groupBy");
    const proto::ProtoString* groupByKs  = groupByKey ? groupByKey->asString(ctx) : nullptr;
    if (groupByKs) {
        const proto::ProtoObject* groupByFn =
            installNonEnumerableMethod(ctx, ctx->newObject(true), "__tmp__", mapGroupBy, 2);
        // groupByFn is a temp proto — extract the function directly
        const proto::ProtoObject* gbWrapper = ctx->space->methodPrototype
            ? ctx->space->methodPrototype->newChild(ctx, true)
            : ctx->newObject(true);
        const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
        if (nfk) gbWrapper = gbWrapper->setAttribute(ctx, nfk, ctx->fromMethod(nullptr, mapGroupBy));
        const proto::ProtoString* lenk = JSSymbols::length(ctx);
        if (lenk) {
            gbWrapper = gbWrapper->setAttribute(ctx, lenk, ctx->fromInteger(2LL));
            const proto::ProtoObject* pdl = ctx->fromUTF8String("__pd_length__");
            const proto::ProtoString* pdlk = pdl ? pdl->asString(ctx) : nullptr;
            if (pdlk) gbWrapper = gbWrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* nmk = JSSymbols::name(ctx);
        if (nmk) {
            gbWrapper = gbWrapper->setAttribute(ctx, nmk, ctx->fromUTF8String("groupBy"));
            const proto::ProtoObject* pdn = ctx->fromUTF8String("__pd_name__");
            const proto::ProtoString* pdnk = pdn ? pdn->asString(ctx) : nullptr;
            if (pdnk) gbWrapper = gbWrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
        }
        ctor = ctor->setAttribute(ctx, groupByKs, gbWrapper);
        // Non-enumerable on ctor
        const proto::ProtoObject* pdgb = ctx->fromUTF8String("__pd_groupBy__");
        const proto::ProtoString* pdgbk = pdgb ? pdgb->asString(ctx) : nullptr;
        if (pdgbk) ctor = ctor->setAttribute(ctx, pdgbk, ctx->fromInteger(0x3LL));
    }
```

- [ ] **Step 5: Verify with quick test**

```bash
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3
cat > /tmp/test_map_new.js << 'EOF'
var m = new Map([['a',1],['b',2]]);
console.log('getOrInsert a:', m.getOrInsert('a', 99));   // 1 (existing)
console.log('getOrInsert c:', m.getOrInsert('c', 99));   // 99 (inserted)
console.log('size after:', m.size);                      // 3

var g = Map.groupBy([1,2,3,4], function(x){ return x % 2 === 0 ? 'even' : 'odd'; });
console.log('groupBy odd:', g.get('odd').length);    // 2
console.log('groupBy even:', g.get('even').length);  // 2
EOF
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_map_new.js 2>/dev/null
```
Expected:
```
getOrInsert a: 1
getOrInsert c: 99
size after: 3
groupBy odd: 2
groupBy even: 2
```

- [ ] **Step 6: Run Map test suite**

```bash
TEST262_PATTERNS="built-ins/Map" node tests/test262/runner/test262_runner.js 2>&1 | tail -3
```
Expected: summary shows ≥ 130 passed

- [ ] **Step 7: Commit**

```bash
git add src/MapPrototype.cpp
git commit -m "feat(phase35): Map.getOrInsert, getOrInsertComputed, groupBy"
```

---

## Task 4: Set — guards, descriptor fix, -0 normalization, Symbol properties, collection methods

**Files:**
- Modify: `src/SetPrototype.cpp`

- [ ] **Step 1: Add normalizeSetVal and requireSetThis helpers**

In `src/SetPrototype.cpp`, inside the anonymous namespace, add after `setSVZ` (around line 44):

```cpp
// Normalize -0 to +0 per SameValueZero spec.
static const proto::ProtoObject* normalizeSetVal(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* val)
{
    if (val && (val->isDouble(ctx) || val->isFloat(ctx))) {
        double d = val->asDouble(ctx);
        if (d == 0.0 && std::signbit(d))
            return ctx->fromInteger(0LL);
    }
    return val;
}

// Returns true if self is a valid Set receiver (has __set_order__ slot).
// Signals TypeError and returns false otherwise.
static bool requireSetThis(proto::ProtoContext* ctx, const proto::ProtoObject* self)
{
    if (!self || self == PROTO_NONE || self == PROTO_TRUE || self == PROTO_FALSE ||
        self->isInteger(ctx) || self->isDouble(ctx) || self->isFloat(ctx) ||
        self->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set operation called on non-Set"));
        return false;
    }
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_order__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    const proto::ProtoObject* v  = ks ? self->getAttribute(ctx, ks, false) : nullptr;
    if (!v || v == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set operation called on non-Set"));
        return false;
    }
    return true;
}
```

Also add `#include "PrototypeUtils.h"` at the top of `src/SetPrototype.cpp` after the existing includes.

- [ ] **Step 2: Normalize -0 in setAdd and setContains**

In `setAdd` (around line 141), after extracting val:
```cpp
    const proto::ProtoObject* val = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!val) val = PROTO_NONE;
    val = normalizeSetVal(ctx, val);  // ← ADD THIS LINE
```

In `setContains`, add normalization at the top:
```cpp
static bool setContains(proto::ProtoContext* ctx,
                         const proto::ProtoObject* setObj,
                         const proto::ProtoObject* val)
{
    val = normalizeSetVal(ctx, val);  // ← ADD THIS LINE
    const proto::ProtoSet* core = getSetCore(ctx, setObj);
    // ... rest unchanged
```

In `setDeleteFn`, after extracting val:
```cpp
    const proto::ProtoObject* val = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!val) val = PROTO_NONE;
    val = normalizeSetVal(ctx, val);  // ← ADD THIS LINE
```

- [ ] **Step 3: Add requireSetThis guard to every Set method**

In each of `setAdd`, `setHas`, `setDeleteFn`, `setClear`, `setForEach`, `setSizeGetter`, `setValues`, `setKeys`, `setEntries`, replace the first guard line with:

```cpp
if (!requireSetThis(ctx, self)) return PROTO_NONE;
```

For `setHas`, it currently returns PROTO_FALSE on bad this — change to `return PROTO_NONE` after requireSetThis (since the TypeError is signaled). For `setSizeGetter`, change `return ctx->fromInteger(0LL)` to `return PROTO_NONE`.

- [ ] **Step 4: Add makeEmptySet and setAddValue helpers**

In the anonymous namespace, add after `makeSetIterator`:

```cpp
// Create a new empty Set inheriting from s_setPrototype.
static const proto::ProtoObject* makeEmptySet(proto::ProtoContext* ctx)
{
    const proto::ProtoObject* s = s_setPrototype
        ? s_setPrototype->newChild(ctx, true)
        : ctx->newObject(true);
    if (!s) return PROTO_NONE;
    setSetCoreInPlace(ctx, s, ctx->newSet());
    setSetOrderInPlace(ctx, s, ctx->newSparseList());
    setSetSizeInPlace(ctx, s, 0L);
    return s;
}

// Add a single value to a Set in place (with normalization, no-op if already present).
static void setAddValue(proto::ProtoContext* ctx,
                        const proto::ProtoObject* setObj,
                        const proto::ProtoObject* val)
{
    val = normalizeSetVal(ctx, val);
    if (setContains(ctx, setObj, val)) return;
    const proto::ProtoSet* core  = getSetCore(ctx, setObj);
    const proto::ProtoSparseList* order = getSetOrder(ctx, setObj);
    long sz = getSetSize(ctx, setObj);
    if (core)  setSetCoreInPlace(ctx, setObj, core->add(ctx, val));
    if (order) setSetOrderInPlace(ctx, setObj,
                   order->setAt(ctx, static_cast<unsigned long>(sz), val));
    setSetSizeInPlace(ctx, setObj, sz + 1);
}

// Iterate `other` (a Set or array-like) and add each element to setObj.
static void setAddAllFrom(proto::ProtoContext* ctx,
                          const proto::ProtoObject* setObj,
                          const proto::ProtoObject* other)
{
    if (!other || other == PROTO_NONE) return;
    // Prefer Set protocol (has __set_order__).
    const proto::ProtoSparseList* otherOrder = getSetOrder(ctx, other);
    if (otherOrder) {
        const proto::ProtoSparseListIterator* it = otherOrder->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            const proto::ProtoObject* v = it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            setAddValue(ctx, setObj, v ? v : PROTO_NONE);
        }
        return;
    }
    // Fall back to array-like length+index.
    const proto::ProtoString* lenKs = JSSymbols::length(ctx);
    if (!lenKs) return;
    const proto::ProtoObject* lenObj = other->getAttribute(ctx, lenKs, true);
    if (!lenObj || lenObj == PROTO_NONE || !lenObj->isInteger(ctx)) return;
    long len = lenObj->asLong(ctx);
    for (long i = 0; i < len; i++) {
        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        if (!ik) continue;
        const proto::ProtoObject* v = other->getAttribute(ctx, ik, true);
        setAddValue(ctx, setObj, v ? v : PROTO_NONE);
    }
}
```

- [ ] **Step 5: Implement the seven collection methods**

Add after `setAddAllFrom`, still in the anonymous namespace:

```cpp
// ---------------------------------------------------------------------------
// set.union(other) → new Set with elements of this and other
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setUnion(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other) other = PROTO_NONE;
    if (other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.union: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* result = makeEmptySet(ctx);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    setAddAllFrom(ctx, result, self);
    setAddAllFrom(ctx, result, other);
    return result;
}

// ---------------------------------------------------------------------------
// set.intersection(other) → new Set with elements in both this and other
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setIntersection(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other || other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.intersection: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* result = makeEmptySet(ctx);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return result;
    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        if (setContains(ctx, other, v))
            setAddValue(ctx, result, v);
    }
    return result;
}

// ---------------------------------------------------------------------------
// set.difference(other) → new Set with elements in this but not in other
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setDifference(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other || other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.difference: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* result = makeEmptySet(ctx);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return result;
    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        if (!setContains(ctx, other, v))
            setAddValue(ctx, result, v);
    }
    return result;
}

// ---------------------------------------------------------------------------
// set.symmetricDifference(other) → (this ∪ other) − (this ∩ other)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setSymmetricDifference(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other || other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.symmetricDifference: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* result = makeEmptySet(ctx);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    // Elements in this but not in other.
    {
        const proto::ProtoSparseList* order = getSetOrder(ctx, self);
        if (order) {
            const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
            while (it && it->hasNext(ctx)) {
                const proto::ProtoObject* v = it->nextValue(ctx);
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                if (!v) v = PROTO_NONE;
                if (!setContains(ctx, other, v))
                    setAddValue(ctx, result, v);
            }
        }
    }
    // Elements in other but not in this.
    {
        const proto::ProtoSparseList* order = getSetOrder(ctx, other);
        if (order) {
            const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
            while (it && it->hasNext(ctx)) {
                const proto::ProtoObject* v = it->nextValue(ctx);
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                if (!v) v = PROTO_NONE;
                if (!setContains(ctx, self, v))
                    setAddValue(ctx, result, v);
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// set.isSubsetOf(other) → true if every element of this is in other
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setIsSubsetOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other || other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.isSubsetOf: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return PROTO_TRUE;
    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        if (!setContains(ctx, other, v)) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// set.isSupersetOf(other) → true if every element of other is in this
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setIsSupersetOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other || other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.isSupersetOf: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoSparseList* order = getSetOrder(ctx, other);
    if (!order) return PROTO_TRUE;
    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        if (!setContains(ctx, self, v)) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// set.isDisjointFrom(other) → true if no element of this is in other
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setIsDisjointFrom(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other || other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.isDisjointFrom: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return PROTO_TRUE;
    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        if (setContains(ctx, other, v)) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}
```

- [ ] **Step 6: Replace installMethod lambda in BuildSetPrototype**

In `BuildSetPrototype`, replace the `auto installMethod = [&]...` lambda and all its calls with:

```cpp
    setProto = installNonEnumerableMethod(ctx, setProto, "add",     setAdd,     1);
    setProto = installNonEnumerableMethod(ctx, setProto, "has",     setHas,     1);
    setProto = installNonEnumerableMethod(ctx, setProto, "delete",  setDeleteFn, 1);
    setProto = installNonEnumerableMethod(ctx, setProto, "clear",   setClear,   0);
    setProto = installNonEnumerableMethod(ctx, setProto, "forEach", setForEach, 1);
    setProto = installNonEnumerableMethod(ctx, setProto, "values",  setValues,  0);
    setProto = installNonEnumerableMethod(ctx, setProto, "keys",    setKeys,    0);
    setProto = installNonEnumerableMethod(ctx, setProto, "entries", setEntries, 0);
    // ES2025 collection methods
    setProto = installNonEnumerableMethod(ctx, setProto, "union",               setUnion,               1);
    setProto = installNonEnumerableMethod(ctx, setProto, "intersection",        setIntersection,        1);
    setProto = installNonEnumerableMethod(ctx, setProto, "difference",          setDifference,          1);
    setProto = installNonEnumerableMethod(ctx, setProto, "symmetricDifference", setSymmetricDifference, 1);
    setProto = installNonEnumerableMethod(ctx, setProto, "isSubsetOf",          setIsSubsetOf,          1);
    setProto = installNonEnumerableMethod(ctx, setProto, "isSupersetOf",        setIsSupersetOf,        1);
    setProto = installNonEnumerableMethod(ctx, setProto, "isDisjointFrom",      setIsDisjointFrom,      1);
```

- [ ] **Step 7: Add Symbol.iterator and Symbol.toStringTag to Set prototype**

After the `installNonEnumerableMethod` calls (before the `__get_size__` block), add:

```cpp
    // Symbol.iterator = values (Set iterates values)
    // JSSymbols::symbolIterator returns "Symbol.iterator"; sidecar = "__pd_Symbol.iterator__"
    {
        const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(ctx);
        if (symIterKey) {
            const proto::ProtoString* valuesKey = ctx->fromUTF8String("values")->asString(ctx);
            const proto::ProtoObject* valuesFn = valuesKey
                ? setProto->getAttribute(ctx, valuesKey, false) : nullptr;
            if (valuesFn && valuesFn != PROTO_NONE) {
                setProto = setProto->setAttribute(ctx, symIterKey, valuesFn);
                const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_Symbol.iterator__");
                const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
                if (pdks) setProto = setProto->setAttribute(ctx, pdks, ctx->fromInteger(0x3LL));
            }
        }
    }

    // Symbol.toStringTag = "Set": {writable:false, enumerable:false, configurable:true} = 0x2
    // JSSymbols::toStringTag returns "__toStringTag__"; sidecar = "__pd___toStringTag____"
    {
        const proto::ProtoString* tstKey = JSSymbols::toStringTag(ctx);
        if (tstKey) {
            setProto = setProto->setAttribute(ctx, tstKey, ctx->fromUTF8String("Set"));
            const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd___toStringTag____");
            const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
            if (pdks) setProto = setProto->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
        }
    }
```

- [ ] **Step 8: Verify JSSymbols::toStringTag returns the right key**

```bash
grep -n "toStringTag\|__toStringTag__" src/JSSymbols.cpp | head -5
```

Expected: shows `toStringTag` returning string `"__toStringTag__"` (or `"Symbol.toStringTag"`). If it returns `"Symbol.toStringTag"`, then the test262 checks for `obj[Symbol.toStringTag]` may not work. Verify by running:

```bash
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -3
cat > /tmp/test_set_tag.js << 'EOF'
var s = new Set([1,2,3]);
var s2 = new Set([2,3,4]);
console.log('union size:', s.union(s2).size);              // 4
console.log('intersection:', [...s.intersection(s2)]);     // [2,3]
console.log('difference:', [...s.difference(s2)]);         // [1]
console.log('symDiff:', [...s.symmetricDifference(s2)]);   // [1,4]
console.log('isSubset:', new Set([2]).isSubsetOf(s));       // true
console.log('isSuperset:', s.isSupersetOf(new Set([1])));  // true
console.log('isDisjoint:', new Set([5]).isDisjointFrom(s)); // true
EOF
PROTOJS_NO_FALLBACK=1 ./build/protojs /tmp/test_set_tag.js 2>/dev/null
```
Expected:
```
union size: 4
intersection: 2,3
difference: 1
symDiff: 1,4
isSubset: true
isSuperset: true
isDisjoint: true
```

- [ ] **Step 9: Run Set test suite**

```bash
TEST262_PATTERNS="built-ins/Set" node tests/test262/runner/test262_runner.js 2>&1 | tail -3
```
Expected: summary shows ≥ 280 passed (from 117 baseline)

- [ ] **Step 10: Commit**

```bash
git add src/SetPrototype.cpp
git commit -m "feat(phase35): Set — non-enumerable descriptors, guards, -0, Symbol.iterator/toStringTag, collection methods (union/intersection/difference/symmetricDifference/isSubsetOf/isSupersetOf/isDisjointFrom)"
```

---

## Task 5: Run full test snapshots, update TEST262_STATUS.md, commit

**Files:**
- Modify: `docs/TEST262_STATUS.md`

- [ ] **Step 1: Run Map and Set test suites for final snapshot**

```bash
TEST262_PATTERNS="built-ins/Map" node tests/test262/runner/test262_runner.js 2>&1 | tail -5
TEST262_PATTERNS="built-ins/Set" node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```
Note the snapshot filenames printed.

- [ ] **Step 2: Run language/expressions regression check**

```bash
TEST262_PATTERNS="language/expressions" node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```
Expected: ≥ 9416 passed (same as Phase 34 baseline — no regression)

- [ ] **Step 3: Parse results with node**

```bash
node -e "
['built-ins/Map', 'built-ins/Set'].forEach(function(p) {
  var files = require('fs').readdirSync('tests/test262/reports')
    .filter(function(f){ return f.startsWith('snapshot-' + p.replace(/\//g,'_').replace(/-/g,'_')); })
    .sort();
  var last = files[files.length - 1];
  if (!last) { console.log('No snapshot for', p); return; }
  var d = JSON.parse(require('fs').readFileSync('tests/test262/reports/' + last, 'utf8'));
  console.log(p + ': ' + d.summary.passed + '/' + d.total + ' (' + Math.round(d.summary.passed/d.total*100) + '%)');
});
"
```

- [ ] **Step 4: Update TEST262_STATUS.md**

Add a new Phase 35 section at the top (after the `---` separator following the header), following the established format. Include:
- Phase 35 header with date 2026-04-12
- Table with columns: Area, Total, Passed, Pass%, Phase 34 Baseline, Delta
- Rows for `built-ins/Map` and `built-ins/Set` with actual numbers from step 3
- Row for `language/expressions` showing no regression
- Key implementations delivered table
- Notes on what was fixed

- [ ] **Step 5: Commit final snapshot and status update**

```bash
git add docs/TEST262_STATUS.md
git commit -m "docs: update TEST262_STATUS.md with Phase 35 results (Map/Set completion)"
```

---

## Self-Review Checklist

**Spec coverage:**
- [x] Property descriptors (enumerable:false, writable/configurable per spec) → PrototypeUtils + installNonEnumerableMethod
- [x] `length` and `name` on functions with correct descriptors → installNonEnumerableMethod
- [x] `requireMapThis` / `requireSetThis` guards → Tasks 2 and 4
- [x] `-0` normalization in Map and Set → `normalizeMapKey` / `normalizeSetVal`
- [x] `getOrInsert`, `getOrInsertComputed` → Task 3
- [x] `Map.groupBy` static → Task 3
- [x] `Symbol.iterator` (Map=entries, Set=values) → Tasks 2 and 4
- [x] `Symbol.toStringTag` ("Map" / "Set") → Tasks 2 and 4
- [x] Set collection methods (7) → Task 4

**Type consistency:**
- `normalizeMapKey` used in `mapFind` (line 129) and `mapSet` (line 161)
- `normalizeSetVal` used in `setContains`, `setAdd`, `setDeleteFn`
- `requireMapThis` used in all 10 Map methods
- `requireSetThis` used in all 8+7=15 Set methods
- `makeEmptySet` used in all 7 collection methods
- `setAddValue` used in `setAddAllFrom` and `setIntersection`/`setSymmetricDifference`
