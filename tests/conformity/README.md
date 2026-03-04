# Conformity Test Suite (Phase 1)

Tests defined by `TEST_PLAN.md` for semantic correctness on the immutable protoCore engine.

## Layout

- **builtins/** — Phase 1.1: Number, String, Array, Object (protoJS).
- **import/** — Phase 1.2: module resolution, wrapper vs exports (when applicable).
- **bootstrap/** — Phase 1.3: manifest of minimal Test262 (or equivalent) subset.

## Running (protoJS)

Run integration tests with the protoJS runner; ensure `tests/conformity` is included in the test config.

```bash
# From protoJS repo root
./build/protojs tests/conformity/builtins/test_number_conformity.js
```

Or use the runner script (if provided):

```bash
node tests/conformity/run_conformity.js
```

## Immutability verification

Run the same pattern as protoPython: grep for `const_cast` in module/resolution paths and fail CI if forbidden uses remain.
