// Concurrent Deferred test

console.log("=== Concurrent Deferred Tests ===");

if (typeof Deferred !== 'undefined') {
    console.log("Creating multiple Deferreds...");
    
    const deferreds = [];
    for (let i = 0; i < 5; i++) {
        const d = new Deferred((resolve) => {
            // Simulate work
            let sum = 0;
            for (let j = 0; j < 100000; j++) {
                sum += j;
            }
            resolve(`Deferred ${i} completed with sum ${sum}`);
        });
        deferreds.push(d);
    }
    
    console.log(`Created ${deferreds.length} deferreds`);
    console.log("Note: In full implementation, these would execute concurrently");
} else {
    console.log("Deferred not available");
}

console.log("=== Concurrent Deferred Tests Complete ===");
