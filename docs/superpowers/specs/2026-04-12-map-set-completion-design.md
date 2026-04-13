# Map/Set Completion — Design Spec (Phase 35)

**Goal:** Recover ~360 test262 tests by fixing property descriptors on Map/Set prototype methods and implementing missing methods (`getOrInsert`, `getOrInsertComputed`, `groupBy`, Set collection methods, Symbol.iterator, Symbol.toStringTag).

**Architecture:** Two parallel tracks: (1) a shared `PrototypeUtils` helper that installs non-enumerable methods with correct `length`/`name` descriptors, (2) new method implementations in MapPrototype.cpp and SetPrototype.cpp.

**Tech Stack:** C++20, protoCore ProtoSparseList/ProtoSet, JSSymbols convention, `__pd_<name>__` sidecar descriptor system.

---

## Track 1: Property Descriptor Fix

### Problem

protoCore stores property descriptor flags in a sidecar attribute named `__pd_<propertyName>__` as a packed integer:
- bit 0 = writable (0x1)
- bit 1 = configurable (0x2)
- bit 2 = enumerable (0x4)

When no sidecar exists, all bits default to true (enumerable). All Map/Set prototype methods are currently installed via plain `setAttribute`, so they are enumerable. ECMAScript requires all built-in prototype methods to be non-enumerable (`{writable: true, enumerable: false, configurable: true}` → bits = 0x3). Function `length` and `name` properties must be `{writable: false, enumerable: false, configurable: true}` → bits = 0x2.

### Solution: `installNonEnumerableMethod`

Create `src/PrototypeUtils.h` and `src/PrototypeUtils.cpp` with a single utility function:

```cpp
// Installs fn as a non-enumerable, configurable, writable method on proto.
// Also sets fn.length = argc and fn.name = name, both non-enumerable.
// Returns the updated proto pointer.
const proto::ProtoObject* installNonEnumerableMethod(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proto,
    const char* name,
    proto::ProtoMethod fn,
    int argc);
```

Internal steps:
1. `methodObj = ctx->fromMethod(nullptr, fn)`
2. Set `methodObj.length = argc` with sidecar `__pd_length__ = 0x2`
3. Set `methodObj.name = name` with sidecar `__pd_name__ = 0x2`
4. Set `proto.<name> = methodObj` with sidecar `__pd_<name>__ = 0x3`
5. Return updated `proto`

Since all prototype objects are mutable (`newObject(true)` or `objectProto->newChild(ctx, true)`), `setAttribute` modifies in place; the returned pointer must still be used in case of internal reallocation.

### Apply To

- `src/MapPrototype.cpp` — replace the `installMethod` lambda with calls to `installNonEnumerableMethod`
- `src/SetPrototype.cpp` — same

Array/String/Number prototypes have the same enumerable issue but are **out of scope** for this phase.

### Symbol Properties (Map and Set)

`Symbol.iterator` and `Symbol.toStringTag` require special treatment since they use Symbols as keys. protoJS represents well-known symbols via dedicated `JSSymbols::` accessors that return `ProtoString*`. The existing pattern uses `JSSymbols::symbolIterator(ctx)` (or equivalent). These are installed as non-enumerable via the same sidecar mechanism but using the symbol-keyed attribute directly.

- `Map.prototype[Symbol.iterator]` = same function object as `Map.prototype.entries`; descriptor: `{writable: true, enumerable: false, configurable: true}`
- `Map.prototype[Symbol.toStringTag]` = string `"Map"`; descriptor: `{writable: false, enumerable: false, configurable: true}` → bits = 0x2

Same pattern for Set with `"Set"` and `Symbol.iterator = values`.

---

## Track 2: Missing Map Methods

### `Map.prototype.getOrInsert(key, defaultValue)`

Spec (Stage 4 proposal, shipping in ES2025):
- If `this` has entry for `key` (SameValueZero), return existing value.
- Else, normalize key (-0 → +0), call `this.set(key, defaultValue)`, return `defaultValue`.

Implementation: reuse `mapFind` + `mapSet` logic inline. `length = 2`, `name = "getOrInsert"`.

### `Map.prototype.getOrInsertComputed(key, callbackFn)`

- If `this` has entry for `key`, return existing value.
- Else, normalize key, call `callbackFn()` (no arguments), set the result, return it.
- If `callbackFn` throws, propagate exception via `hasCallException()`.

Implementation: same as `getOrInsert` but call `callJSFunction(ctx, callbackFn, PROTO_NONE, emptyArgs)`. `length = 2`, `name = "getOrInsertComputed"`.

### `Map.groupBy(iterable, keyFn)` (static)

- Iterates `iterable` using the array-like length+index protocol (consistent with current protoJS iterator model).
- For each element `v`, calls `keyFn(v, index)`.
- Groups elements: result Map maps `key → [v1, v2, ...]` (each group is a JS Array).
- Returns the grouped Map.

Implementation: installed on the constructor object (not on `Map.prototype`). `length = 2`, `name = "groupBy"`.

### SameValueZero `-0` Normalization

The spec requires: "If key is −0, let key be +0." This must happen in `mapSet` and `setAdd` before any storage or lookup.

Fix: add a `normalizeKey` inline helper that converts double `-0.0` → `+0.0` (store as integer 0 via `ctx->fromInteger(0LL)`).

---

## Track 3: Missing Set Methods

### Set Collection Methods (ES2025)

All seven methods follow the same pattern: validate `this` has Set data, validate `other` is iterable (array-like), compute result, return new Set constructed with `ctx->newObject(true)` inheriting from `s_setPrototype`.

All methods: `length = 1`, non-enumerable.

**`Set.prototype.union(other)`**
- Create result Set copying all elements of `this`.
- Iterate `other`; add each element to result.

**`Set.prototype.intersection(other)`**
- Create empty result Set.
- Iterate `this`; for each element, if it is in `other`, add to result.

**`Set.prototype.difference(other)`**
- Create result Set copying all elements of `this`.
- Iterate `other`; remove each element from result if present.

**`Set.prototype.symmetricDifference(other)`**
- Start with union.
- Remove elements that are in both `this` and `other`.

**`Set.prototype.isSubsetOf(other)`**
- Return `true` if every element of `this` is in `other`.

**`Set.prototype.isSupersetOf(other)`**
- Return `true` if every element of `other` is in `this`.

**`Set.prototype.isDisjointFrom(other)`**
- Return `true` if no element of `this` is in `other`.

### `Symbol.toStringTag` and `Symbol.iterator`

- `Set.prototype[Symbol.toStringTag]` = `"Set"`, non-writable, non-enumerable, configurable.
- `Set.prototype[Symbol.iterator]` = same function as `Set.prototype.values`, non-enumerable.

---

## File Structure

| File | Change |
|------|--------|
| `src/PrototypeUtils.h` | New — declares `installNonEnumerableMethod` |
| `src/PrototypeUtils.cpp` | New — implements `installNonEnumerableMethod` |
| `src/MapPrototype.cpp` | Replace `installMethod` lambda; add `getOrInsert`, `getOrInsertComputed`, `groupBy`, `Symbol.iterator`, `Symbol.toStringTag`, `-0` normalization |
| `src/SetPrototype.cpp` | Replace `installMethod` lambda; add 7 collection methods, `Symbol.iterator`, `Symbol.toStringTag`, `-0` normalization in `setAdd` |
| `CMakeLists.txt` | Add `src/PrototypeUtils.cpp` |

---

## Test Plan

After each implementation task, run targeted test262 patterns:

```bash
TEST262_PATTERNS="built-ins/Map" node tests/test262/runner/test262_runner.js
TEST262_PATTERNS="built-ins/Set" node tests/test262/runner/test262_runner.js
```

Regression check:
```bash
TEST262_PATTERNS="language/expressions" node tests/test262/runner/test262_runner.js
```

Expected final results:
- Map: ≥175/204 (≥85%)
- Set: ≥345/383 (≥90%)
- `language/expressions`: ≥9,416/11,036 (no regression)
