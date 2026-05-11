# Test262 Conformance Status

This document tracks the current compliance of the `protoJS` engine with the official ECMAScript Test262 suite. The core focus is to ensure that `protoCore`'s object model natively enforces standard ECMAScript semantics without delegating property logic to `QuickJS`.

## Current Status (May 2026)

**`built-ins/Object/defineProperty`:**
*   Total tests: 1131
*   Passed: 481 (42.5%)
*   Failed: 650 (57.5%)

Recent wins in the Phase 6 effort include:
1.  **Descriptor Inheritance:** Descriptor objects returned by `getOwnPropertyDescriptor` now correctly inherit from `Object.prototype`, satisfying `test262_runner` type checks.
2.  **`writable: false` Enforcement:** Fixed a regression where `OP_put_field` bypassed the internal `protoCore` attribute bits (`__pd_<key>__`). Now, writes to non-writable properties are correctly intercepted and ignored in sloppy mode (and will eventually throw `TypeError` in strict mode).
3.  **Symbol Handling:** Fixed regressions regarding `Symbol` property definition by ensuring correct `sym_<ptr>` stringification down in the `resolvePutFieldOOP` dispatcher.

---

## Roadmap to 95% Conformance

To reach at least 95% compliance in the `Object.defineProperty` and core property resolution suite, the following steps are planned:

### Step 1: Implement Missing Globals (`NaN`, `Infinity`, `undefined`, `Symbol`)
*   **Context:** Currently, over 300 tests fail because `test262_runner` implicitly relies on `Infinity` or `NaN` resolving to valid primitives. In `protoJS`, these are currently `undefined`.
*   **Action:** Inject `Infinity`, `NaN`, and `undefined` into the `ProtoContext` global object during bootstrap in `JSPrototypes.cpp`.
*   **Action:** Ensure the polyfill or native implementation of `Symbol()` correctly returns a unique primitive object rather than `undefined`.

### Step 2: Implement Strict Mode `TypeError` on Write
*   **Context:** While `OP_put_field` now drops writes to `writable: false` properties, it drops them silently in all modes.
*   **Action:** Modify `resolvePutFieldOOP` to check the execution frame's strict-mode flag and set `pending_exception` (TypeError) when an assignment is attempted on a non-writable property in strict mode.

### Step 3: Implement Accessor Descriptor Resolution (`get`/`set`)
*   **Context:** `Object.defineProperty` tests checking custom `get`/`set` behavior fail because `__get_<key>__` and `__set_<key>__` are not fully wired into `resolvePutFieldOOP` and `resolveGetFieldOOP` for standard assignment and access ops.
*   **Action:** Implement `invokeSetterIfPresent` and `invokeGetterIfPresent` paths inside the property resolution hot-paths.

### Step 4: Correct `configurable: false` Enforcement
*   **Context:** Deletion logic (`OP_delete`) must respect the `configurable` bit of the sidecar descriptor.
*   **Action:** Update the interpreter's `OP_delete` case to check the `__pd_<key>__` bitmask (specifically bit 1, `0x2`) before permitting `removeAttribute`. In strict mode, throw a `TypeError`.

### Step 5: `enumerable: false` Handling in `Object.keys()` / `for...in`
*   **Context:** Iteration semantics must skip keys where the `__pd_<key>__` sidecar indicates `enumerable: false`.
*   **Action:** Refactor `getOwnPropertyNames` and related iterator helpers to filter out keys based on their descriptor sidecar.

### Step 6: Fix Array Length Exotic Behavior
*   **Context:** Modifying the `length` property of an Array via `defineProperty` requires exotic behavior (e.g., truncating the array if the new length is smaller, and handling `writable: false` edge cases).
*   **Action:** Introduce `ArrayLengthBehavior` to intercept `putField` and `defineProperty` specifically for the `"length"` key on arrays.

Executing these steps will systematically eliminate the remaining `FAILED_SEMANTICS` results in the Test262 property suite.
