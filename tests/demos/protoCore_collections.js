// Demo: protoCore special collections

console.log("=== protoCore Collections Demo ===");

if (typeof protoCore !== 'undefined') {
    // Set
    if (protoCore.Set) {
        console.log("\n--- Set ---");
        const set = new protoCore.Set([1, 2, 3, 3, 4, 4, 5]);
        console.log("Set from [1,2,3,3,4,4,5]");
        console.log("Size:", set.size);
        set.add(6);
        console.log("After add(6), size:", set.size);
        console.log("Has 3:", set.has(3));
    }
    
    // Multiset
    if (protoCore.Multiset) {
        console.log("\n--- Multiset ---");
        const multiset = new protoCore.Multiset([1, 1, 2, 2, 2, 3]);
        console.log("Multiset from [1,1,2,2,2,3]");
        console.log("Size:", multiset.size);
        console.log("Count of 1:", multiset.count(1));
        console.log("Count of 2:", multiset.count(2));
    }
    
    // Tuple
    if (protoCore.Tuple) {
        console.log("\n--- Tuple (Immutable) ---");
        const tuple = protoCore.Tuple([1, 2, 3]);
        console.log("Tuple created:", tuple);
        console.log("Length:", tuple.length);
        console.log("Tuples are immutable in protoCore");
    }
    
    // SparseList
    if (protoCore.SparseList) {
        console.log("\n--- SparseList ---");
        const sparse = new protoCore.SparseList();
        sparse.set(0, "first");
        sparse.set(100, "hundredth");
        console.log("SparseList: set(0, 'first'), set(100, 'hundredth')");
        console.log("Size:", sparse.size);
        console.log("Has 0:", sparse.has(0));
        console.log("Has 100:", sparse.has(100));
    }
    
    // Mutability control
    console.log("\n--- Mutability Control ---");
    if (protoCore.ImmutableObject) {
        const immutable = protoCore.ImmutableObject({a: 1, b: 2});
        console.log("Immutable object created");
    }
    
    if (protoCore.MutableObject) {
        const mutable = protoCore.MutableObject({a: 1, b: 2});
        console.log("Mutable object created");
    }
} else {
    console.log("protoCore module not available");
}

console.log("\n=== protoCore Collections Demo Complete ===");
