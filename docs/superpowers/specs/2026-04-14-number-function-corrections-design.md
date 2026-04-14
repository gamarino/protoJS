# Number + Function Corrections — Design Spec (Phase 38)

**Goal:** Recover ~100-120 test262 tests by correcting `Number.MIN_VALUE` to the proper subnormal value, adding RangeError in `toFixed`, making `Function.prototype` built-in methods non-enumerable, and fixing `Function.prototype.bind` length type handling.

**Architecture:** Two tracks — (1) Number corrections in `src/NumberPrototype.cpp`; (2) Function corrections in `src/FunctionPrototype.cpp`. All fixes are independent. `installNonEnumerableMethod` from `PrototypeUtils.h` (Phase 35) is reused for Function.prototype methods.

**Tech Stack:** C++20, protoCore ProtoSparseList/ProtoObject, JSSymbols, `__pd_<name>__` sidecar descriptor system (bit0=writable 0x1, bit1=configurable 0x2, bit2=enumerable 0x4).

---

## Baselines (measured 2026-04-14)

| Suite | Passed | Total | Pass Rate |
|-------|--------|-------|-----------|
| `built-ins/Number` | 101 | 338 | 29.9% |
| `built-ins/Function` | 213 | 509 | 41.8% |
| `language/expressions` | 9419 | 11036 | 85.3% |

---

## Track 1: Number Corrections

### Fix 1a — `Number.MIN_VALUE` uses wrong C++ constant

**File:** `src/NumberPrototype.cpp` ~line 500

**Problem:** `setConst("MIN_VALUE", std::numeric_limits<double>::min())` sets `Number.MIN_VALUE` to `2.2250738585072014e-308` (the smallest positive *normal* double). ECMAScript specifies `Number.MIN_VALUE` = `5e-324`, which is the smallest positive *subnormal* double = `std::numeric_limits<double>::denorm_min()`.

This is now a high-priority fix: Phase 37 added a non-configurable, non-writable descriptor (`0x0`) to all Number constants, so the wrong value is locked and cannot be corrected at runtime.

**Fix:**
```cpp
setConst("MIN_VALUE", std::numeric_limits<double>::denorm_min());
```

**Expected recovery:** ~5-10 tests (Number.MIN_VALUE descriptor and value tests).

---

### Fix 1b — `Number.prototype.toFixed` missing RangeError for out-of-range `fractionDigits`

**File:** `src/NumberPrototype.cpp`, `numberToFixed` function

**Problem:** When `fractionDigits` is outside `[0, 100]`, the code silently clamps to 0 instead of throwing `RangeError`. ECMAScript §20.1.3.3 step 3 requires: "If fractionDigits < 0 or fractionDigits > 100, throw a RangeError exception."

**Current code** (lines ~158-160):
```cpp
if (fractionDigits < 0 || fractionDigits > 100) {
    fractionDigits = 0;
}
```

**Fix:**
```cpp
if (fractionDigits < 0 || fractionDigits > 100) {
    signalNativeException(makeNativeError(context, "RangeError",
        "Number.prototype.toFixed() fractionDigits must be between 0 and 100"));
    return PROTO_NONE;
}
```

**Expected recovery:** ~12 tests (all `toFixed` RangeError tests).

---

## Track 2: Function Corrections

### Fix 2a — `Function.prototype` methods missing non-enumerable descriptors

**File:** `src/FunctionPrototype.cpp`, `ensureFunctionPrototype` function (~lines 237-244)

**Problem:** The `reg` lambda installs `call`, `apply`, `bind`, `toString` with plain `setAttribute` — no `__pd_<name>__` sidecar. ECMAScript §19.2.3 requires all built-in prototype methods to be `{writable: true, enumerable: false, configurable: true}` → bits = `0x3`.

**Current code:**
```cpp
auto reg = [&](const proto::ProtoString* key, proto::ProtoMethod fn) {
    if (key) fp = fp->setAttribute(ctx, key, ctx->fromMethod(nullptr, fn));
};

reg(JSSymbols::call(ctx),     fnCall);
reg(JSSymbols::apply(ctx),    fnApply);
reg(JSSymbols::bind(ctx),     fnBind);
reg(JSSymbols::toString(ctx), fnToString);
```

**Fix:** Replace with `installNonEnumerableMethod` calls:
```cpp
fp = installNonEnumerableMethod(ctx, fp, "call",     fnCall,     1);
fp = installNonEnumerableMethod(ctx, fp, "apply",    fnApply,    2);
fp = installNonEnumerableMethod(ctx, fp, "bind",     fnBind,     1);
fp = installNonEnumerableMethod(ctx, fp, "toString", fnToString, 0);
```

**Note:** Also add `#include "PrototypeUtils.h"` to `FunctionPrototype.cpp` if not already present.

**Also:** Add `name` and `length` descriptors to the `Function` constructor object created in `ensureFunctionPrototype` (~lines 262-269):
```cpp
// After setting fnCtor name and prototype:
// Function.length = 1 (the Function constructor takes 1 arg)
const proto::ProtoString* lenKey = JSSymbols::length(ctx);
if (lenKey) {
    fnCtor = fnCtor->setAttribute(ctx, lenKey, ctx->fromInteger(1LL));
    const proto::ProtoObject* pdlko = ctx->fromUTF8String("__pd_length__");
    const proto::ProtoString* pdlk = pdlko ? pdlko->asString(ctx) : nullptr;
    if (pdlk) fnCtor = fnCtor->setAttribute(ctx, pdlk, ctx->fromInteger(0x2));
}
// Function.name descriptor
const proto::ProtoObject* pdnko = ctx->fromUTF8String("__pd_name__");
const proto::ProtoString* pdnk = pdnko ? pdnko->asString(ctx) : nullptr;
if (pdnk) fnCtor = fnCtor->setAttribute(ctx, pdnk, ctx->fromInteger(0x2));
```

**Expected recovery:** ~70-80 tests (enumerable tests for call/apply/bind/toString + Function.length/name descriptor tests).

---

### Fix 2b — `Function.prototype.bind` length type handling

**File:** `src/FunctionPrototype.cpp`, `fnBind` function (~lines 119-129)

**Problem:** When computing the `length` of a bound function, the code reads `target.length`. If `target.length` is not a number (undefined, null, boolean, string), ES2015 §19.2.3.2 step 5f requires treating it as `0`. The current code may behave incorrectly for non-numeric values.

**Current code (schematic):**
```cpp
long long targetLen = 0;
const proto::ProtoObject* lo = self->getAttribute(ctx, lenKey, false);
if (lo && lo != PROTO_NONE) {
    if (lo->isInteger(ctx)) targetLen = lo->asLong(ctx);
    else if (lo->isDouble(ctx) || lo->isFloat(ctx)) targetLen = static_cast<long long>(lo->asDouble(ctx));
    // else: non-number → targetLen stays 0 (already correct if this falls through)
}
```

**Fix:** Verify the existing fallthrough correctly handles non-numeric `length`. If it does, add a comment. If there is a branch that uses the value regardless of type, add explicit type guards. Read the actual code before implementing.

The ES rule: `length = max(0, target.length - args.length)` where `target.length` is coerced to integer and defaults to `0` if not a valid number.

**Expected recovery:** ~10-15 tests (bind length edge cases).

---

## File Structure

| File | Changes |
|------|---------|
| `src/NumberPrototype.cpp` | Fix 1a (MIN_VALUE denorm_min), Fix 1b (toFixed RangeError) |
| `src/FunctionPrototype.cpp` | Fix 2a (Function.prototype non-enumerable + Function constructor descriptors), Fix 2b (bind length type check) |

---

## Test Plan

```bash
# Number
TEST262_PATTERNS="built-ins/Number" node tests/test262/runner/test262_runner.js 2>&1 | tail -5

# Function
TEST262_PATTERNS="built-ins/Function" node tests/test262/runner/test262_runner.js 2>&1 | tail -5

# Regression
TEST262_PATTERNS="language/expressions" node tests/test262/runner/test262_runner.js 2>&1 | tail -5
```

**Expected Phase 38 targets:**
| Suite | Baseline | Target |
|-------|---------|--------|
| `built-ins/Number` | 101/338 (29.9%) | ≥ 115/338 (≥34%) |
| `built-ins/Function` | 213/509 (41.8%) | ≥ 280/509 (≥55%) |
| `language/expressions` | 9419/11036 | ≥ 9419/11036 (no regression) |
