# Design Specification: Primitive Wrappers, Object.defineProperty Full Protocol, and Prototype Methods

**Date:** 2026-04-12
**Topic:** Three sequential phases to recover ~2,300 test262 tests in built-ins conformance
**Status:** Approved

---

## Objective

Raise test262 pass rates in key built-ins areas currently at 10–38% by fixing three interconnected root causes:

1. Primitive wrapper objects (`new Number`, `new String`, `new Boolean`) do not have the correct prototype chain.
2. `Object.defineProperty` implements only a subset of the ES2015 descriptor protocol.
3. `String.prototype` and `Number.prototype` methods fail because they depend on both of the above.

---

## Baseline (2026-04-12, Phase 29 targeted snapshot)

| Area | Total | Passed | Pass % |
|------|------:|-------:|-------:|
| `built-ins/Object/defineProperty` | 1,131 | 164 | 14.5% |
| `built-ins/Object/defineProperties` | 632 | 106 | 15.8% |
| `built-ins/String/prototype` | 1,073 | 405 | 37.7% |
| `built-ins/Function/prototype` | 309 | 137 | 44.3% |
| `built-ins/Number/prototype` | 168 | 17 | 10.1% |
| **Total targeted** | **3,313** | **829** | **25.0%** |

---

## Phase 30: Primitive Wrapper Prototype Chains

### Problem

`Object.getPrototypeOf(new Number(42))` returns `[object Object]` instead of `Number.prototype`.
This causes the entire `built-ins/Number/prototype` and most `built-ins/String/prototype` suites to fail.

**Root cause:** In `OP_call_constructor` for the `__number_ctor__` / `__string_ctor__` / `__boolean_ctor__`
markers, the constructed object is created as a child of `space->objectPrototype` rather than
`space->numberPrototype` / `space->stringPrototype` / `space->booleanPrototype`.

### Fix

**`src/runtime/ProtoInterpreter.cpp` — `OP_call_constructor` dispatch:**

- When the constructor marker is `__number_ctor__`, create the new object as
  `space->numberPrototype->newChild(ctx, true)` and store the numeric value in a
  `__primitive_value__` attribute.
- Same pattern for `__string_ctor__` → `space->stringPrototype` and
  `__boolean_ctor__` → `space->booleanPrototype`.

**`src/JSPrototypes.cpp` — Bootstrap:**

- Ensure `space->numberPrototype`, `space->stringPrototype`, and `space->booleanPrototype`
  are created as children of `space->objectPrototype` before any TypeBridge object construction.
- Install `Number.prototype`, `String.prototype`, `Boolean.prototype` as own properties of the
  `Number`, `String`, `Boolean` constructor objects respectively (fixes `String.hasOwnProperty('prototype')`).

**`src/runtime/TypeBridge.cpp` — `fromJSValue` boxing:**

- When boxing a JS number to a ProtoObject, if the context has a `numberPrototype`, use it as parent.

### Primitive value access

Methods on `Number.prototype` (e.g. `valueOf`, `toFixed`) must extract the primitive value from the
wrapper object. Convention: the value is stored at `__primitive_value__`. The method implementation
calls `obj->getAttribute(ctx, "__primitive_value__")` and unwraps to a C++ double or string.

### Expected test recovery

~200 tests across `built-ins/Number/prototype` (151 total), `built-ins/Boolean` (partial),
and `Object.getPrototypeOf` related tests in `built-ins/Object`.

---

## Phase 31: Object.defineProperty Full Protocol

### Problems identified

| Issue | Impact |
|-------|--------|
| Key coercion not applied (`undefined` → `"undefined"`) | All tests passing non-string keys fail |
| Sidecar `__pd_<prop>__` stores only bit 0 (writable) | `configurable`/`enumerable` not enforced |
| `Object.getOwnPropertyDescriptor` returns wrong/missing descriptor | ~300+ tests |
| `Object.defineProperties` delegates to broken `defineProperty` | ~526 tests |
| Accessor/data conflict validation absent | Tests expecting `TypeError` pass incorrectly |
| Reconfiguring a non-configurable property should throw | Tests expecting `TypeError` pass incorrectly |

### Sidecar bit layout (extended)

The existing `__pd_<prop>__` integer attribute is extended to a full bit field:

```
bit 0: writable      (0 = not writable, default 0 for data descriptors)
bit 1: enumerable    (0 = not enumerable, default 0)
bit 2: configurable  (0 = not configurable, default 0)
bit 3: is_accessor   (1 = get/set descriptor; 0 = data descriptor)
bit 4: has_value     (1 = value was explicitly set)
bit 5: has_writable  (1 = writable was explicitly set)
bit 6: has_get       (1 = get was explicitly set)
bit 7: has_set       (1 = set was explicitly set)
```

Getters and setters continue to be stored in `__get_<prop>__` / `__set_<prop>__` (already implemented).

### Key coercion

At the top of `objectDefineProperty(ctx, obj, keyVal, descVal)`:
```cpp
std::string key = jsValueToString(ctx, keyVal);  // coerces undefined → "undefined"
```

### Object.getOwnPropertyDescriptor

Returns a fresh JS object with the correct fields:

- **Data descriptor:** `{ value, writable, enumerable, configurable }`
- **Accessor descriptor:** `{ get, set, enumerable, configurable }`

Reads the sidecar bits and the stored value / getter / setter.

### Object.defineProperties

Iterate the `Properties` object keys and call the updated `objectDefineProperty` for each.
Must handle `null`/`undefined` first argument with a `TypeError` (already in Phase 29; verify it holds).

### Validation

- **Accessor + data conflict:** if both `value`/`writable` and `get`/`set` are present → `TypeError`.
- **Non-configurable reconfiguration:** if the sidecar shows `configurable = 0` and the caller
  attempts to change `configurable`, `enumerable`, or `writable` from false → true → `TypeError`.
- **Non-writable value change:** if `writable = 0` and caller sets a different `value` → `TypeError`
  in strict mode; silent ignore in sloppy mode.

### Expected test recovery

~967 `Object/defineProperty` + ~526 `Object/defineProperties` + ~183 `Object/prototype` methods
that test enumeration and `hasOwnProperty` on defined properties.
**Total: ~1,500 tests.**

---

## Phase 32: String / Number / Boolean Prototype Methods

### Dependencies

Phase 32 assumes Phase 30 is complete (primitive wrapper `__primitive_value__` available).

### String.prototype fixes

**Root causes:**

1. `String.hasOwnProperty('prototype')` → `TypeError: is not a function`.
   Fix: `String` constructor must have `prototype` as own data property (handled in Phase 30 bootstrap).

2. Methods like `String.prototype.trim`, `String.prototype.split`, `String.prototype.indexOf`
   fail when called on a `String` wrapper object because they try to use QuickJS string methods
   on a ProtoObject that holds the value in `__primitive_value__`.
   Fix: each String method must extract the primitive string via `__primitive_value__` before
   performing the operation.

3. `String.prototype.split` with a RegExp argument crashes. Root cause: `RegExp` constructor
   interaction with the protoCore path. Fix: guard with `shouldUseProtoCore()` check; if `split`
   receives a RegExp argument, fall back to QuickJS's native `String.prototype.split`.

**Methods to audit/fix:** `trim`, `trimStart`, `trimEnd`, `split`, `indexOf`, `lastIndexOf`,
`substring`, `slice`, `includes`, `search`, `match`, `replace`, `toLowerCase`, `toUpperCase`,
`charAt`, `charCodeAt`, `concat`, `matchAll`.

### Number.prototype fixes

Methods that need `__primitive_value__`:
- `valueOf()` → return `__primitive_value__` as a JS number
- `toString(radix)` → convert the stored double to string with given radix
- `toFixed(digits)` → format with fixed decimal places
- `toPrecision(precision)` → format with total significant digits
- `toExponential(fractionDigits)` → scientific notation

### Boolean.prototype fixes

- `valueOf()` → return `__primitive_value__` (boolean)
- `toString()` → `"true"` or `"false"`

### Function.prototype residual fixes

~172 remaining failures in `built-ins/Function/prototype`. These are likely:
- `Function.prototype.toString()` — returns source text or `"function () { [native code] }"`
- `Function.prototype[Symbol.hasInstance]` — `instanceof` conformance

### Expected test recovery

~668 `String/prototype` + ~151 `Number/prototype` + ~30 `Boolean` + ~100 `Function/prototype` partial.
**Total: ~600–800 tests.**

---

## Implementation Sequence

```
Phase 30  →  Phase 32 (depends on 30)
Phase 31  →  independent, can run in parallel with 30
```

Recommended order: Phase 30 → Phase 31 → Phase 32.

Each phase ends with:
1. A targeted test262 snapshot run (`TEST262_USE_PROTO_EVAL=1`).
2. Update `docs/TEST262_STATUS.md` with the snapshot results.
3. A git commit with message `feat(phaseNN): <description>` + a docs commit `docs: update TEST262_STATUS.md with Phase NN results`.

---

## Deliverables Summary

| Phase | Files changed | Tests targeted |
|-------|--------------|----------------|
| Phase 30 | `src/JSPrototypes.cpp`, `src/runtime/ProtoInterpreter.cpp`, `src/runtime/TypeBridge.cpp` | ~200 |
| Phase 31 | `src/ObjectPrototype.cpp`, `src/runtime/ProtoInterpreter.cpp` | ~1,500 |
| Phase 32 | `src/StringPrototype.cpp`, `src/NumberPrototype.cpp`, `src/JSPrototypes.cpp` | ~600 |
| **Total** | | **~2,300** |
