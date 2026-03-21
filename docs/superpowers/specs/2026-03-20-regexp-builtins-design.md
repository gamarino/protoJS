# Design Specification: Completing RegExp Built-ins

**Date:** 2026-03-20
**Topic:** RegExp Built-ins Implementation via libregexp (Test262 Conformance)
**Status:** Approved

## Objective
To implement `RegExp.prototype` and the `RegExp` constructor in `protoJS` using the QuickJS `libregexp` engine, completing the standard RegExp object API and resolving related failures in `String.prototype.match`, `replace`, `search`, and `split`.

## 1. Scope
The implementation will cover:
- **Constructor:** `RegExp(pattern, flags)` with runtime compilation to bytecode.
- **Properties:** `source`, `flags`, `global`, `ignoreCase`, `multiline`, `dotAll`, `unicode`, `sticky`, `hasIndices`.
- **Methods:** `exec()`, `test()`, `toString()`.
- **Well-Known Symbols:** `[Symbol.match]`, `[Symbol.replace]`, `[Symbol.search]`, `[Symbol.split]`, `[Symbol.matchAll]`.
- **String Integration:** Updating `String.prototype.match`, `replace`, `search`, and `split` to delegate to these symbols when a RegExp object is provided.

This addresses ~677 failures in the `built-ins/RegExp` test suite and remaining failures in `built-ins/String`.

## 2. Architecture & Data Flow
- **C++ Components:** All RegExp logic will reside in `src/RegExpPrototype.cpp` and `src/RegExpPrototype.h`. Registration will happen during `BootstrapJSPrototypes` in `src/JSPrototypes.cpp`.
- **Compilation (`lre_compile`):** 
  - When `RegExp` is instantiated, the `source` and `flags` strings are extracted.
  - Flags are parsed into `libregexp` integer flags (e.g., `LRE_FLAG_GLOBAL`).
  - `lre_compile` is called to produce bytecode.
  - The resulting bytecode buffer is stored as an internal attribute on the newly constructed object (e.g., `__re_bytecode__`) using `ctx->fromBuffer(...)` with a custom destructor if needed (or manually freed if `lre_compile` uses `malloc`).
- **Execution (`lre_exec`):**
  - When `exec` or `test` is called, the target string is converted to UTF-16 (to correctly handle JS indices).
  - `lre_exec` is called using `cbuf_type = 1` (16-bit characters).
  - Capture groups are extracted and mapped into a JS Array structure per the ECMAScript spec.
  - `lastIndex` is updated accordingly on the RegExp instance.
- **Delegation:** Existing string methods (`match`, `replace`, `search`, `split`) will be modified to detect RegExp objects (using a `isRegExp` helper) and invoke their respective well-known symbols (e.g., `ctx->fromUTF8String("Symbol.match")` lookup).

## 3. Error Handling
- **Compilation Errors:** If `lre_compile` returns an error, a `SyntaxError` will be thrown using the error message populated by `libregexp`.
- **Type Coercion:** Standard ECMAScript type coercions (`ToString`, `ToLength`) will be rigorously applied, throwing `TypeError` where specified.

## 4. Testing & Validation
- **Incremental Validation:** Isolated subsets of Test262 will be run after each batch of methods is implemented (e.g., `TEST262_PATTERNS=built-ins/RegExp/prototype/exec node tests/test262/runner/test262_runner.js`).
- **Integration Validation:** Re-run `built-ins/String` subsets to verify the regex paths for `match`, `replace`, `search`, and `split` are now fixed.
- **Regression Testing:** Upon completion, a full suite run (`./tests/run_all_tests.sh`) will guarantee zero regressions across the codebase.
- **Reporting:** `CONFORMANCE_JS.md` will be updated with the exact number of fixed failures.