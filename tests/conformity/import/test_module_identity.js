// Phase 1.2.1 protoJS: Module identity — repeated require must share state.
// This exercises CommonJSLoader's internal module cache for file-based modules.

console.log("=== Conformity: JS module identity ===");

// Local test module relative to this file
const first = require("./pkg/dummy_module");

// Mutate the exported object
first.conformityMarker = 12345;

// Second require of the same module must see the updated property
const second = require("./pkg/dummy_module");
if (second.conformityMarker !== 12345) {
  throw new Error(
    "Module identity broken: expected second.conformityMarker === 12345, got " +
      String(second.conformityMarker)
  );
}

console.log("OK JS module identity");

