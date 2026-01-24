// Demo: Deferred with worker threads

console.log("=== Deferred Demo ===");

if (typeof Deferred !== 'undefined') {
    console.log("Creating Deferred...");
    
    const deferred = new Deferred((resolve, reject) => {
        console.log("  Deferred function executing...");
        
        // Simulate CPU-intensive work
        let sum = 0;
        for (let i = 0; i < 10000000; i++) {
            sum += i;
        }
        
        console.log("  Work completed, sum =", sum);
        resolve(sum);
    });
    
    console.log("Deferred created, waiting for result...");
    // Note: In full implementation, would use .then() or await
    console.log("(In full implementation, would handle async result)");
} else {
    console.log("Deferred not available");
}

console.log("=== Deferred Demo Complete ===");
