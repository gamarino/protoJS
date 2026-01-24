// Basic test for Deferred functionality

console.log("=== Testing Deferred ===");

// Test 1: Create a Deferred
console.log("\n1. Creating Deferred...");
try {
    const deferred = new Deferred((resolve, reject) => {
        console.log("   Deferred function executing...");
        // Simulate some work
        let sum = 0;
        for (let i = 0; i < 1000; i++) {
            sum += i;
        }
        console.log("   Work completed, sum =", sum);
        resolve(sum);
    });
    console.log("   Deferred created successfully");
} catch (e) {
    console.log("   Error creating Deferred:", e);
}

// Test 2: Deferred with error
console.log("\n2. Testing Deferred with error handling...");
try {
    const deferred2 = new Deferred((resolve, reject) => {
        console.log("   This will throw an error");
        throw new Error("Test error");
    });
    console.log("   Deferred with error created");
} catch (e) {
    console.log("   Error:", e);
}

console.log("\n=== Deferred tests completed ===");
