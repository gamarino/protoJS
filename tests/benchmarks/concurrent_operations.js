// Benchmark: Concurrent operations with Deferred

console.log("=== Concurrent Operations Benchmark ===");

if (typeof Deferred !== 'undefined') {
    const numTasks = 10;
    const workPerTask = 1000000;
    
    console.time("protoJS: Concurrent with Deferred");
    
    const deferreds = [];
    for (let i = 0; i < numTasks; i++) {
        const d = new Deferred((resolve) => {
            let sum = 0;
            for (let j = 0; j < workPerTask; j++) {
                sum += j;
            }
            resolve(sum);
        });
        deferreds.push(d);
    }
    
    // Note: In full implementation, would await all
    console.log(`Created ${deferreds.length} deferreds`);
    console.log("(In full implementation, would measure completion time)");
    
    console.timeEnd("protoJS: Concurrent with Deferred");
    
    console.log("\nExpected: protoJS Deferred should utilize all CPU cores");
    console.log("Compare with Node.js Promise (single-threaded)");
} else {
    console.log("Deferred not available for benchmark");
}
