# Design Specification: Array Conformance — Isolated Bug Fixes

**Date:** 2026-04-05
**Topic:** Raising `built-ins/Array` Test262 conformance from 96.1% to ~99%
**Status:** Approved

---

## Objective

Fix three isolated bugs responsible for ~116 of the 120 remaining failures in `built-ins/Array`.
The remaining ~4 failures depend on `Reflect.construct` and `Intl` and are explicitly deferred.

| Bug | Tests recovered | Root cause |
|-----|----------------:|------------|
| `Object.prototype.toString` wrong tag | ~105 | `objectToString` hardcoded to `"[object Object]"` |
| Array literal length wrong for >32 elements | ~3–5 | `OP_define_field` does not update `.length` |
| `String()` as conversion function returns `undefined` | ~5–7 | String constructor not callable as function |
| **Total** | **~116** | |

---

## Process

Diagnosis-first: root causes were confirmed by running the Test262 `built-ins/Array` snapshot
and reproducing each failure manually with `./build/protojs`. One commit per fix.

---

## Fix 1: `Object.prototype.toString` — correct type tag

### Root cause

`objectToString` in `src/ObjectPrototype.cpp` ignores its `self` parameter and always returns
`"[object Object]"`. Tests use the pattern:

```js
arr.getClass = Object.prototype.toString;
if (arr.getClass() !== "[object Array]") throw ...
```

When `getClass()` is called, `OP_call_method` dispatches to `objectToString` with `self = arr`,
but the return value is always `"[object Object]"`.

### Fix

Add type-detection logic to `objectToString`. Detection order (first match wins):

1. `self` is `null` or `PROTO_NONE` → `"[object Null]"`
2. `self` is boolean → `"[object Boolean]"`
3. `self` is integer or double → `"[object Number]"`
4. `self` is string → `"[object String]"`
5. `self` has `__bytecode_id__` attribute OR `self->isMethod(ctx)` → `"[object Function]"`
6. `self` has `__arrayCtor__` attribute (set by the Array constructor marker) OR parent chain
   includes `__arrayProto__` → `"[object Array]"`
7. `self` has `__regexp_bc__` attribute (set in `RegExpPrototype.cpp`) → `"[object RegExp]"`
8. Default → `"[object Object]"`

For the array check: use `JSSymbols::arrayCtor(ctx)` to look up the marker attribute on `self`
with `getAttribute(..., false)` (own only). If not found, also check `JSSymbols::arrayProto(ctx)`
— look up `__arrayProto__` on the global object and compare with `self`'s parent chain.
Simplest reliable check: `self->getAttribute(ctx, JSSymbols::arrayCtor(ctx), false) == PROTO_TRUE`
(the same flag used in `OP_call` to detect array constructors) — but this is set on the Array
constructor, not on array instances. Instead, check if `self` is a child of `__arrayProto__`:
look up `__arrayProto__` from the global root (accessible via `t_currentGlobalRoot` or passed
as a parameter), then check `self->getAttribute(ctx, JSSymbols::length(ctx), false) != PROTO_NONE`
combined with the `__arrayProto__` ancestry.

**Simpler and sufficient:** add a `__is_array__` marker to every array created in
`OP_array_from` and `createNewArray`, then check for that marker in `objectToString`.

**Chosen approach:** Add `__is_array__ = true` marker at array creation time.

- In `OP_array_from` (`src/runtime/ProtoInterpreter.cpp`): after setting length, set
  `arr = arr->setAttribute(pContext, JSSymbols::isArray(pContext), PROTO_TRUE)`.
- In `createNewArray` (`src/ArrayPrototype.cpp`): after creating the object, set the marker.
- In `objectToString` (`src/ObjectPrototype.cpp`): check `self->getAttribute(ctx, JSSymbols::isArray(ctx), false) == PROTO_TRUE` → return `"[object Array]"`.
- Add `isArray` symbol to `src/JSSymbols.h` and `src/JSSymbols.cpp`.

**Files changed:** `src/ObjectPrototype.cpp`, `src/ArrayPrototype.cpp`,
`src/runtime/ProtoInterpreter.cpp`, `src/JSSymbols.h`, `src/JSSymbols.cpp`.

---

## Fix 2: `OP_define_field` updates array `.length` for numeric indices

### Root cause

QuickJS compiles array literals using `OP_array_from` for the first 32 elements, then
`OP_define_field` with explicit atom indices for elements 33+. `OP_define_field` calls
`setAttribute` but never updates `.length`. As a result, a 33-element literal has `.length = 32`,
and `arraySort` only iterates `i < length`, missing the last element.

Confirmed: `["a", ..., "G"]` (33 elements) reports `length = 32`, with `arr[32] = "G"` accessible
but excluded from sort.

### Fix

In the `OP_define_field` handler in `src/runtime/ProtoInterpreter.cpp`, after setting the
attribute, detect if the key is a pure numeric string (all digits). If yes:

```
uint32_t idx = parseNumericKey(key);   // returns UINT32_MAX if not numeric
if (idx != UINT32_MAX) {
    const proto::ProtoString* lenKey = JSSymbols::length(pContext);
    const proto::ProtoObject* curLenObj = newObj->getAttribute(pContext, lenKey, false);
    long long curLen = (curLenObj && curLenObj->isInteger(pContext)) ? curLenObj->asLong(pContext) : 0;
    if ((long long)(idx + 1) > curLen) {
        newObj = newObj->setAttribute(pContext, lenKey, pContext->fromInteger((long long)(idx + 1)));
        updateMapping(pContext, obj, newObj);  // keep alias map in sync
        stackTop = newObj;
    }
}
```

A helper `parseNumericKey(const proto::ProtoString* key, proto::ProtoContext* ctx)` converts the
interned string to a `uint32_t` index, returning `UINT32_MAX` if non-numeric.

**Files changed:** `src/runtime/ProtoInterpreter.cpp`.

---

## Fix 3: `String()` as a conversion function

### Root cause

`String(42)` returns `undefined` because the `String` constructor is registered as a plain
object (`typeof String === "object"`), not as a callable function. When `OP_call` encounters it,
none of the dispatch paths match and it falls through to PROTO_NONE.

The `Array` constructor already has a workaround: `__array_ctor__ = true` marker detected in
`OP_call`. The same pattern is needed for `String`.

### Fix

Apply the same `__string_ctor__ = true` marker pattern:

1. In `src/JSPrototypes.cpp` (or wherever `String` is registered on the global object), add:
   ```cpp
   stringCtorObj = stringCtorObj->setAttribute(ctx, JSSymbols::stringCtor(ctx), PROTO_TRUE);
   ```
2. Add `stringCtor` symbol to `src/JSSymbols.h` and `src/JSSymbols.cpp`.
3. In `OP_call` in `src/runtime/ProtoInterpreter.cpp`, add a detection block (similar to the
   existing `__array_ctor__` block):
   ```cpp
   const proto::ProtoObject* isStringCtor =
       func->getAttribute(pContext, JSSymbols::stringCtor(pContext), false);
   if (isStringCtor == PROTO_TRUE) {
       const proto::ProtoObject* arg = (argc > 0) ? stackAt(pContext, argc - 1) : PROTO_NONE;
       for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
       stackPush(pContext, toString(pContext, arg));  // reuse existing toString() helper
       break;
   }
   ```

`toString(pContext, val)` is the existing helper in `ProtoInterpreter.cpp` that converts any
protoObject to its string representation.

**Files changed:** `src/JSPrototypes.cpp`, `src/runtime/ProtoInterpreter.cpp`,
`src/JSSymbols.h`, `src/JSSymbols.cpp`.

---

## Verification and Done Criteria

A fix is **done** when:
- The targeted failing tests now pass locally (run `TEST262_PATTERNS="built-ins/Array" node tests/test262/runner/test262_runner.js 2>/dev/null`).
- No previously-passing tests regress (zero new failures).
- `docs/TEST262_STATUS.md` is updated with the new snapshot.

After all three fixes, the expected result is `built-ins/Array` ≥ 99% (≥ 3,047 / 3,081).
The remaining ~4 failures (`is-a-constructor`, `toLocaleString`) require `Reflect.construct`
and `Intl` respectively and are explicitly deferred.

---

## Commit Discipline

One commit per fix:
- `fix(object): make Object.prototype.toString return correct type tag`
- `fix(interpreter): update array length in OP_define_field for numeric indices`
- `fix(string): make String() callable as a conversion function`

---

## Deferred

| Feature | Tests | Reason |
|---------|------:|--------|
| `Reflect.construct` | 1 | Needs full Reflect API |
| `Intl` / `toLocaleString` | 2–3 | Large separate effort |
