# Design Specification: Completing Array and String Built-ins

**Date:** 2026-03-20
**Topic:** Array and String Prototype Completion (Test262 Conformance)
**Status:** Approved

## Objective
To complete the remaining missing methods on `Array.prototype` and `String.prototype` in protoJS, driving the Test262 failure count down in the `built-ins/Array` and `built-ins/String` categories.

## 1. Scope
The scope covers implementing any stubbed or missing ECMAScript built-in methods on `Array` and `String` prototypes.
Based on the Test262 conformance data (`CONFORMANCE_JS.md`), the primary targets are:
- **Array:** Lingering prototype methods causing the 279 failures in the general `Array/prototype` suite, and static methods like `Array.fromAsync`.
- **String:** `String.prototype.matchAll`, `normalize`, `padEnd`, `padStart`, `repeat`, `replace` edge cases, and `isWellFormed`.

## 2. Architecture & Data Flow
- **C++ Signatures:** All new methods will be implemented in `src/ArrayPrototype.cpp` and `src/StringPrototype.cpp` matching the signature: `const proto::ProtoObject*(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parent, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs)`.
- **String Handling:** Since ECMAScript mandates string indexes be treated as 16-bit code units, all string operations will strictly rely on the existing `utf8ToUTF16` and `utf16ToUTF8` helpers. This avoids multi-byte UTF-8 calculation bugs.
- **Array Handling:** Array methods will proxy elements via the existing `arrGet`, `arrSet`, and `arrLen` helpers. This ensures compatibility with both contiguous arrays (`ProtoList`) and sparse arrays (`ProtoSparseList`), as well as array-like objects.
- **Registration:** The new methods will be bound during prototype initialization in `BuildStringPrototype` and `ensureArrayPrototype` using `ctx->fromMethod()`.

## 3. Error Handling
- **Type Coercion:** Standard type coercions (e.g., `ToIntegerOrInfinity`, `ToLength`, `ToString`) will be enforced using established helpers like `getIntArg` and `getStrArg`.
- **Exceptions:** Standard ECMAScript exceptions (`TypeError`, `RangeError`) will be thrown when appropriate (e.g., calling string methods on `null` or `undefined`, or invalid lengths in `padEnd`). The protoCore error reporting mechanisms will be utilized to ensure exceptions match exactly what QuickJS and Node.js emit.

## 4. Testing & Validation
- **Incremental Validation:** Isolated subsets of Test262 will be run after each batch of methods is implemented (e.g., `TEST262_PATTERNS=built-ins/String/prototype/padEnd node tests/test262/runner/test262_runner.js`).
- **Regression Testing:** Upon completion of the surface area, a full suite run (`./tests/run_all_tests.sh`) will guarantee zero regressions across the 42,800+ already-passing tests.
- **Reporting:** `CONFORMANCE_JS.md` will be updated with the exact number of fixed failures.
