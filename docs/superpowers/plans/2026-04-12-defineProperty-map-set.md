# defineProperty Full Protocol + Map + Set — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recover ~1,050 test262 tests across Object.getOwnPropertyNames, Map, and Set.

**Architecture:** Phase 33 fixes `objectGetOwnPropertyNames` (stub that delegates to `objectKeys`, skipping non-enumerable properties) and the `getOwnPropertyDescriptor` bug where `!val` returns early even when the property exists with an undefined value. Phase 34 implements Map and Set using only protoCore structures (ProtoSparseList for ordered storage, ProtoSet for O(log n) membership).

**Tech Stack:** C++20, protoCore (ProtoSparseList, ProtoSet, ProtoObject immutable graph), QuickJS bytecode interpreter, test262 runner.

---

## Phase 33: Object.defineProperty Full Descriptor Protocol

### Task 1: Fix `collectOwnKeys` + `objectGetOwnPropertyNames` + `objectGetOwnPropertyDescriptor`

**Files:**
- Modify: `src/ObjectPrototype.cpp:36-85` (collectOwnKeys — add includeNonEnumerable param)
- Modify: `src/ObjectPrototype.cpp:442-450` (objectGetOwnPropertyNames — replace stub)
- Modify: `src/ObjectPrototype.cpp:740-741` (objectGetOwnPropertyDescriptor — `!val` guard)

- [ ] **Step 1: Add `includeNonEnumerable` parameter to `collectOwnKeys`**

Change the signature and the enumerable-filter block in `src/ObjectPrototype.cpp`:

```cpp
// OLD signature (line 36-40):
static void collectOwnKeys(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* obj,
    std::vector<std::string>&       keys,
    std::vector<const proto::ProtoObject*>* vals)

// NEW signature:
static void collectOwnKeys(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* obj,
    std::vector<std::string>&       keys,
    std::vector<const proto::ProtoObject*>* vals,
    bool includeNonEnumerable = false)
```

And change the enumerable filter block (lines 70-81) from:
```cpp
        // Respect the enumerable descriptor flag (bit 2 of __pd_<key>__).
        // A missing __pd__ key means default = enumerable (bit 2 = 1).
        {
            std::string pdKeyStr = "__pd_" + kstr + "__";
            const proto::ProtoObject* pko = ctx->fromUTF8String(pdKeyStr.c_str());
            const proto::ProtoString* pdk = pko ? pko->asString(ctx) : nullptr;
            if (pdk) {
                const proto::ProtoObject* pdv = obj->getAttribute(ctx, pdk, false);
                if (pdv && pdv != PROTO_NONE && pdv->isInteger(ctx)) {
                    uint8_t bits = static_cast<uint8_t>(pdv->asLong(ctx));
                    if (!(bits & 0x4)) continue; // not enumerable — skip
                }
            }
        }
```
to:
```cpp
        // Respect the enumerable descriptor flag (bit 2 of __pd_<key>__).
        // A missing __pd__ key means default = enumerable (bit 2 = 1).
        // Skip non-enumerable properties unless includeNonEnumerable is set.
        if (!includeNonEnumerable) {
            std::string pdKeyStr = "__pd_" + kstr + "__";
            const proto::ProtoObject* pko = ctx->fromUTF8String(pdKeyStr.c_str());
            const proto::ProtoString* pdk = pko ? pko->asString(ctx) : nullptr;
            if (pdk) {
                const proto::ProtoObject* pdv = obj->getAttribute(ctx, pdk, false);
                if (pdv && pdv != PROTO_NONE && pdv->isInteger(ctx)) {
                    uint8_t bits = static_cast<uint8_t>(pdv->asLong(ctx));
                    if (!(bits & 0x4)) continue; // not enumerable — skip
                }
            }
        }
```

- [ ] **Step 2: Replace `objectGetOwnPropertyNames` stub**

Replace the stub at lines 442-450 with:
```cpp
static const proto::ProtoObject* objectGetOwnPropertyNames(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* obj = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : nullptr;

    // Include ALL own string-keyed properties regardless of enumerable flag.
    std::vector<std::string> keys;
    collectOwnKeys(ctx, obj, keys, nullptr, /*includeNonEnumerable=*/true);

    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    const proto::ProtoString* isArrKey2 = JSSymbols::isArray(ctx);
    for (size_t i = 0; i < keys.size(); i++) {
        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        const proto::ProtoObject* kv = ctx->fromUTF8String(keys[i].c_str());
        if (ik && kv) result = result->setAttribute(ctx, ik, kv);
    }
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(static_cast<long long>(keys.size())));
    if (isArrKey2) result = result->setAttribute(ctx, isArrKey2, ctx->fromInteger(1LL));
    return result;
}
```

- [ ] **Step 3: Fix `objectGetOwnPropertyDescriptor` `!val` guard**

On line 741 change:
```cpp
    const proto::ProtoObject* val = target->getAttribute(ctx, pk, false);
    if (!val) return PROTO_NONE;
```
to:
```cpp
    const proto::ProtoObject* val = target->getAttribute(ctx, pk, false);
    // val==nullptr means getAttribute returned null (e.g. attribute absent at C++ level);
    // treat as PROTO_NONE (undefined) — the property DOES exist (ownFlag confirmed above).
    const proto::ProtoObject* storedVal = val ? val : PROTO_NONE;
```
Then replace the use of `val` in the result construction with `storedVal`:
```cpp
    setAttr(result, "value",        storedVal);
```

- [ ] **Step 4: Build and verify smoke tests**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS/.worktrees/feat-phases-33-34/build
cmake --build . -j$(nproc) 2>&1 | tail -5
```
Expected: `[100%] Built target protojs`

```bash
cd /home/gamarino/Documentos/proyectos/protoJS/.worktrees/feat-phases-33-34
./build/protojs -e "
var o = {};
Object.defineProperty(o, 'a', {value:1, enumerable:false});
var names = Object.getOwnPropertyNames(o);
print('names:', names.length, names[0]);
print('keys:', Object.keys(o).length);
"
```
Expected: `names: 1 a` and `keys: 0`

```bash
./build/protojs -e "
var o = {};
Object.defineProperty(o, 'x', {value: undefined, writable:true, enumerable:true, configurable:true});
var d = Object.getOwnPropertyDescriptor(o, 'x');
print('desc:', d !== undefined, d.value === undefined, d.writable);
"
```
Expected: `desc: true true true`

- [ ] **Step 5: Commit Phase 33 code changes**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS/.worktrees/feat-phases-33-34
git add src/ObjectPrototype.cpp
git commit -m "feat(phase33): fix getOwnPropertyNames and getOwnPropertyDescriptor

- collectOwnKeys: add includeNonEnumerable parameter (default false)
- objectGetOwnPropertyNames: replace objectKeys stub, pass includeNonEnumerable=true
- objectGetOwnPropertyDescriptor: treat !val as PROTO_NONE rather than absence

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

### Task 2: Phase 33 test262 snapshot and TEST262_STATUS.md update

**Files:**
- Modify: `docs/TEST262_STATUS.md`

- [ ] **Step 1: Run test262 snapshots for affected areas**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS/.worktrees/feat-phases-33-34
TEST262_ROOT=../../../test262 TEST262_USE_PROTO_EVAL=1 \
  TEST262_PATTERNS="built-ins/Object/getOwnPropertyNames,built-ins/Object/getOwnPropertyDescriptor,built-ins/Object/defineProperty,built-ins/Object/defineProperties" \
  node tests/test262/runner/test262_runner.js 2>&1 | tail -30
```

- [ ] **Step 2: Update `docs/TEST262_STATUS.md`**

Add a Phase 33 section with actual counts from the test run output.

- [ ] **Step 3: Commit documentation**

```bash
git add docs/TEST262_STATUS.md
git commit -m "docs: update TEST262_STATUS.md with Phase 33 results"
```

---

## Phase 34: Map and Set

### Task 3: Implement `src/MapPrototype.h` and `src/MapPrototype.cpp`

**Files:**
- Create: `src/MapPrototype.h`
- Create: `src/MapPrototype.cpp`

Backing storage: four hidden attributes on the Map wrapper object:
- `__map_keys__` — ProtoSparseList (index → key ProtoObject)
- `__map_vals__` — ProtoSparseList (index → value ProtoObject)
- `__map_hash__` — ProtoSparseList (hash → insertion index)
- `__map_size__` — integer ProtoObject (count of live entries)

Helper functions:
- `szvHash(ctx, key)` — SameValueZero-compatible hash
- `sameValueZero(ctx, a, b)` — SameValueZero equality comparison
- `mapFind(ctx, mapObj, key, &foundIdx)` — hash lookup + scan, returns true if found

- [ ] **Step 1: Write `src/MapPrototype.h`**

```cpp
#ifndef PROTOJS_MAPPROTOTYPE_H
#define PROTOJS_MAPPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Build the JS Map prototype (get, set, has, delete, clear, forEach,
 * keys, values, entries, size getter) and attach to space->mapPrototype.
 */
void BuildMapPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                       const proto::ProtoObject* objectProto);

/**
 * Register the Map constructor in the global root.
 * Idempotent — no-op when "Map" is already present.
 */
void ensureMapConstructor(proto::ProtoContext* ctx,
                          const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_MAPPROTOTYPE_H
```

- [ ] **Step 2: Write helper functions in `src/MapPrototype.cpp`**

```cpp
#include "MapPrototype.h"
#include "JSSymbols.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"
#include <cmath>
#include <cstring>
#include <string>

namespace protojs {

namespace {

// ---------------------------------------------------------------------------
// SameValueZero hash — used as index into __map_hash__ ProtoSparseList.
// ---------------------------------------------------------------------------
static unsigned long szvHash(proto::ProtoContext* ctx, const proto::ProtoObject* key) {
    if (!key || key == PROTO_NONE)          return 0UL;
    if (key == PROTO_TRUE)                  return 1UL;
    if (key == PROTO_FALSE)                 return 2UL;
    if (key->isInteger(ctx))
        return static_cast<unsigned long>(key->asLong(ctx) & 0x7FFFFFFF);
    if (key->isDouble(ctx) || key->isFloat(ctx)) {
        double d = key->asDouble(ctx);
        if (std::isnan(d))                  return 3UL;
        uint64_t bits; memcpy(&bits, &d, 8);
        return static_cast<unsigned long>(bits ^ (bits >> 32));
    }
    if (key->isString(ctx)) {
        const proto::ProtoString* ps = key->asString(ctx);
        return ps ? static_cast<unsigned long>(ps->getHash(ctx)) : 4UL;
    }
    // Object/other: pointer identity.
    return static_cast<unsigned long>(reinterpret_cast<uintptr_t>(key) >> 3);
}

// ---------------------------------------------------------------------------
// SameValueZero equality.
// ---------------------------------------------------------------------------
static bool sameValueZero(proto::ProtoContext* ctx,
                           const proto::ProtoObject* a,
                           const proto::ProtoObject* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    // NaN === NaN under SameValueZero
    if ((a->isDouble(ctx) || a->isFloat(ctx)) && (b->isDouble(ctx) || b->isFloat(ctx))) {
        double da = a->asDouble(ctx), db = b->asDouble(ctx);
        if (std::isnan(da) && std::isnan(db)) return true;
        return da == db;
    }
    if (a->isInteger(ctx) && b->isInteger(ctx))
        return a->asLong(ctx) == b->asLong(ctx);
    if (a->isString(ctx) && b->isString(ctx)) {
        std::string sa, sb;
        a->asString(ctx)->toUTF8String(ctx, sa);
        b->asString(ctx)->toUTF8String(ctx, sb);
        return sa == sb;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Retrieve one of the map's hidden ProtoSparseList attributes by name.
// Returns nullptr if absent.
// ---------------------------------------------------------------------------
static const proto::ProtoSparseList* getMapList(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* mapObj,
    const char* attrName)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String(attrName);
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks) return nullptr;
    const proto::ProtoObject* v = mapObj->getAttribute(ctx, ks, false);
    return (v && v != PROTO_NONE) ? v->asSparseList(ctx) : nullptr;
}

// ---------------------------------------------------------------------------
// Set one of the map's hidden attributes to a new ProtoSparseList value.
// Returns the updated mapObj root.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setMapList(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* mapObj,
    const char* attrName,
    const proto::ProtoSparseList* list)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String(attrName);
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks || !list) return mapObj;
    return mapObj->setAttribute(ctx, ks, list->asObject(ctx));
}

// ---------------------------------------------------------------------------
// Look up a key in the Map. Returns true + sets foundIdx if found.
// Uses hash slot first, falls back to linear scan on collision.
// ---------------------------------------------------------------------------
static bool mapFind(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* mapObj,
    const proto::ProtoObject* key,
    unsigned long& foundIdx)
{
    const proto::ProtoSparseList* keysList = getMapList(ctx, mapObj, "__map_keys__");
    const proto::ProtoSparseList* hashList = getMapList(ctx, mapObj, "__map_hash__");
    if (!keysList || !hashList) return false;

    unsigned long h = szvHash(ctx, key);
    // Check hash slot first.
    if (hashList->has(ctx, h)) {
        const proto::ProtoObject* slotObj = hashList->getAt(ctx, h);
        if (slotObj && slotObj != PROTO_NONE && slotObj->isInteger(ctx)) {
            unsigned long idx = static_cast<unsigned long>(slotObj->asLong(ctx));
            if (keysList->has(ctx, idx)) {
                const proto::ProtoObject* k = keysList->getAt(ctx, idx);
                if (sameValueZero(ctx, k, key)) { foundIdx = idx; return true; }
            }
        }
    }
    // Linear scan for collisions.
    const proto::ProtoSparseListIterator* it = keysList->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        unsigned long idx = it->nextKey(ctx);
        const proto::ProtoObject* k = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (sameValueZero(ctx, k, key)) { foundIdx = idx; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Get the map size (live entry count) as a C++ long.
// ---------------------------------------------------------------------------
static long getMapSize(proto::ProtoContext* ctx, const proto::ProtoObject* mapObj) {
    const proto::ProtoObject* ko = ctx->fromUTF8String("__map_size__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks) return 0L;
    const proto::ProtoObject* v = mapObj->getAttribute(ctx, ks, false);
    return (v && v != PROTO_NONE && v->isInteger(ctx)) ? v->asLong(ctx) : 0L;
}

// ---------------------------------------------------------------------------
// Set the map size attribute. Returns updated mapObj root.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setMapSize(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* mapObj,
    long sz)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String("__map_size__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks) return mapObj;
    return mapObj->setAttribute(ctx, ks, ctx->fromInteger(sz));
}
```

- [ ] **Step 3: Write Map constructor and prototype methods**

```cpp
// ---------------------------------------------------------------------------
// Map constructor: new Map(iterable?)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapConstruct(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self) return PROTO_NONE;

    // Initialize empty backing storage.
    const proto::ProtoSparseList* emptyList = ctx->newSparseList();
    const proto::ProtoObject* mapObj = self;
    mapObj = setMapList(ctx, mapObj, "__map_keys__", emptyList);
    mapObj = setMapList(ctx, mapObj, "__map_vals__", emptyList);
    mapObj = setMapList(ctx, mapObj, "__map_hash__", emptyList);
    mapObj = setMapSize(ctx, mapObj, 0L);

    // If iterable argument provided, call map.set for each [k, v] pair.
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* iterable = args->getAt(ctx, 0);
        if (iterable && iterable != PROTO_NONE) {
            // Try array-like: iterate indices 0..length-1.
            const proto::ProtoObject* lenKo = ctx->fromUTF8String("length");
            const proto::ProtoString* lenKs = lenKo ? lenKo->asString(ctx) : nullptr;
            if (lenKs) {
                const proto::ProtoObject* lenObj = iterable->getAttribute(ctx, lenKs, true);
                if (lenObj && lenObj != PROTO_NONE && lenObj->isInteger(ctx)) {
                    long len = lenObj->asLong(ctx);
                    for (long i = 0; i < len; i++) {
                        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                        if (!ik) continue;
                        const proto::ProtoObject* pair = iterable->getAttribute(ctx, ik, true);
                        if (!pair || pair == PROTO_NONE) continue;
                        // pair must be [key, value].
                        const proto::ProtoString* k0 = JSSymbols::indexKey(ctx, 0);
                        const proto::ProtoString* k1 = JSSymbols::indexKey(ctx, 1);
                        if (!k0 || !k1) continue;
                        const proto::ProtoObject* pkey = pair->getAttribute(ctx, k0, true);
                        const proto::ProtoObject* pval = pair->getAttribute(ctx, k1, true);
                        // Perform map.set(pkey, pval) inline.
                        unsigned long existingIdx = 0;
                        const proto::ProtoSparseList* keysList = getMapList(ctx, mapObj, "__map_keys__");
                        const proto::ProtoSparseList* valsList = getMapList(ctx, mapObj, "__map_vals__");
                        const proto::ProtoSparseList* hashList = getMapList(ctx, mapObj, "__map_hash__");
                        long sz = getMapSize(ctx, mapObj);
                        if (keysList && valsList && hashList) {
                            if (mapFind(ctx, mapObj, pkey, existingIdx)) {
                                valsList = valsList->setAt(ctx, existingIdx, pval ? pval : PROTO_NONE);
                                mapObj = setMapList(ctx, mapObj, "__map_vals__", valsList);
                            } else {
                                unsigned long newIdx = static_cast<unsigned long>(sz);
                                keysList = keysList->setAt(ctx, newIdx, pkey ? pkey : PROTO_NONE);
                                valsList = valsList->setAt(ctx, newIdx, pval ? pval : PROTO_NONE);
                                unsigned long h = szvHash(ctx, pkey);
                                if (!hashList->has(ctx, h))
                                    hashList = hashList->setAt(ctx, h, ctx->fromInteger(static_cast<long long>(newIdx)));
                                mapObj = setMapList(ctx, mapObj, "__map_keys__", keysList);
                                mapObj = setMapList(ctx, mapObj, "__map_vals__", valsList);
                                mapObj = setMapList(ctx, mapObj, "__map_hash__", hashList);
                                mapObj = setMapSize(ctx, mapObj, sz + 1);
                            }
                        }
                    }
                }
            }
        }
    }
    return mapObj;
}

// ---------------------------------------------------------------------------
// map.set(key, value) → map
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapSet(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* key = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* val = (args && args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!key) key = PROTO_NONE;
    if (!val) val = PROTO_NONE;

    const proto::ProtoSparseList* keysList = getMapList(ctx, self, "__map_keys__");
    const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
    const proto::ProtoSparseList* hashList = getMapList(ctx, self, "__map_hash__");
    if (!keysList || !valsList || !hashList) return self;

    unsigned long existingIdx = 0;
    if (mapFind(ctx, self, key, existingIdx)) {
        valsList = valsList->setAt(ctx, existingIdx, val);
        return setMapList(ctx, self, "__map_vals__", valsList);
    }
    long sz = getMapSize(ctx, self);
    unsigned long newIdx = static_cast<unsigned long>(sz);
    keysList = keysList->setAt(ctx, newIdx, key);
    valsList = valsList->setAt(ctx, newIdx, val);
    unsigned long h = szvHash(ctx, key);
    if (!hashList->has(ctx, h))
        hashList = hashList->setAt(ctx, h, ctx->fromInteger(static_cast<long long>(newIdx)));
    const proto::ProtoObject* result = self;
    result = setMapList(ctx, result, "__map_keys__", keysList);
    result = setMapList(ctx, result, "__map_vals__", valsList);
    result = setMapList(ctx, result, "__map_hash__", hashList);
    result = setMapSize(ctx, result, sz + 1);
    return result;
}

// ---------------------------------------------------------------------------
// map.get(key) → value or undefined
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapGet(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* key = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!key) key = PROTO_NONE;

    unsigned long foundIdx = 0;
    if (!mapFind(ctx, self, key, foundIdx)) return PROTO_NONE;

    const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
    if (!valsList) return PROTO_NONE;
    const proto::ProtoObject* v = valsList->getAt(ctx, foundIdx);
    return v ? v : PROTO_NONE;
}

// ---------------------------------------------------------------------------
// map.has(key) → boolean
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapHas(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoObject* key = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!key) key = PROTO_NONE;
    unsigned long foundIdx = 0;
    return mapFind(ctx, self, key, foundIdx) ? PROTO_TRUE : PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// map.delete(key) → boolean (true if entry was present)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapDelete(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoObject* key = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!key) key = PROTO_NONE;

    unsigned long foundIdx = 0;
    if (!mapFind(ctx, self, key, foundIdx)) return PROTO_FALSE;

    const proto::ProtoSparseList* keysList = getMapList(ctx, self, "__map_keys__");
    const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
    const proto::ProtoSparseList* hashList = getMapList(ctx, self, "__map_hash__");
    if (!keysList || !valsList || !hashList) return PROTO_FALSE;

    keysList = keysList->removeAt(ctx, foundIdx);
    valsList = valsList->removeAt(ctx, foundIdx);
    unsigned long h = szvHash(ctx, key);
    if (hashList->has(ctx, h)) {
        const proto::ProtoObject* slotObj = hashList->getAt(ctx, h);
        if (slotObj && slotObj->isInteger(ctx) &&
            static_cast<unsigned long>(slotObj->asLong(ctx)) == foundIdx)
            hashList = hashList->removeAt(ctx, h);
    }

    long sz = getMapSize(ctx, self);
    const proto::ProtoObject* result = self;
    result = setMapList(ctx, result, "__map_keys__", keysList);
    result = setMapList(ctx, result, "__map_vals__", valsList);
    result = setMapList(ctx, result, "__map_hash__", hashList);
    result = setMapSize(ctx, result, sz > 0 ? sz - 1 : 0);
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// map.clear() → undefined
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapClear(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoSparseList* emptyList = ctx->newSparseList();
    const proto::ProtoObject* result = self;
    result = setMapList(ctx, result, "__map_keys__", emptyList);
    result = setMapList(ctx, result, "__map_vals__", emptyList);
    result = setMapList(ctx, result, "__map_hash__", emptyList);
    result = setMapSize(ctx, result, 0L);
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// map.size (getter) → integer
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapSizeGetter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return ctx->fromInteger(0LL);
    return ctx->fromInteger(static_cast<long long>(getMapSize(ctx, self)));
}

// ---------------------------------------------------------------------------
// map.forEach(callback, thisArg?)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapForEach(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* callback = args->getAt(ctx, 0);
    if (!callback || callback == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* thisArg = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;

    const proto::ProtoSparseList* keysList = getMapList(ctx, self, "__map_keys__");
    const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
    if (!keysList || !valsList) return PROTO_NONE;

    const proto::ProtoSparseListIterator* it = keysList->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        unsigned long idx = it->nextKey(ctx);
        const proto::ProtoObject* k = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        const proto::ProtoObject* v = valsList->has(ctx, idx) ? valsList->getAt(ctx, idx) : PROTO_NONE;
        if (!k) k = PROTO_NONE;
        if (!v) v = PROTO_NONE;
        // Call callback(value, key, map).
        const proto::ProtoObject* cbArgs[3] = {v, k, self};
        proto::ProtoList* callArgsList = ctx->newList();
        if (callArgsList) {
            callArgsList = const_cast<proto::ProtoList*>(callArgsList->append(ctx, v));
            callArgsList = const_cast<proto::ProtoList*>(callArgsList->append(ctx, k));
            callArgsList = const_cast<proto::ProtoList*>(callArgsList->append(ctx, self));
        }
        callFunction(ctx, callback, thisArg ? thisArg : PROTO_NONE, callArgsList, nullptr);
    }
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// Map iterator helper: creates a JS iterator over keys, values, or entries.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* makeMapIterator(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* mapObj,
    const char* kind) // "keys", "values", "entries"
{
    const proto::ProtoObject* iter = ctx->newObject(true);
    const proto::ProtoObject* targetKey = ctx->fromUTF8String("__iter_target__");
    const proto::ProtoObject* posKey    = ctx->fromUTF8String("__iter_pos__");
    const proto::ProtoObject* kindKey   = ctx->fromUTF8String("__iter_kind__");
    const proto::ProtoString* tk = targetKey ? targetKey->asString(ctx) : nullptr;
    const proto::ProtoString* pk = posKey    ? posKey->asString(ctx)    : nullptr;
    const proto::ProtoString* kk = kindKey   ? kindKey->asString(ctx)   : nullptr;
    if (tk) iter = iter->setAttribute(ctx, tk, mapObj);
    if (pk) iter = iter->setAttribute(ctx, pk, ctx->fromInteger(0LL));
    if (kk) iter = iter->setAttribute(ctx, kk, ctx->fromUTF8String(kind));
    return iter;
}

static const proto::ProtoObject* mapKeys(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeMapIterator(ctx, self, "keys"); }

static const proto::ProtoObject* mapValues(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeMapIterator(ctx, self, "values"); }

static const proto::ProtoObject* mapEntries(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeMapIterator(ctx, self, "entries"); }

} // namespace

// ---------------------------------------------------------------------------
// BuildMapPrototype and ensureMapConstructor
// ---------------------------------------------------------------------------
void BuildMapPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                       const proto::ProtoObject* objectProto)
{
    if (!space || !ctx || !objectProto) return;

    const proto::ProtoObject* mapProto = objectProto->newChild(ctx, false);
    if (!mapProto) return;

    auto installMethod = [&](const proto::ProtoObject*& proto, const char* name,
                             proto::NativeFunction fn) {
        const proto::ProtoObject* ko = ctx->fromUTF8String(name);
        const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
        if (!ks) return;
        const proto::ProtoObject* mObj = ctx->newObject(true);
        if (!mObj) return;
        mObj = ctx->fromMethod(mObj, fn);
        proto = proto->setAttribute(ctx, ks, mObj);
    };

    installMethod(mapProto, "set",     mapSet);
    installMethod(mapProto, "get",     mapGet);
    installMethod(mapProto, "has",     mapHas);
    installMethod(mapProto, "delete",  mapDelete);
    installMethod(mapProto, "clear",   mapClear);
    installMethod(mapProto, "forEach", mapForEach);
    installMethod(mapProto, "keys",    mapKeys);
    installMethod(mapProto, "values",  mapValues);
    installMethod(mapProto, "entries", mapEntries);

    // Install size as a getter via __get_size__
    {
        const proto::ProtoObject* gko = ctx->fromUTF8String("__get_size__");
        const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
        if (gks) {
            const proto::ProtoObject* getter = ctx->newObject(true);
            getter = ctx->fromMethod(getter, mapSizeGetter);
            mapProto = mapProto->setAttribute(ctx, gks, getter);
        }
    }

    // Store map prototype in a global slot for the constructor to reference.
    {
        const proto::ProtoObject* slotKey = ctx->fromUTF8String("__map_proto__");
        const proto::ProtoString* slotKs  = slotKey ? slotKey->asString(ctx) : nullptr;
        if (slotKs && space->objectPrototype)
            const_cast<proto::ProtoObject*>(space->objectPrototype)
                ->setAttribute(ctx, slotKs, mapProto);
    }
}

void ensureMapConstructor(proto::ProtoContext* ctx,
                          const proto::ProtoObject** globalRoot)
{
    if (!ctx || !globalRoot || !*globalRoot) return;
    const proto::ProtoObject* ko = ctx->fromUTF8String("Map");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks) return;
    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, ks, false);
    if (existing && existing != PROTO_NONE) return; // already installed

    const proto::ProtoObject* methodProto = ctx->getMethodPrototype();
    const proto::ProtoObject* ctorParent  = methodProto ? methodProto : *globalRoot;
    const proto::ProtoObject* ctor        = ctorParent->newChild(ctx, true);
    if (!ctor) return;
    ctor = ctx->fromMethod(ctor, mapConstruct);

    // Retrieve map prototype stored by BuildMapPrototype.
    const proto::ProtoObject* mapProtoSlotKey = ctx->fromUTF8String("__map_proto__");
    const proto::ProtoString* mpsk = mapProtoSlotKey ? mapProtoSlotKey->asString(ctx) : nullptr;
    const proto::ProtoObject* mapProto = (mpsk && ctx->getRootObject())
        ? ctx->getRootObject()->getAttribute(ctx, mpsk, false) : nullptr;

    if (mapProto) {
        const proto::ProtoObject* protoKey = ctx->fromUTF8String("prototype");
        const proto::ProtoString* protoKs  = protoKey ? protoKey->asString(ctx) : nullptr;
        if (protoKs) ctor = ctor->setAttribute(ctx, protoKs, mapProto);
        // Set __construct__ for OP_call_constructor dispatch.
        const proto::ProtoObject* constructKey = ctx->fromUTF8String("__construct__");
        const proto::ProtoString* constructKs  = constructKey ? constructKey->asString(ctx) : nullptr;
        if (constructKs) {
            const proto::ProtoObject* constructFn = ctx->newObject(true);
            constructFn = ctx->fromMethod(constructFn, mapConstruct);
            ctor = ctor->setAttribute(ctx, constructKs, constructFn);
        }
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, ks, ctor);
}

} // namespace protojs
```

- [ ] **Step 4: Build and verify smoke tests**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS/.worktrees/feat-phases-33-34/build
cmake --build . -j$(nproc) 2>&1 | tail -5
```

```bash
cd /home/gamarino/Documentos/proyectos/protoJS/.worktrees/feat-phases-33-34
./build/protojs -e "
var m = new Map();
m.set('a', 1); m.set('b', 2);
print('size:', m.size);
print('get a:', m.get('a'));
print('has b:', m.has('b'));
print('has c:', m.has('c'));
m.delete('a');
print('size after delete:', m.size);
"
```
Expected: `size: 2`, `get a: 1`, `has b: true`, `has c: false`, `size after delete: 1`

- [ ] **Step 5: Commit MapPrototype**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS/.worktrees/feat-phases-33-34
git add src/MapPrototype.h src/MapPrototype.cpp CMakeLists.txt src/JSPrototypes.cpp src/runtime/ProtoInterpreter.cpp
git commit -m "feat(phase34): implement Map using protoCore ProtoSparseList backing

- MapPrototype.h/.cpp: full Map implementation (set, get, has, delete, clear, forEach, keys, values, entries, size)
- Backing storage: __map_keys__, __map_vals__, __map_hash__ (ProtoSparseList) + __map_size__ (integer)
- SameValueZero hash and equality for all key types including NaN
- Constructor accepts optional iterable argument (array-like protocol)
- Wire into JSPrototypes.cpp (BuildMapPrototype) and ProtoInterpreter.cpp (remove from kUnimplementedCtors)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

### Task 4: Implement `src/SetPrototype.h` and `src/SetPrototype.cpp`

**Files:**
- Create: `src/SetPrototype.h`
- Create: `src/SetPrototype.cpp`

Backing storage: three hidden attributes on the Set wrapper object:
- `__set_core__` — ProtoSet (fast membership O(log n))
- `__set_order__` — ProtoSparseList (insertion-order index → value)
- `__set_size__` — integer ProtoObject (count of live entries)

- [ ] **Step 1: Write `src/SetPrototype.h`**

```cpp
#ifndef PROTOJS_SETPROTOTYPE_H
#define PROTOJS_SETPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Build the JS Set prototype (add, has, delete, clear, forEach,
 * keys, values, entries, size getter) and attach to space->setPrototype.
 */
void BuildSetPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                       const proto::ProtoObject* objectProto);

/**
 * Register the Set constructor in the global root.
 * Idempotent — no-op when "Set" is already present.
 */
void ensureSetConstructor(proto::ProtoContext* ctx,
                          const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_SETPROTOTYPE_H
```

- [ ] **Step 2: Write `src/SetPrototype.cpp`**

```cpp
#include "SetPrototype.h"
#include "JSSymbols.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"
#include <cmath>
#include <cstring>
#include <string>

namespace protojs {

namespace {

// ---------------------------------------------------------------------------
// SameValueZero equality (same as in MapPrototype, duplicated for isolation).
// ---------------------------------------------------------------------------
static bool setSameValueZero(proto::ProtoContext* ctx,
                              const proto::ProtoObject* a,
                              const proto::ProtoObject* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if ((a->isDouble(ctx) || a->isFloat(ctx)) && (b->isDouble(ctx) || b->isFloat(ctx))) {
        double da = a->asDouble(ctx), db = b->asDouble(ctx);
        if (std::isnan(da) && std::isnan(db)) return true;
        return da == db;
    }
    if (a->isInteger(ctx) && b->isInteger(ctx))
        return a->asLong(ctx) == b->asLong(ctx);
    if (a->isString(ctx) && b->isString(ctx)) {
        std::string sa, sb;
        a->asString(ctx)->toUTF8String(ctx, sa);
        b->asString(ctx)->toUTF8String(ctx, sb);
        return sa == sb;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Helpers to access/update hidden Set attributes.
// ---------------------------------------------------------------------------
static const proto::ProtoSet* getSetCore(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* setObj)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_core__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks) return nullptr;
    const proto::ProtoObject* v = setObj->getAttribute(ctx, ks, false);
    return (v && v != PROTO_NONE) ? v->asSet(ctx) : nullptr;
}

static const proto::ProtoObject* setSetCore(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* setObj,
                                             const proto::ProtoSet* core)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_core__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks || !core) return setObj;
    return setObj->setAttribute(ctx, ks, core->asObject(ctx));
}

static const proto::ProtoSparseList* getSetOrder(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* setObj)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_order__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks) return nullptr;
    const proto::ProtoObject* v = setObj->getAttribute(ctx, ks, false);
    return (v && v != PROTO_NONE) ? v->asSparseList(ctx) : nullptr;
}

static const proto::ProtoObject* setSetOrder(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* setObj,
                                              const proto::ProtoSparseList* order)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_order__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks || !order) return setObj;
    return setObj->setAttribute(ctx, ks, order->asObject(ctx));
}

static long getSetSize(proto::ProtoContext* ctx, const proto::ProtoObject* setObj) {
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_size__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks) return 0L;
    const proto::ProtoObject* v = setObj->getAttribute(ctx, ks, false);
    return (v && v != PROTO_NONE && v->isInteger(ctx)) ? v->asLong(ctx) : 0L;
}

static const proto::ProtoObject* setSetSize(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* setObj,
                                             long sz)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_size__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks) return setObj;
    return setObj->setAttribute(ctx, ks, ctx->fromInteger(sz));
}

// ---------------------------------------------------------------------------
// Check whether val is in the Set (combines ProtoSet::has with SameValueZero
// scan for NaN/double edge cases).
// ---------------------------------------------------------------------------
static bool setContains(proto::ProtoContext* ctx,
                         const proto::ProtoObject* setObj,
                         const proto::ProtoObject* val)
{
    const proto::ProtoSet* core = getSetCore(ctx, setObj);
    if (!core) return false;
    if (core->has(ctx, val) == PROTO_TRUE) return true;

    // ProtoSet may not intern NaN or -0/+0 correctly for doubles.
    // Fall back to linear SameValueZero scan of __set_order__.
    if (val && (val->isDouble(ctx) || val->isFloat(ctx))) {
        const proto::ProtoSparseList* order = getSetOrder(ctx, setObj);
        if (!order) return false;
        const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            const proto::ProtoObject* existing = it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            if (setSameValueZero(ctx, existing, val)) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Set constructor: new Set(iterable?)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setConstruct(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self) return PROTO_NONE;

    const proto::ProtoSet* emptySet      = ctx->newSet();
    const proto::ProtoSparseList* emptyList = ctx->newSparseList();
    const proto::ProtoObject* setObj = self;
    setObj = setSetCore(ctx, setObj, emptySet);
    setObj = setSetOrder(ctx, setObj, emptyList);
    setObj = setSetSize(ctx, setObj, 0L);

    // If iterable argument provided, add each value.
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* iterable = args->getAt(ctx, 0);
        if (iterable && iterable != PROTO_NONE) {
            const proto::ProtoObject* lenKo = ctx->fromUTF8String("length");
            const proto::ProtoString* lenKs = lenKo ? lenKo->asString(ctx) : nullptr;
            if (lenKs) {
                const proto::ProtoObject* lenObj = iterable->getAttribute(ctx, lenKs, true);
                if (lenObj && lenObj != PROTO_NONE && lenObj->isInteger(ctx)) {
                    long len = lenObj->asLong(ctx);
                    for (long i = 0; i < len; i++) {
                        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                        if (!ik) continue;
                        const proto::ProtoObject* val = iterable->getAttribute(ctx, ik, true);
                        if (!val) val = PROTO_NONE;
                        if (!setContains(ctx, setObj, val)) {
                            const proto::ProtoSet* core  = getSetCore(ctx, setObj);
                            const proto::ProtoSparseList* order = getSetOrder(ctx, setObj);
                            long sz = getSetSize(ctx, setObj);
                            if (core)  core  = core->add(ctx, val);
                            if (order) order = order->setAt(ctx, static_cast<unsigned long>(sz), val);
                            if (core)  setObj = setSetCore(ctx, setObj, core);
                            if (order) setObj = setSetOrder(ctx, setObj, order);
                            setObj = setSetSize(ctx, setObj, sz + 1);
                        }
                    }
                }
            }
        }
    }
    return setObj;
}

// ---------------------------------------------------------------------------
// set.add(val) → set
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setAdd(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return self;
    const proto::ProtoObject* val = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!val) val = PROTO_NONE;

    if (setContains(ctx, self, val)) return self;

    const proto::ProtoSet* core  = getSetCore(ctx, self);
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    long sz = getSetSize(ctx, self);
    if (core)  core  = core->add(ctx, val);
    if (order) order = order->setAt(ctx, static_cast<unsigned long>(sz), val);
    const proto::ProtoObject* result = self;
    if (core)  result = setSetCore(ctx, result, core);
    if (order) result = setSetOrder(ctx, result, order);
    result = setSetSize(ctx, result, sz + 1);
    return result;
}

// ---------------------------------------------------------------------------
// set.has(val) → boolean
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setHas(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoObject* val = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!val) val = PROTO_NONE;
    return setContains(ctx, self, val) ? PROTO_TRUE : PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// set.delete(val) → boolean
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setDeleteFn(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoObject* val = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!val) val = PROTO_NONE;

    if (!setContains(ctx, self, val)) return PROTO_FALSE;

    const proto::ProtoSet* core  = getSetCore(ctx, self);
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    long sz = getSetSize(ctx, self);
    if (core)  core = core->remove(ctx, val);

    // Remove from order list: find matching index.
    if (order) {
        const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            unsigned long idx = it->nextKey(ctx);
            const proto::ProtoObject* existing = it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            if (setSameValueZero(ctx, existing, val)) {
                order = order->removeAt(ctx, idx);
                break;
            }
        }
    }

    const proto::ProtoObject* result = self;
    if (core)  result = setSetCore(ctx, result, core);
    if (order) result = setSetOrder(ctx, result, order);
    result = setSetSize(ctx, result, sz > 0 ? sz - 1 : 0);
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// set.clear() → undefined
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setClear(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* result = self;
    result = setSetCore(ctx, result, ctx->newSet());
    result = setSetOrder(ctx, result, ctx->newSparseList());
    result = setSetSize(ctx, result, 0L);
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// set.size (getter)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setSizeGetter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return ctx->fromInteger(0LL);
    return ctx->fromInteger(static_cast<long long>(getSetSize(ctx, self)));
}

// ---------------------------------------------------------------------------
// set.forEach(callback, thisArg?)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setForEach(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* callback = args->getAt(ctx, 0);
    if (!callback || callback == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* thisArg = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;

    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return PROTO_NONE;

    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        proto::ProtoList* callArgsList = ctx->newList();
        if (callArgsList) {
            callArgsList = const_cast<proto::ProtoList*>(callArgsList->append(ctx, v));
            callArgsList = const_cast<proto::ProtoList*>(callArgsList->append(ctx, v));
            callArgsList = const_cast<proto::ProtoList*>(callArgsList->append(ctx, self));
        }
        callFunction(ctx, callback, thisArg ? thisArg : PROTO_NONE, callArgsList, nullptr);
    }
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// Set iterator (values, keys=values, entries).
// ---------------------------------------------------------------------------
static const proto::ProtoObject* makeSetIterator(
    proto::ProtoContext* ctx, const proto::ProtoObject* setObj, const char* kind)
{
    const proto::ProtoObject* iter = ctx->newObject(true);
    const proto::ProtoString* tk = ctx->fromUTF8String("__iter_target__")->asString(ctx);
    const proto::ProtoString* pk = ctx->fromUTF8String("__iter_pos__")->asString(ctx);
    const proto::ProtoString* kk = ctx->fromUTF8String("__iter_kind__")->asString(ctx);
    if (tk) iter = iter->setAttribute(ctx, tk, setObj);
    if (pk) iter = iter->setAttribute(ctx, pk, ctx->fromInteger(0LL));
    if (kk) iter = iter->setAttribute(ctx, kk, ctx->fromUTF8String(kind));
    return iter;
}

static const proto::ProtoObject* setValues(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeSetIterator(ctx, self, "values"); }

static const proto::ProtoObject* setKeys(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeSetIterator(ctx, self, "values"); } // Set.keys() === Set.values()

static const proto::ProtoObject* setEntries(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeSetIterator(ctx, self, "entries"); }

} // namespace

// ---------------------------------------------------------------------------
// BuildSetPrototype and ensureSetConstructor
// ---------------------------------------------------------------------------
void BuildSetPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                       const proto::ProtoObject* objectProto)
{
    if (!space || !ctx || !objectProto) return;

    const proto::ProtoObject* setProto = objectProto->newChild(ctx, false);
    if (!setProto) return;

    auto installMethod = [&](const proto::ProtoObject*& proto, const char* name,
                             proto::NativeFunction fn) {
        const proto::ProtoObject* ko = ctx->fromUTF8String(name);
        const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
        if (!ks) return;
        const proto::ProtoObject* mObj = ctx->newObject(true);
        if (!mObj) return;
        mObj = ctx->fromMethod(mObj, fn);
        proto = proto->setAttribute(ctx, ks, mObj);
    };

    installMethod(setProto, "add",     setAdd);
    installMethod(setProto, "has",     setHas);
    installMethod(setProto, "delete",  setDeleteFn);
    installMethod(setProto, "clear",   setClear);
    installMethod(setProto, "forEach", setForEach);
    installMethod(setProto, "values",  setValues);
    installMethod(setProto, "keys",    setKeys);
    installMethod(setProto, "entries", setEntries);

    // size getter via __get_size__
    {
        const proto::ProtoObject* gko = ctx->fromUTF8String("__get_size__");
        const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
        if (gks) {
            const proto::ProtoObject* getter = ctx->newObject(true);
            getter = ctx->fromMethod(getter, setSizeGetter);
            setProto = setProto->setAttribute(ctx, gks, getter);
        }
    }

    // Store set prototype for constructor lookup.
    {
        const proto::ProtoObject* slotKey = ctx->fromUTF8String("__set_proto__");
        const proto::ProtoString* slotKs  = slotKey ? slotKey->asString(ctx) : nullptr;
        if (slotKs && space->objectPrototype)
            const_cast<proto::ProtoObject*>(space->objectPrototype)
                ->setAttribute(ctx, slotKs, setProto);
    }
}

void ensureSetConstructor(proto::ProtoContext* ctx,
                          const proto::ProtoObject** globalRoot)
{
    if (!ctx || !globalRoot || !*globalRoot) return;
    const proto::ProtoObject* ko = ctx->fromUTF8String("Set");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks) return;
    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, ks, false);
    if (existing && existing != PROTO_NONE) return;

    const proto::ProtoObject* methodProto = ctx->getMethodPrototype();
    const proto::ProtoObject* ctorParent  = methodProto ? methodProto : *globalRoot;
    const proto::ProtoObject* ctor        = ctorParent->newChild(ctx, true);
    if (!ctor) return;
    ctor = ctx->fromMethod(ctor, setConstruct);

    const proto::ProtoObject* setProtoSlotKey = ctx->fromUTF8String("__set_proto__");
    const proto::ProtoString* spsk = setProtoSlotKey ? setProtoSlotKey->asString(ctx) : nullptr;
    const proto::ProtoObject* setProto = (spsk && ctx->getRootObject())
        ? ctx->getRootObject()->getAttribute(ctx, spsk, false) : nullptr;

    if (setProto) {
        const proto::ProtoObject* protoKey = ctx->fromUTF8String("prototype");
        const proto::ProtoString* protoKs  = protoKey ? protoKey->asString(ctx) : nullptr;
        if (protoKs) ctor = ctor->setAttribute(ctx, protoKs, setProto);
        const proto::ProtoObject* constructKey = ctx->fromUTF8String("__construct__");
        const proto::ProtoString* constructKs  = constructKey ? constructKey->asString(ctx) : nullptr;
        if (constructKs) {
            const proto::ProtoObject* constructFn = ctx->newObject(true);
            constructFn = ctx->fromMethod(constructFn, setConstruct);
            ctor = ctor->setAttribute(ctx, constructKs, constructFn);
        }
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, ks, ctor);
}

} // namespace protojs
```

- [ ] **Step 3: Build and verify smoke tests**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS/.worktrees/feat-phases-33-34/build
cmake --build . -j$(nproc) 2>&1 | tail -5
```

```bash
cd /home/gamarino/Documentos/proyectos/protoJS/.worktrees/feat-phases-33-34
./build/protojs -e "
var s = new Set([1, 2, 3, 2, 1]);
print('size:', s.size);
print('has 2:', s.has(2));
print('has 5:', s.has(5));
s.delete(2);
print('size after delete:', s.size);
s.add(10);
print('size after add:', s.size);
"
```
Expected: `size: 3`, `has 2: true`, `has 5: false`, `size after delete: 2`, `size after add: 3`

### Task 5: Wire Map and Set into the build system

**Files:**
- Modify: `CMakeLists.txt:114-115` (add new source files)
- Modify: `src/JSPrototypes.cpp` (call BuildMapPrototype + BuildSetPrototype)
- Modify: `src/JSPrototypes.h` (no change needed)
- Modify: `src/runtime/ProtoInterpreter.cpp` (remove "Map"/"Set" from kUnimplementedCtors, add ensureMap/SetConstructor calls)

- [ ] **Step 1: Add new source files to CMakeLists.txt**

After line 114 (`src/BooleanPrototype.cpp`), add:
```cmake
    src/MapPrototype.cpp
    src/SetPrototype.cpp
```

- [ ] **Step 2: Add includes and calls to `src/JSPrototypes.cpp`**

Add `#include "MapPrototype.h"` and `#include "SetPrototype.h"` after the BooleanPrototype include.
Add `BuildMapPrototype(space, ctx, objectProto);` and `BuildSetPrototype(space, ctx, objectProto);` at the end of `BootstrapJSPrototypes`.

- [ ] **Step 3: Remove Map and Set from kUnimplementedCtors[] and add constructor registration**

In `src/runtime/ProtoInterpreter.cpp` at `kUnimplementedCtors[]` (~line 1095), remove `"Map"` and `"Set"` entries.

Find where `ensureBooleanConstructor` is called (~line 1139) and add calls after it:
```cpp
ensureMapConstructor(ctx, &globalRoot);
ensureSetConstructor(ctx, &globalRoot);
```

- [ ] **Step 4: Build the complete project**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS/.worktrees/feat-phases-33-34/build
cmake --build . -j$(nproc) 2>&1 | tail -10
```
Expected: `[100%] Built target protojs`

- [ ] **Step 5: Run combined smoke tests**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS/.worktrees/feat-phases-33-34
./build/protojs -e "
// Phase 33 check
var o = {};
Object.defineProperty(o, 'hidden', {value: 42, enumerable: false, writable: false, configurable: true});
Object.defineProperty(o, 'visible', {value: 99, enumerable: true, writable: true, configurable: true});
print('getOwnPropertyNames count:', Object.getOwnPropertyNames(o).length);
print('keys count:', Object.keys(o).length);

// Phase 34 Map check
var m = new Map([[1,'a'],[2,'b'],[3,'c']]);
print('map size:', m.size, 'get 2:', m.get(2));

// Phase 34 Set check
var s = new Set([1,2,3,2,1]);
print('set size:', s.size, 'has 3:', s.has(3));
"
```
Expected:
```
getOwnPropertyNames count: 2
keys count: 1
map size: 3 get 2: b
set size: 3 has 3: true
```

### Task 6: Phase 34 test262 snapshot and final commit

**Files:**
- Modify: `docs/TEST262_STATUS.md`

- [ ] **Step 1: Run test262 for Map and Set**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS/.worktrees/feat-phases-33-34
TEST262_ROOT=../../../test262 TEST262_USE_PROTO_EVAL=1 \
  TEST262_PATTERNS="built-ins/Map,built-ins/Set" \
  node tests/test262/runner/test262_runner.js 2>&1 | tail -30
```

- [ ] **Step 2: Run regression check on language/expressions**

```bash
TEST262_ROOT=../../../test262 TEST262_USE_PROTO_EVAL=1 \
  TEST262_PATTERNS="language/expressions" \
  node tests/test262/runner/test262_runner.js 2>&1 | tail -10
```

- [ ] **Step 3: Update `docs/TEST262_STATUS.md`**

Add Phase 34 section with actual counts. Include total improvement from Phase 32 baseline.

- [ ] **Step 4: Commit documentation and final state**

```bash
git add docs/TEST262_STATUS.md
git commit -m "docs: update TEST262_STATUS.md with Phase 34 Map+Set results"
```
