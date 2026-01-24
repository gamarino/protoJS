// Demo: Immutable arrays

console.log("=== Immutable Arrays Demo ===");

// Create array
const arr1 = [1, 2, 3];
console.log("Original array:", arr1);

// In protoJS, array operations should return new arrays (immutable)
// Note: This behavior depends on TypeBridge implementation
const arr2 = arr1.concat([4]);
console.log("After concat([4]):");
console.log("  Original:", arr1); // Should be unchanged
console.log("  New:", arr2);       // New array

// Demonstrate structural sharing concept
console.log("\n=== Structural Sharing Concept ===");
console.log("In protoCore, immutable arrays share structure");
console.log("This means memory-efficient operations even for large arrays");

console.log("\n=== Immutable Arrays Demo Complete ===");
