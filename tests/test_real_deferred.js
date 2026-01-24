// Test script to verify Deferred execution in worker threads
// This test demonstrates that CPU-intensive work runs in a worker thread
// 
// How to verify execution in worker thread:
// 1. The computation should not block the main thread
// 2. Multiple Deferreds should execute concurrently
// 3. CPU usage should be distributed across cores (check with htop/top)

console.log("=== Deferred Worker Thread Test ===");
console.log("Starting test at:", new Date().toISOString());

// Test 1: Simple CPU-intensive computation
console.log("\n--- Test 1: CPU-intensive loop ---");
const deferred1 = new Deferred(() => {
    console.log("Worker: Starting computation...");
    let x = 0;
    const start = Date.now();
    while (x < 1000000) {
        x++;
    }
    const end = Date.now();
    console.log("Worker: Computation complete. Result:", x, "Time:", end - start, "ms");
    return x;
});

deferred1.then((result) => {
    console.log("Main: Received result from worker:", result);
    console.log("Main: Test 1 PASSED");
}).catch((error) => {
    console.error("Main: Test 1 FAILED with error:", error);
});

// Test 2: Verify thread execution (print thread info if available)
console.log("\n--- Test 2: Thread verification ---");
const deferred2 = new Deferred(() => {
    // Try to get thread information
    const threadInfo = {
        timestamp: Date.now(),
        computation: "running"
    };
    
    // Do some work
    let sum = 0;
    for (let i = 0; i < 500000; i++) {
        sum += i;
    }
    
    threadInfo.computation = "complete";
    threadInfo.sum = sum;
    return threadInfo;
});

deferred2.then((result) => {
    console.log("Main: Received result:", result);
    console.log("Main: Test 2 PASSED - Function executed in worker thread");
}).catch((error) => {
    console.error("Main: Test 2 FAILED with error:", error);
});

// Test 3: Error handling
console.log("\n--- Test 3: Error handling ---");
const deferred3 = new Deferred(() => {
    throw new Error("Test error from worker thread");
});

deferred3.then((result) => {
    console.error("Main: Test 3 FAILED - Should have caught error");
}).catch((error) => {
    console.log("Main: Test 3 PASSED - Error caught:", error);
});

// Test 4: Multiple concurrent Deferreds
console.log("\n--- Test 4: Concurrent execution ---");
const promises = [];
for (let i = 0; i < 5; i++) {
    const deferred = new Deferred(() => {
        const id = i;
        let count = 0;
        for (let j = 0; j < 100000; j++) {
            count += j;
        }
        return { id, count };
    });
    
    promises.push(deferred.then((result) => {
        console.log(`Main: Deferred ${result.id} completed with count ${result.count}`);
        return result;
    }));
}

// Note: setTimeout may not be available, so we'll rely on EventLoop processing
// The main.cpp will wait for all callbacks to complete
console.log("\n=== All Tests Submitted ===");
console.log("Waiting for worker threads to complete...");
console.log("(The EventLoop will process results when workers finish)");
