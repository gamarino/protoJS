// protoCore collections test

console.log("=== protoCore Collections Tests ===");

// Test Set
if (typeof protoCore !== 'undefined' && protoCore.Set) {
    const set = new protoCore.Set([1, 2, 3, 3, 4]);
    console.log("Set created, size:", set.size);
    set.add(5);
    console.log("After add(5), size:", set.size);
    console.log("Has 3:", set.has(3));
} else {
    console.log("protoCore.Set not available");
}

// Test Multiset
if (typeof protoCore !== 'undefined' && protoCore.Multiset) {
    const multiset = new protoCore.Multiset([1, 1, 2, 2, 2, 3]);
    console.log("Multiset size:", multiset.size);
    console.log("Count of 2:", multiset.count(2));
} else {
    console.log("protoCore.Multiset not available");
}

// Test Tuple
if (typeof protoCore !== 'undefined' && protoCore.Tuple) {
    const tuple = protoCore.Tuple([1, 2, 3]);
    console.log("Tuple created, length:", tuple.length);
} else {
    console.log("protoCore.Tuple not available");
}

console.log("=== protoCore Collections Tests Complete ===");
