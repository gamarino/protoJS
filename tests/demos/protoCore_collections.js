// Demo: the protoCore collections exposed on the `protoCore` global.
//
// These are native protoCore structures (persistent AVL-backed sets and sparse
// lists), not the ECMAScript `Set`/`Map`. `size()` is a method.
//
// Run: protojs tests/demos/protoCore_collections.js

console.log("=== protoCore Collections Demo ===");

// --- Set: unique elements -------------------------------------------------
console.log("\n--- Set ---");
const set = new protoCore.Set([1, 2, 3, 3, 4, 4, 5]);
console.log("Set from [1,2,3,3,4,4,5], size():", set.size());
set.add(6);
console.log("After add(6), size():", set.size());
console.log("has(3):", set.has(3));
set.remove(3);
console.log("After remove(3), has(3):", set.has(3), "size():", set.size());

// --- Multiset: counted occurrences ----------------------------------------
console.log("\n--- Multiset ---");
const multiset = new protoCore.Multiset([1, 1, 2, 2, 2, 3]);
console.log("Multiset from [1,1,2,2,2,3], size():", multiset.size());
console.log("count(1):", multiset.count(1));
console.log("count(2):", multiset.count(2));
multiset.remove(2);
console.log("After remove(2), count(2):", multiset.count(2));

// --- SparseList: indices without a dense backing array --------------------
console.log("\n--- SparseList ---");
const sparse = new protoCore.SparseList();
sparse.set(10, "ten");
sparse.set(1000000, "million");
console.log("size() after two far-apart indices:", sparse.size());
console.log("get(10):", sparse.get(10));
console.log("get(1000000):", sparse.get(1000000));
console.log("has(11):", sparse.has(11), "- get(11):", sparse.get(11));

// --- Tuple: a plain array built from protoCore storage --------------------
console.log("\n--- Tuple ---");
const tuple = protoCore.Tuple([1, 2, 3]);
console.log("Tuple([1,2,3]):", tuple, "length:", tuple.length,
            "Array.isArray:", Array.isArray(tuple));

// --- Mutability helpers ---------------------------------------------------
// Known limitation: ProtoObject::clone() does not carry the source's own
// attributes, so these helpers return a NEW BUT EMPTY object rather than a
// copy. See docs/PROTOCORE_MODULE.md.
console.log("\n--- Mutability ---");
const source = { a: 1, b: "two" };
const frozen = protoCore.ImmutableObject(source);
console.log("ImmutableObject returns a new object:", frozen !== source);
console.log("limitation - the result is empty, so frozen.a is:", frozen.a);
console.log("the source is untouched:", source.a, source.b);

const thawed = protoCore.MutableObject(source);
thawed.a = 2;
console.log("MutableObject result accepts writes, a =", thawed.a);

// Placeholder: protoCore exposes no mutability query, so primitives report
// true and every object reports false.
console.log("isImmutable(1):", protoCore.isImmutable(1),
            "- isImmutable({}):", protoCore.isImmutable({}));

console.log("\n=== protoCore Collections Demo Complete ===");
