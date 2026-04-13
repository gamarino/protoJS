# Object Built-in Conformance — Design Spec (Phase 36)

**Goal:** Recover ~400–600 test262 tests by fixing three gaps in `Object` built-in conformance: property key coercion in `Object.defineProperty`/`defineProperties`, `Symbol.toStringTag` edge cases in `Object.prototype.toString`, and missing prototype-chain enumeration in `for...in`.

**Architecture:** Three independent tracks, each targeting a confirmed bug with no cross-track dependencies. All changes are confined to `src/ObjectPrototype.cpp` and `src/runtime/ProtoInterpreter.cpp`.

**Tech Stack:** C++20, protoCore ProtoSparseList, `__pd_<name>__` sidecar descriptor system (bit0=writable 0x1, bit1=configurable 0x2, bit2=enumerable 0x4), JSSymbols well-known symbol registry.

---

## Background

### Property Key Coercion (ToPropertyKey)

ECMAScript requires that the property name argument to `Object.defineProperty(target, P, Desc)` be converted via `ToPropertyKey(P)`, which calls `ToString()` on non-Symbol values. protoJS currently passes the raw value to `setAttribute` without coercion, so `Object.defineProperty(obj, false, {})` fails to store under key `"false"` and `Object.defineProperty(obj, 123, {})` fails to store under key `"123"`. This causes the entire `15.2.3.6-2-*` test cluster (~967 tests).

### Symbol.toStringTag in objectToString

`Object.prototype.toString` checks `__toStringTag__` (the internal key for `Symbol.toStringTag`) via `getAttribute(ctx, tagKey, true)`. This works for Map/Set because they set `__toStringTag__` explicitly. Edge cases that fail:
- When `Symbol.toStringTag` is deleted from a built-in prototype, `toString` should fall back to `[object Object]` — this should already work but needs verification.
- When the tag value is a non-string (e.g., a Symbol primitive or object), the spec requires ignoring it and using the built-in tag. The check `tagVal->isString(ctx)` handles this but the fallback logic needs verification.
- `Symbol.prototype[Symbol.toStringTag]` = `"Symbol"` — this tag must be set on `Symbol.prototype`.

### for...in Prototype Chain

`OP_for_in_start` in `ProtoInterpreter.cpp` collects properties using `getOwnAttributes()` only. It does not walk the prototype chain. ECMAScript requires `for...in` to enumerate all own and inherited enumerable string-keyed properties. The existing code has a comment claiming it walks the chain but the implementation does not. This causes failures when code iterates inherited properties.

---

## Track 1: Object.defineProperty / Object.defineProperties — ToPropertyKey

### Problem

`objectDefineProperty` and `objectDefineProperties` in `src/ObjectPrototype.cpp` receive the property name argument and pass it directly to attribute storage without calling `ToString()`. The ES spec mandates `ToPropertyKey(P)` which for non-Symbol values calls `ToString()`.

### Solution

At the beginning of `objectDefineProperty`, after extracting the property name argument, add a `ToPropertyKey` coercion step:

```cpp
// ToPropertyKey coercion: convert non-string property names to string.
// Spec: Object.defineProperty(O, P, Desc) step 3: Let key be ToPropertyKey(P).
if (propName && propName != PROTO_NONE && !propName->isString(ctx)) {
    // For numeric values, use the existing toUTF8 / asString path.
    std::string coerced;
    propName->asString(ctx)->toUTF8String(ctx, coerced);
    // Or use ctx->toString(propName) if available.
    propName = ctx->fromUTF8String(coerced.c_str());
}
```

The exact implementation depends on the protoCore API for value-to-string conversion. The pattern used elsewhere in ObjectPrototype.cpp (e.g., in `collectOwnKeys`) for extracting string keys should be reused.

Apply the same fix to `objectDefineProperties`.

### Expected Impact

Fixes ~967 `15.2.3.6-2-*` tests and additional `15.2.3.6-4-*` tests that use non-string keys.

---

## Track 2: Object.prototype.toString — Symbol.toStringTag Edge Cases

### Problem

`objectToString` in `src/ObjectPrototype.cpp` (lines 983–1059) handles `Symbol.toStringTag` but has gaps:

1. `Symbol.prototype[Symbol.toStringTag]` is not set — `typeof sym` is "symbol" but `Object.prototype.toString.call(sym)` returns `[object Object]` instead of `[object Symbol]`.
2. When `Symbol.toStringTag` returns a non-string value (accessor returning object/Symbol), the spec requires falling back to the built-in tag, not `[object Object]`. The `isString()` check handles this but the fallback path needs a built-in tag lookup.
3. Tests that delete `Symbol.toStringTag` from Map/Set prototype should already work (tag falls through to `[object Object]`) — verify.

### Solution

**Part A: Set `Symbol.prototype[Symbol.toStringTag]` = `"Symbol"`**

In whichever file sets up the Symbol prototype (search for `SymbolPrototype` or `symbolProto` in src/), add:
```cpp
const proto::ProtoString* tstKey = JSSymbols::toStringTag(ctx);
if (tstKey) {
    symProto = symProto->setAttribute(ctx, tstKey, ctx->fromUTF8String("Symbol"));
    // Non-writable, non-enumerable, configurable: bits = 0x2
    const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd___toStringTag____");
    const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
    if (pdks) symProto = symProto->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
}
```

**Part B: Built-in tag lookup in objectToString**

After the `isString` check returns false for a tag value, instead of falling through to `[object Object]`, add a built-in tag lookup that checks the object's type (already done for Array, Function, etc.) before the final default. This is already the structure of the function — verify the flow is correct and the `[object Object]` default is only reached when no built-in tag applies.

### Expected Impact

Fixes ~32 `built-ins/Object/prototype/toString/symbol-tag-*` tests.

---

## Track 3: for...in — Prototype Chain Enumeration

### Problem

`OP_for_in_start` in `src/runtime/ProtoInterpreter.cpp` (around lines 5321–5408) only collects own properties via `getOwnAttributes()`. It does not walk the prototype chain. ECMAScript requires `for (var k in obj)` to yield all own AND inherited enumerable string-keyed properties (excluding duplicates).

### Solution

After collecting own properties, walk the prototype chain:

```cpp
// Walk prototype chain for inherited enumerable properties.
// Use obj->getParent(ctx) or equivalent protoCore API.
std::set<std::string> seen(keys.begin(), keys.end());
const proto::ProtoObject* proto = fiObj->getParent(ctx);
while (proto && proto != PROTO_NONE && proto != globalRoot) {
    // Collect own enumerable properties of proto not already in seen.
    auto protoAttrs = proto->getOwnAttributes(ctx);
    for (auto& attr : protoAttrs) {
        std::string kstr = /* extract key string */;
        if (seen.count(kstr)) continue;      // already enumerated
        if (isInternalKey(kstr)) continue;   // skip __xxx__ internals
        // Check enumerable descriptor
        // ... same __pd_ check as for own properties ...
        if (enumerable) {
            keys.push_back(kstr);
            seen.insert(kstr);
        }
    }
    proto = proto->getParent(ctx);
}
```

The exact protoCore API for `getParent` and iterating own attributes must be confirmed by reading protoCore headers. The existing `OP_for_in_start` code already has the `__pd_` enumerable check pattern — reuse it for prototype properties.

**Deduplication:** Keys seen on the object take priority. If a property is shadowed (own non-enumerable shadows inherited enumerable), the inherited property must NOT be enumerated (this matches the ES spec).

### Expected Impact

Fixes an unknown number of `language/statements` tests that use inherited properties in `for...in` loops.

---

## File Structure

| File | Change |
|------|--------|
| `src/ObjectPrototype.cpp` | Track 1: ToPropertyKey coercion in `objectDefineProperty` and `objectDefineProperties`; Track 2: Symbol.toStringTag edge cases |
| `src/runtime/ProtoInterpreter.cpp` | Track 3: Prototype chain walk in `OP_for_in_start` |
| `src/SymbolPrototype.cpp` (or equivalent) | Track 2: Set `Symbol.prototype[Symbol.toStringTag] = "Symbol"` |

---

## Test Plan

After each track, run targeted tests:

```bash
# Track 1
TEST262_PATTERNS="built-ins/Object/defineProperty" node tests/test262/runner/test262_runner.js 2>&1 | tail -5
TEST262_PATTERNS="built-ins/Object/defineProperties" node tests/test262/runner/test262_runner.js 2>&1 | tail -5

# Track 2
TEST262_PATTERNS="built-ins/Object/prototype/toString" node tests/test262/runner/test262_runner.js 2>&1 | tail -5

# Track 3
TEST262_PATTERNS="language/statements/for-in" node tests/test262/runner/test262_runner.js 2>&1 | tail -5

# Regression check
TEST262_PATTERNS="language/expressions" node tests/test262/runner/test262_runner.js 2>&1 | tail -5
TEST262_PATTERNS="built-ins/Map" node tests/test262/runner/test262_runner.js 2>&1 | tail -3
TEST262_PATTERNS="built-ins/Set" node tests/test262/runner/test262_runner.js 2>&1 | tail -3
```

Expected results (Phase 35 baselines as floor):
- `built-ins/Map`: ≥ 139/204 (no regression)
- `built-ins/Set`: ≥ 259/383 (no regression)
- `language/expressions`: ≥ 9,416/11,036 (no regression)
- `built-ins/Object/defineProperty`: significant improvement over baseline
