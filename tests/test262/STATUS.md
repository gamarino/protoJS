# Test262 Per-Test Status (Current Run)

This file documents the **per-test status** for the latest local mini-suite run driven by `tests/test262/runner/test262_runner.js` without `TEST262_ROOT` (i.e. using `tests/test262/tests` as the source tree).

## Snapshot metadata

- **Snapshot file**: `tests/test262/reports/snapshot-language-expressions_language-statements_language-scoping_built-ins-Object-1772595052491.json`  
- **Generated at**: `2026-03-04T03:30:52.490Z`  
- **Patterns**: `language/expressions`, `language/statements`, `language/scoping`, `built-ins/Object`  
- **Summary**:
  - Total: 7
  - Passed: 7
  - Failed (syntax): 0
  - Failed (semantics): 0
  - Timeouts: 0

---

## Per-test results (mini-suite)

| Path                                               | Result  | Duration (ms) |
|----------------------------------------------------|---------|---------------|
| `built-ins/Object/defineProperty-basic.js`         | passed  | 25            |
| `built-ins/Object/prototype-chain.js`             | passed  | 21            |
| `language/expressions/addition-simple.js`         | passed  | 20            |
| `language/expressions/unary-negation.js`          | passed  | 22            |
| `language/scoping/closure-basic.js`               | passed  | 21            |
| `language/scoping/let-block.js`                   | passed  | 21            |
| `language/statements/if-basic.js`                 | passed  | 21            |

---

## Notes

- This STATUS file tracks the **local mini-suite** under `tests/test262/tests`.  
- For full Test262 runs (with `TEST262_ROOT` pointing to `tc39/test262`), the authoritative per-test data lives in the JSON snapshots under `tests/test262/reports/`.  
- When expanding coverage, regenerate a new snapshot with the runner and either:
  - Update this file to reference the new snapshot and list additional tests, or
  - Generate an aggregated report script that consumes the JSON and emits a refreshed STATUS document.

