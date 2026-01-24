// Benchmark: Array operations

console.log("=== Array Operations Benchmark ===");

const size = 100000;
const iterations = 100;

console.time("protoJS: Array creation and operations");

for (let i = 0; i < iterations; i++) {
    // Create large array
    const arr = Array.from({length: size}, (_, idx) => idx);
    
    // Array operations
    const arr2 = arr.map(x => x * 2);
    const arr3 = arr2.filter(x => x % 2 === 0);
    const sum = arr3.reduce((a, b) => a + b, 0);
}

console.timeEnd("protoJS: Array creation and operations");

console.log("Note: Compare with Node.js for performance comparison");
console.log("Expected: protoJS should be competitive or better for immutable operations");
