# Design Specification: Object.defineProperty Full Protocol, Map, and Set

**Date:** 2026-04-12
**Topic:** Two sequential phases to recover ~800 test262 tests in built-ins conformance
**Status:** Approved

---

## Objective

Continue improving test262 pass rates in key built-ins areas:

1. **Phase 33** — Complete the `Object.defineProperty` descriptor protocol:
   `getOwnPropertyDescriptor`, `getOwnPropertyNames`, and `writable` enforcement in assignments.
2. **Phase 34** — Implement `Map` and `Set` using only protoCore data structures
   (`ProtoSet`, `ProtoSparseList`) as backing storage.

---

## Baseline (2026-04-12, Phase 32 snapshot)

| Area | Total | Passed | Pass % |
|------|------:|-------:|-------:|
| `built-ins/Object/defineProperty` | 1,131 | 342 | 30.2% |
| `built-ins/Object/defineProperties` | 632 | 192 | 30.4% |
| `built-ins/Object/getOwnPropertyDescriptor` | ~150 | ~50 | ~33% |
| `built-ins/Object/getOwnPropertyNames` | ~150 | ~30 | ~20% |
| `built-ins/Map` | ~530 | ~0 | ~0% |
| `built-ins/Set` | ~383 | ~0 | ~0% |

---

## Phase 33: Object.defineProperty Full Descriptor Protocol

### Problems identified

| Issue | Root cause | Impact |
|-------|-----------|--------|
| `getOwnPropertyDescriptor` returns `undefined` for props set to `undefined` | Line 741: `if (!val) return PROTO_NONE` — `getAttribute` returns `nullptr` both when key absent AND when value is `PROTO_NONE` | ~150 tests |
| `getOwnPropertyNames` returns only enumerable properties | It delegates to `objectKeys` which filters non-enumerable (bit 2 check) | ~120 tests |
| `writable=false` is not enforced on assignment | `OP_put_field` / `OP_set_field` do not read the sidecar before writing | ~200 tests |

### Fix 1: `objectGetOwnPropertyDescriptor`

**File:** `src/ObjectPrototype.cpp` — `objectGetOwnPropertyDescriptor` (~line 674)

The data property path (lines 736–758) must separate "key absent" from "value is undefined":

```cpp
// Check existence first — returns PROTO_TRUE only for own properties.
const proto::ProtoObject* ownFlag = target->hasOwnAttribute(ctx, pk);
if (ownFlag != PROTO_TRUE) return PROTO_NONE; // absent or inherited

// Key exists: read value (may be PROTO_NONE = undefined).
const proto::ProtoObject* val = target->getAttribute(ctx, pk, false);
const proto::ProtoObject* storedVal = val ? val : PROTO_NONE;
// ... build descriptor with storedVal
```

The current code does `if (!val) return PROTO_NONE` after `getAttribute`, which conflates
absence with undefined value. The fix always builds the descriptor once existence is confirmed.

### Fix 2: `objectGetOwnPropertyNames`

**File:** `src/ObjectPrototype.cpp` — `objectGetOwnPropertyNames` (~line 442)

Currently a stub that calls `objectKeys`. Replace with a dedicated implementation that
returns **all** own non-internal properties regardless of enumerable bit:

```cpp
static const proto::ProtoObject* objectGetOwnPropertyNames(ctx, ...) {
    // Same key-collection loop as objectKeys but WITHOUT the enumerable filter.
    // Still suppresses __*__ internal keys.
    // Returns an array of all own string-keyed property names.
}
```

Extract the shared key-collection logic into a static helper:

```cpp
static void collectOwnStringKeys(ctx, obj, bool includeNonEnumerable,
                                  std::vector<std::string>& out);
```

Both `objectKeys` and `objectGetOwnPropertyNames` call this helper.

**Note:** `std::vector<std::string>` is local to the function (stack-allocated, not
heap-retained); it is only used to build the protoCore result array and is immediately
discarded. This does not violate the "no C++ standard containers in persistent storage"
requirement.

### Fix 3: `writable` enforcement in `OP_put_field` / `OP_set_field`

**File:** `src/runtime/ProtoInterpreter.cpp` — `OP_put_field` dispatch (~line 3000)

Before writing to a property, check the sidecar:

```cpp
// Writable check (sidecar bit 0 = writable).
std::string pdKey = std::string("__pd_") + fieldName + "__";
const proto::ProtoObject* bitsObj = target->getAttribute(ctx, pdKey_sym, false);
if (bitsObj && bitsObj != PROTO_NONE && bitsObj->isInteger(ctx)) {
    uint8_t bits = static_cast<uint8_t>(bitsObj->asLong(ctx));
    if (!(bits & 0x1)) { // writable = false
        if (isStrictMode) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Cannot assign to read only property"));
            break;
        }
        // Sloppy mode: silent ignore — do NOT write.
        break;
    }
}
// ... proceed with setAttribute
```

Strict/sloppy mode detection: check the `isStrict` flag already tracked per bytecode module
(`nf.isStrict` or equivalent). If unavailable, default to throwing TypeError (conservative).

### Expected test recovery — Phase 33

~400 tests across `built-ins/Object/defineProperty`, `getOwnPropertyDescriptor`,
`getOwnPropertyNames`, and `language/expressions` (writable enforcement).

---

## Phase 34: Map and Set

### Design principle

All backing storage uses protoCore-managed structures only. No C++ standard containers
are retained beyond local function scope. The GC tracks all data through the ProtoObject
attribute graph.

### JS Map — backing storage

Four hidden attributes on the Map wrapper ProtoObject:

| Attribute | Type | Contents |
|-----------|------|---------|
| `__map_keys__` | `ProtoSparseList` | Insertion-order list: index → key ProtoObject |
| `__map_vals__` | `ProtoSparseList` | Insertion-order list: index → value ProtoObject (same index as keys) |
| `__map_hash__` | `ProtoSparseList` | Hash index: `szvHash(key)` → insertion index in keys/vals |
| `__map_size__` | integer ProtoObject | Count of live (non-deleted) entries |

**Hash function `szvHash(key)`** (SameValueZero-compatible):

```cpp
static unsigned long szvHash(ProtoContext* ctx, const proto::ProtoObject* key) {
    if (!key || key == PROTO_NONE)          return 0UL;
    if (key == PROTO_TRUE)                  return 1UL;
    if (key == PROTO_FALSE)                 return 2UL;
    if (key->isInteger(ctx))               return static_cast<unsigned long>(key->asLong(ctx) & 0x7FFFFFFF);
    if (key->isDouble(ctx) || key->isFloat(ctx)) {
        double d = key->asDouble(ctx);
        if (std::isnan(d))                  return 3UL; // NaN → fixed slot
        uint64_t bits; memcpy(&bits, &d, 8);
        return static_cast<unsigned long>(bits ^ (bits >> 32));
    }
    if (key->isString(ctx)) {
        const proto::ProtoString* ps = key->asString(ctx);
        return ps ? static_cast<unsigned long>(ps->getHash(ctx)) : 4UL;
    }
    // Object or other: use pointer identity.
    return static_cast<unsigned long>(
        reinterpret_cast<uintptr_t>(key) >> 3);
}
```

**Collision handling:** `__map_hash__` stores only the first insertion index for a given hash.
On collision (hash slot occupied by a different key), fall back to linear scan of `__map_keys__`.
For the typical case (no collisions) lookup is O(1) via the hash index.

**SameValueZero comparison:**

```cpp
static bool sameValueZero(ProtoContext* ctx,
                           const proto::ProtoObject* a,
                           const proto::ProtoObject* b) {
    if (a == b) return true;
    // NaN === NaN
    if (a && a->isDouble(ctx) && b && b->isDouble(ctx))
        if (std::isnan(a->asDouble(ctx)) && std::isnan(b->asDouble(ctx))) return true;
    // +0 === -0 (same double value)
    return false;
}
```

**Map operations:**

- `map.set(key, val)`: find existing slot via hash+scan; if found update `__map_vals__[idx]`;
  if not, append to `__map_keys__` and `__map_vals__` at `__map_size__`, update `__map_hash__`.
- `map.get(key)`: hash lookup → check key with `sameValueZero` → return value or `undefined`.
- `map.has(key)`: hash lookup → boolean.
- `map.delete(key)`: `removeAt` from both sparse lists + hash index; decrement size.
- `map.clear()`: replace all four hidden attrs with fresh empty instances; size = 0.
- `map.size` (getter): return `__map_size__`.

**Map iteration** (`keys()`, `values()`, `entries()`): return a JS iterator object with
`__iter_target__` (the Map) and `__iter_pos__` (integer index into `__map_keys__`). The
`next()` method advances the index, skipping holes left by `delete`, until exhausted.

### JS Set — backing storage

Three hidden attributes on the Set wrapper ProtoObject:

| Attribute | Type | Contents |
|-----------|------|---------|
| `__set_core__` | `ProtoSet` | Fast membership test (O(log n) via protoCore's immutable set) |
| `__set_order__` | `ProtoSparseList` | Insertion-order list: index → value ProtoObject |
| `__set_size__` | integer ProtoObject | Count of live entries |

**Set operations:**

- `set.add(val)`: check `__set_core__->has(val)`; if absent, `__set_core__ = ->add(val)`;
  append to `__set_order__` at current size; size++.
- `set.has(val)`: `__set_core__->has(val) == PROTO_TRUE`.
- `set.delete(val)`: `__set_core__ = ->remove(val)`; scan `__set_order__` and `removeAt`;
  size--.
- `set.clear()`: reset all three hidden attrs; size = 0.
- `set.size` (getter): return `__set_size__`.

**Note on ProtoSet equality:** `ProtoSet::has` uses protoCore's native object equality.
For primitives (integers, strings) protoCore interns the values so pointer equality implies
value equality. For `NaN` and `-0`/`+0` edge cases, the `__set_core__` lookup is augmented
by a `sameValueZero` scan of `__set_order__` when `has` returns false but the value
is a double (to catch `NaN` interning differences). This is a rare code path.

**Set iteration** (`values()`, `keys()` = `values()`, `entries()`): same iterator pattern
as Map, iterating `__set_order__` and skipping entries removed from `__set_core__`.

### Constructor

Both constructors accept an optional iterable argument:

```
new Map([[k1,v1],[k2,v2]])
new Set([v1, v2, v3])
```

The constructor reads the iterable argument using the following protocol (Symbol is not
yet fully implemented, so a fallback order applies):
1. If the argument has a `Symbol.iterator` / `@@iterator` attribute, call it to get an iterator.
2. Otherwise, if the argument has a numeric `length` property, treat it as an array-like
   and iterate indices `0..length-1`.
3. If absent or `undefined`, the collection is created empty.

For Map, each iterable element must be a two-element array-like `[key, value]`.
For Set, each element is added directly.

### Files

| File | Change |
|------|--------|
| `src/MapPrototype.h` | New — `BuildMapPrototype`, `ensureMapConstructor` declarations |
| `src/MapPrototype.cpp` | New — full Map implementation |
| `src/SetPrototype.h` | New — `BuildSetPrototype`, `ensureSetConstructor` declarations |
| `src/SetPrototype.cpp` | New — full Set implementation |
| `src/JSPrototypes.cpp` | Call `BuildMapPrototype` + `BuildSetPrototype` in bootstrap |
| `src/runtime/ProtoInterpreter.cpp` | Remove `"Map"`, `"Set"` from `kUnimplementedCtors[]`; call `ensureMapConstructor`, `ensureSetConstructor` |
| `CMakeLists.txt` | Add `src/MapPrototype.cpp`, `src/SetPrototype.cpp` |

### Expected test recovery — Phase 34

~400 tests in `built-ins/Map` + ~250 tests in `built-ins/Set`. **Total: ~650 tests.**

---

## Implementation Sequence

```
Phase 33  →  independent of Phase 34
Phase 34  →  independent of Phase 33
Recommended order: Phase 33 → Phase 34
```

Each phase ends with:
1. A targeted test262 snapshot run (`TEST262_USE_PROTO_EVAL=1`).
2. Update `docs/TEST262_STATUS.md` with the snapshot results.
3. A git commit `feat(phaseNN): <description>` + `docs: update TEST262_STATUS.md`.

---

## Deliverables Summary

| Phase | Files changed | Tests targeted |
|-------|--------------|----------------|
| Phase 33 | `src/ObjectPrototype.cpp`, `src/runtime/ProtoInterpreter.cpp` | ~400 |
| Phase 34 | `src/MapPrototype.cpp/.h`, `src/SetPrototype.cpp/.h`, `src/JSPrototypes.cpp`, `src/runtime/ProtoInterpreter.cpp`, `CMakeLists.txt` | ~650 |
| **Total** | | **~1,050** |
