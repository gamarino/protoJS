/**
 * Test: Real Deferred Execution in Worker Threads
 * 
 * This test demonstrates:
 * 1. Creation of a Deferred with a CPU-intensive function
 * 2. Bytecode serialization of the function
 * 3. Execution in a worker thread (CPUThreadPool)
 * 4. Result serialization and round-trip
 * 5. Promise resolution with the computed result
 */

console.log("=== Testing Real Deferred Execution ===\n");

// Test 1: Basic Deferred with CPU-intensive work
console.log("Test 1: CPU-intensive loop in worker thread");
console.log("Creating Deferred with a loop that counts to 1,000,000...\n");

const startTime = Date.now();

new Deferred(() => {
    // This function will be serialized to bytecode
    // and executed in a worker thread
    let x = 0;
    while (x < 1000000) {
        x++;
    }
    return x;
}).then((result) => {
    const elapsed = Date.now() - startTime;
    console.log(`✓ Worker thread completed successfully`);
    console.log(`  Result: ${result}`);
    console.log(`  Time: ${elapsed}ms`);
    console.log(`  Expected: 1000000`);
    if (result === 1000000) {
        console.log(`  Status: PASS\n`);
    } else {
        console.log(`  Status: FAIL - incorrect result\n`);
    }
}).catch((error) => {
    console.log(`✗ Worker thread failed: ${error}\n`);
});

// Test 2: Deferred with arithmetic operations
console.log("Test 2: Simple arithmetic in worker thread");
console.log("Creating Deferred that computes 42 * 2...\n");

new Deferred(() => {
    return 42 * 2;
}).then((result) => {
    console.log(`✓ Worker thread completed arithmetic`);
    console.log(`  Result: ${result}`);
    console.log(`  Expected: 84`);
    if (result === 84) {
        console.log(`  Status: PASS\n`);
    } else {
        console.log(`  Status: FAIL - incorrect result\n`);
    }
}).catch((error) => {
    console.log(`✗ Worker thread failed: ${error}\n`);
});

// Test 3: Deferred with string operations
console.log("Test 3: String manipulation in worker thread");
console.log("Creating Deferred that reverses a string...\n");

new Deferred(() => {
    const str = "Deferred_Execution_Works";
    let reversed = "";
    for (let i = str.length - 1; i >= 0; i--) {
        reversed += str[i];
    }
    return reversed;
}).then((result) => {
    console.log(`✓ Worker thread completed string operation`);
    console.log(`  Result: ${result}`);
    console.log(`  Expected: skroW_noitucxE_derreffeD`);
    if (result === "skroW_noitucxE_derreffeD") {
        console.log(`  Status: PASS\n`);
    } else {
        console.log(`  Status: FAIL - incorrect result\n`);
    }
}).catch((error) => {
    console.log(`✗ Worker thread failed: ${error}\n`);
});

// Test 4: Deferred with complex computation (Fibonacci)
console.log("Test 4: Fibonacci computation in worker thread");
console.log("Creating Deferred that computes Fibonacci(20)...\n");

new Deferred(() => {
    function fib(n) {
        if (n <= 1) return n;
        return fib(n - 1) + fib(n - 2);
    }
    return fib(20);
}).then((result) => {
    console.log(`✓ Worker thread completed Fibonacci`);
    console.log(`  Result: ${result}`);
    console.log(`  Expected: 6765`);
    if (result === 6765) {
        console.log(`  Status: PASS\n`);
    } else {
        console.log(`  Status: FAIL - incorrect result\n`);
    }
}).catch((error) => {
    console.log(`✗ Worker thread failed: ${error}\n`);
});

// Test 5: Deferred with error handling
console.log("Test 5: Error handling in worker thread");
console.log("Creating Deferred that throws an error...\n");

new Deferred(() => {
    throw new Error("Intentional test error");
}).then((result) => {
    console.log(`✗ Unexpected success: ${result}\n`);
}).catch((error) => {
    console.log(`✓ Worker thread caught and propagated error`);
    console.log(`  Error: ${error}`);
    console.log(`  Status: PASS\n`);
});

// Test 6: Multiple concurrent Deferreds
console.log("Test 6: Multiple concurrent Deferreds");
console.log("Creating 3 concurrent Deferred tasks...\n");

let completedCount = 0;

for (let i = 1; i <= 3; i++) {
    new Deferred(() => {
        let sum = 0;
        for (let j = 1; j <= 100000; j++) {
            sum += j;
        }
        return sum;
    }).then((result) => {
        completedCount++;
        console.log(`✓ Concurrent task ${i} completed`);
        console.log(`  Result: ${result}`);
        console.log(`  Expected: 5000050000`);
        if (result === 5000050000) {
            console.log(`  Status: PASS`);
        } else {
            console.log(`  Status: FAIL`);
        }
        
        if (completedCount === 3) {
            console.log(`\n✓ All 3 concurrent tasks completed\n`);
        }
    }).catch((error) => {
        console.log(`✗ Concurrent task ${i} failed: ${error}`);
    });
}

console.log("=== Test Suite Complete ===");
console.log("Waiting for all Deferred tasks to complete...");
console.log("(Results printed above as tasks complete)");
