# Test262 Status: `built-ins/Object/defineProperty`

**Last Updated**: 2026-05-11
**Total Passed**: 685 / 1131
**Total Failed (Semantics)**: 446
**Status**: SIGNIFICANT PROGRESS (Property Shadowing Fixed)

## Recent Improvements
- **Property Shadowing**: Fully resolved ES5.1 property shadowing regressions. Own data properties now correctly shadow inherited accessors, and own accessors correctly shadow inherited data properties.
- **Accessor Resolution**: Refactored `OP_get_field` and `OP_get_field2` to prioritize own-accessor resolution via a shadowing-aware prototype walk.
- **Placeholder Sentinels**: Switched to `undefined` sentinels for accessor placeholders in `Object.defineProperty`, ensuring robust `hasOwnAttribute` detection.
- **Interpreter Stability**: Eliminated infinite prototype loops in `invokeGetterIfPresentFast` by implementing cycle guards and `Object.prototype` detection.

## Remaining Issues
- **Attribute Lookup in C++**: `jsGetAttribute` in `ObjectPrototype.cpp` still uses a legacy walk for some descriptor parsing paths, which may lead to subtle discrepancies in complex descriptor objects.
- **Self-Prototype Cycle**: Identified a self-prototype cycle on `Object.prototype` during bootstrap that needs a core-level fix.

## Next Steps
- Audit `coercePropNameToKey` to ensure it doesn't trigger side effects during descriptor parsing.
- Refactor `jsGetAttribute` to use the same `invokeGetterIfPresentFast` logic to ensure consistency between C++ and JS property access.
- Resolve the `Object.prototype` self-prototype cycle in the bootstrap sequence.
