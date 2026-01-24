// Arithmetic operations test

console.log("=== Arithmetic Tests ===");

// Basic arithmetic
const a = 10;
const b = 3;
console.log(`${a} + ${b} =`, a + b);
console.log(`${a} - ${b} =`, a - b);
console.log(`${a} * ${b} =`, a * b);
console.log(`${a} / ${b} =`, a / b);
console.log(`${a} % ${b} =`, a % b);

// Large numbers
const big = 12345678901234567890n;
console.log("BigInt:", big);

// Floating point
const pi = 3.14159;
console.log("Pi:", pi);
console.log("Pi * 2 =", pi * 2);

console.log("=== Arithmetic Tests Complete ===");
