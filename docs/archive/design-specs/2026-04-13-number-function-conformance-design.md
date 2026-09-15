# Number + Function Conformance — Design Spec (Phase 37)

**Goal:** Recover ~80-110 test262 tests by fixing property descriptors on Number constants and prototype methods, adding RangeError validation to `Number.prototype.toString`, fixing `Function.prototype.toString` to include the function name, and correcting descriptors on `name`/`length` in `wrapNativeFunction`.

**Architecture:** Two independent tracks: (1) Number conformance — radix validation, constant descriptors, prototype method descriptors; (2) Function conformance — `toString` name extraction, `wrapNativeFunction` descriptor fix. All changes confined to `src/NumberPrototype.cpp` and `src/FunctionPrototype.cpp`. No new files needed; `installNonEnumerableMethod` from Phase 35 is reused.

**Tech Stack:** C++20, protoCore ProtoSparseList/ProtoObject, JSSymbols, `__pd_<name>__` sidecar descriptor system (bit0=writable 0x1, bit1=configurable 0x2, bit2=enumerable 0x4).

---

## Baselines (measured 2026-04-13)

| Suite | Pattern | Passed | Failed | Total |
|-------|---------|--------|--------|-------|
| `built-ins/Number` | `built-ins/Number` | 68 | 270 | 338 |
| `built-ins/Function` | `built-ins/Function` | 209 | 300 | 509 |

---

## Track 1: Number Conformance

### Fix 1a — `Number.prototype.toString` radix RangeError

**File:** `src/NumberPrototype.cpp` ~line 105

**Problem:** When `radix` < 2 or > 36, the runtime silently converts it to 10 instead of throwing `RangeError`. ECMAScript (ES2015 §20.1.3.6 step 3) requires: "If radixMV < 2 or radixMV > 36, throw a RangeError exception."

**Fix:** Replace the silent fallback with:
```cpp
if (radix < 2 || radix > 36) {
    context->signalNativeException(
        makeNativeError(context, "RangeError",
            "toString() radix must be between 2 and 36"));
    return PROTO_NONE;
}
```

**Expected recovery:** ~15-20 tests (all `S15.7.4.2_A3_T*` variants and `radix-invalid-*` tests).

---

### Fix 1b — Number constructor constants: add `{writable:false, enumerable:false, configurable:false}` descriptors

**File:** `src/NumberPrototype.cpp`, `setConst` lambda in `ensureNumberConstructor` (~line 492)

**Problem:** `EPSILON`, `MAX_VALUE`, `MIN_VALUE`, `MAX_SAFE_INTEGER`, `MIN_SAFE_INTEGER`, `POSITIVE_INFINITY`, `NEGATIVE_INFINITY`, `NaN` are installed with plain `setAttribute` — no `__pd_<name>__` sidecar. ECMAScript requires these to be `{writable: false, enumerable: false, configurable: false}` → bits = `0x0`.

**Fix:** After each `setAttribute(ctx, nameStr, value)` call in `setConst`, add:
```cpp
std::string pdKey = "__pd_" + std::string(name) + "__";
const proto::ProtoObject* pko = ctx->fromUTF8String(pdKey.c_str());
const proto::ProtoString* pdk = pko ? pko->asString(ctx) : nullptr;
if (pdk) numberConstructor->setAttribute(ctx, pdk, ctx->fromInteger(0x0));
```

Since there are multiple constants, extract this into a `setConstWithDescriptor` lambda to avoid repetition.

**Expected recovery:** ~8 tests (one per constant: EPSILON, MAX_VALUE, MIN_VALUE, etc.).

---

### Fix 1c — `Number.prototype` methods: add `{writable:true, enumerable:false, configurable:true}` descriptors

**File:** `src/NumberPrototype.cpp`, `BuildNumberPrototype` function (~line 376)

**Problem:** `toString`, `toFixed`, `toExponential`, `toPrecision`, `valueOf` are installed without `__pd_<name>__` sidecars, making them enumerable by default. ECMAScript requires all built-in prototype methods to be non-enumerable.

**Fix:** Replace plain `setAttribute` calls with `installNonEnumerableMethod(ctx, proto, name, fn, argc)` from `PrototypeUtils.h` (introduced in Phase 35). This sets bits = `0x3` and also correctly sets `fn.length` and `fn.name` with `0x2` descriptors.

**Expected recovery:** ~10-15 tests (enumerable-property tests for each method).

---

## Track 2: Function Conformance

### Fix 2a — `Function.prototype.toString` must include function name

**File:** `src/FunctionPrototype.cpp`, `fnToString` function (~line 150)

**Problem:** The current implementation always returns `"function () { [native code] }"`, ignoring the `self` argument entirely. ECMAScript (ES2019 §19.2.3.5) requires:
- For native functions: `"function <name>() { [native code] }"`
- For bound functions: `"function () { [native code] }"`
- The function must be callable, otherwise throw `TypeError`.

**Fix — Step 1:** Add callable guard. If `self` is null/PROTO_NONE, signal `TypeError`.

**Fix — Step 2:** Extract the `name` attribute from `self`:
```cpp
std::string fnName;
const proto::ProtoString* nameKey = JSSymbols::name(ctx);
if (nameKey) {
    const proto::ProtoObject* nameVal = self->getAttribute(ctx, nameKey, false);
    if (nameVal && nameVal != PROTO_NONE && nameVal->isString(ctx)) {
        nameVal->asString(ctx)->toUTF8String(ctx, fnName);
    }
}
std::string result = "function " + fnName + "() { [native code] }";
return ctx->fromUTF8String(result.c_str());
```

**Note:** User-defined functions in this runtime do not have source text preservation — returning `[native code]` for all callables is acceptable. The key requirement is inserting the correct `name`.

**Expected recovery:** ~40-50 tests (`built-ins/Function/prototype/toString/*`).

---

### Fix 2b — `wrapNativeFunction`: add `{writable:false, enumerable:false, configurable:true}` to `name` and `length`

**File:** `src/FunctionPrototype.cpp`, `wrapNativeFunction` function (~line 254)

**Problem:** `name` and `length` properties are set with plain `setAttribute`, so they lack `__pd_name__` / `__pd_length__` sidecars. ECMAScript requires: `{writable: false, enumerable: false, configurable: true}` → bits = `0x2`.

**Note:** `installNonEnumerableMethod` (from Phase 35, `src/PrototypeUtils.cpp`) already sets these descriptors correctly when installing methods on prototypes. However, `wrapNativeFunction` creates standalone function wrapper objects and sets `name`/`length` directly. This fix targets those direct `setAttribute` calls.

**Fix:** After the existing `setAttribute` calls for `name` and `length`, add sidecar:
```cpp
// For "name":
const proto::ProtoObject* pdnk = ctx->fromUTF8String("__pd_name__");
const proto::ProtoString* pdn = pdnk ? pdnk->asString(ctx) : nullptr;
if (pdn) wrapper->setAttribute(ctx, pdn, ctx->fromInteger(0x2));

// For "length":
const proto::ProtoObject* pdlk = ctx->fromUTF8String("__pd_length__");
const proto::ProtoString* pdl = pdlk ? pdlk->asString(ctx) : nullptr;
if (pdl) wrapper->setAttribute(ctx, pdl, ctx->fromInteger(0x2));
```

**Expected recovery:** ~10-15 tests (immutability checks on `fn.name` and `fn.length`).

---

## File Structure

| File | Changes |
|------|---------|
| `src/NumberPrototype.cpp` | Fix 1a (radix RangeError), Fix 1b (constant descriptors), Fix 1c (prototype method descriptors) |
| `src/FunctionPrototype.cpp` | Fix 2a (toString name extraction), Fix 2b (name/length descriptor sidecars) |

---

## Test Plan

After each fix, run the targeted suite:

```bash
# Number
TEST262_PATTERNS="built-ins/Number" node tests/test262/runner/test262_runner.js 2>&1 | tail -5

# Function
TEST262_PATTERNS="built-ins/Function" node tests/test262/runner/test262_runner.js 2>&1 | tail -5

# Regression check
TEST262_PATTERNS="language/expressions" node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```

**Expected Phase 37 targets:**
| Suite | Baseline | Target |
|-------|---------|--------|
| `built-ins/Number` | 68/338 (20.1%) | ≥ 100/338 (≥29.6%) |
| `built-ins/Function` | 209/509 (41.1%) | ≥ 255/509 (≥50.1%) |
| `language/expressions` | 9418/11036 | ≥ 9418/11036 (no regression) |
