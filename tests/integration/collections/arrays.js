// Array operations test

console.log("=== Array Tests ===");

// Create array
const arr = [1, 2, 3, 4, 5];
console.log("Original array:", arr);

// Array operations (should be immutable in protoJS)
const arr2 = arr.concat([6, 7]);
console.log("After concat:", arr2);
console.log("Original unchanged:", arr);

// Array methods
console.log("Array length:", arr.length);
console.log("First element:", arr[0]);
console.log("Last element:", arr[arr.length - 1]);

// Array iteration
console.log("Array elements:");
for (let i = 0; i < arr.length; i++) {
    console.log(`  [${i}] = ${arr[i]}`);
}

console.log("=== Array Tests Complete ===");
