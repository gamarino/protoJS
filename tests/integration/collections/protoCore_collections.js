// protoCore collections, on the protoCore-native `protoCore` global.
//
// These used to exist only in the QuickJS-side module, which scripts running
// on the protoCore interpreter never see, so every check here was guarded by
// an "if available" branch that silently skipped. They are now native, and
// this test asserts.
//
// Note: `size()` is a METHOD, not a property (as documented in
// docs/PROTOCORE_MODULE.md).
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " — " + detail : ""));
    }
}

function throws(name, fn, expectedName) {
    var threw = null;
    try {
        fn();
    } catch (e) {
        threw = e;
    }
    check(name, threw !== null && threw && threw.name === expectedName,
          threw === null ? "did not throw" : "threw " + (threw && (threw.name + ": " + threw.message)));
}

// ---- Surface --------------------------------------------------------------

check("protoCore is an object", typeof protoCore === "object" && protoCore !== null,
      "got " + typeof protoCore);
check("protoCore.Set is a function", typeof protoCore.Set === "function",
      "got " + typeof protoCore.Set);
check("protoCore.Multiset is a function", typeof protoCore.Multiset === "function",
      "got " + typeof protoCore.Multiset);
check("protoCore.SparseList is a function", typeof protoCore.SparseList === "function",
      "got " + typeof protoCore.SparseList);
check("protoCore.Tuple is a function", typeof protoCore.Tuple === "function",
      "got " + typeof protoCore.Tuple);
check("protoCore.ImmutableObject is a function", typeof protoCore.ImmutableObject === "function",
      "got " + typeof protoCore.ImmutableObject);
check("protoCore.MutableObject is a function", typeof protoCore.MutableObject === "function",
      "got " + typeof protoCore.MutableObject);
check("protoCore.isImmutable is a function", typeof protoCore.isImmutable === "function",
      "got " + typeof protoCore.isImmutable);
check("protoCore.makeImmutable is a function", typeof protoCore.makeImmutable === "function",
      "got " + typeof protoCore.makeImmutable);
check("protoCore.makeMutable is a function", typeof protoCore.makeMutable === "function",
      "got " + typeof protoCore.makeMutable);
check("protoCore.runInThread is a function", typeof protoCore.runInThread === "function",
      "got " + typeof protoCore.runInThread);

// ---- Set ------------------------------------------------------------------

var set = new protoCore.Set([1, 2, 3, 3, 4]);
check("Set drops duplicates", set.size() === 4, "size() = " + set.size());
check("Set instanceof protoCore.Set", set instanceof protoCore.Set);
check("Set.has finds a member", set.has(3) === true, "got " + set.has(3));
check("Set.has rejects a non-member", set.has(99) === false, "got " + set.has(99));

var addReturn = set.add(5);
check("Set.add returns the same object", addReturn === set);
check("Set.add grows the set", set.size() === 5, "size() = " + set.size());
check("Set.add is visible to has", set.has(5) === true);

set.add(5);
check("Set.add of an existing member does not grow it", set.size() === 5,
      "size() = " + set.size());

set.remove(5);
check("Set.remove shrinks the set", set.size() === 4, "size() = " + set.size());
check("Set.remove removes the member", set.has(5) === false);

var emptySet = new protoCore.Set();
check("Set without arguments is empty", emptySet.size() === 0,
      "size() = " + emptySet.size());

// Equality of values built at runtime: a rope string must match a literal.
var ropeSet = new protoCore.Set(["ab"]);
check("Set matches a string built at runtime", ropeSet.has("a" + "b") === true,
      "has('a'+'b') = " + ropeSet.has("a" + "b"));
ropeSet.add("a" + "b");
check("Set does not double-store an equal string", ropeSet.size() === 1,
      "size() = " + ropeSet.size());

// ---- Multiset -------------------------------------------------------------

var multiset = new protoCore.Multiset([1, 1, 2, 2, 2, 3]);
check("Multiset keeps duplicates", multiset.size() === 6, "size() = " + multiset.size());
check("Multiset instanceof protoCore.Multiset", multiset instanceof protoCore.Multiset);
check("Multiset.count counts occurrences", multiset.count(2) === 3,
      "count(2) = " + multiset.count(2));
check("Multiset.count of an absent value is 0", multiset.count(99) === 0,
      "count(99) = " + multiset.count(99));

check("Multiset.add returns the same object", multiset.add(2) === multiset);
check("Multiset.add increments the count", multiset.count(2) === 4,
      "count(2) = " + multiset.count(2));
multiset.remove(2);
check("Multiset.remove removes one occurrence", multiset.count(2) === 3,
      "count(2) = " + multiset.count(2));

// ---- SparseList -----------------------------------------------------------

var sparse = new protoCore.SparseList();
check("SparseList starts empty", sparse.size() === 0, "size() = " + sparse.size());
check("SparseList instanceof protoCore.SparseList", sparse instanceof protoCore.SparseList);

check("SparseList.set returns the same object", sparse.set(10, "ten") === sparse);
sparse.set(1000000, "million");
check("SparseList holds both entries", sparse.size() === 2, "size() = " + sparse.size());
check("SparseList.get reads a value", sparse.get(10) === "ten", "got " + sparse.get(10));
check("SparseList.get reads a far index", sparse.get(1000000) === "million",
      "got " + sparse.get(1000000));
check("SparseList.has finds a set index", sparse.has(10) === true);
check("SparseList.has rejects an unset index", sparse.has(11) === false);
check("SparseList.get of an unset index is undefined", sparse.get(11) === undefined,
      "got " + sparse.get(11));
check("SparseList.set overwrites", sparse.set(10, "TEN").get(10) === "TEN",
      "got " + sparse.get(10));

// ---- Tuple ----------------------------------------------------------------

var tuple = protoCore.Tuple([1, 2, 3]);
check("Tuple returns an array", Array.isArray(tuple) === true, "Array.isArray = " + Array.isArray(tuple));
check("Tuple keeps the length", tuple.length === 3, "length = " + tuple.length);
check("Tuple keeps the elements", tuple[0] === 1 && tuple[1] === 2 && tuple[2] === 3,
      "got " + tuple[0] + "," + tuple[1] + "," + tuple[2]);

// ---- Mutability helpers ---------------------------------------------------

// NOTE: `ProtoObject::clone()` does not carry the source's own attributes, so
// these helpers return an EMPTY object of the requested mutability rather than
// a copy of the argument. That is a protoCore-level limitation, recorded in
// docs/PROTOCORE_MODULE.md. The checks below pin the behaviour that actually
// exists rather than the behaviour the names suggest.
var source = { a: 1, b: "two" };
var frozen = protoCore.ImmutableObject(source);
check("ImmutableObject returns a different object", frozen !== source);
check("ImmutableObject returns an object",
      typeof frozen === "object" && frozen !== null, "got " + typeof frozen);
check("ImmutableObject does not carry the source properties (protoCore clone limitation)",
      frozen.a === undefined, "got a=" + frozen.a);
check("ImmutableObject leaves the source untouched",
      source.a === 1 && source.b === "two");

var thawed = protoCore.MutableObject(source);
check("MutableObject returns a different object", thawed !== source);
thawed.a = 2;
check("MutableObject result accepts writes", thawed.a === 2, "got " + thawed.a);

check("makeImmutable returns an object", typeof protoCore.makeImmutable(source) === "object");
check("makeMutable returns an object", typeof protoCore.makeMutable(source) === "object");

// isImmutable reports true for primitives. For object cells it is a
// placeholder that always reports false; see docs/PROTOCORE_MODULE.md.
check("isImmutable(1) is true", protoCore.isImmutable(1) === true,
      "got " + protoCore.isImmutable(1));

// ---- Error handling -------------------------------------------------------

throws("Set without new throws TypeError", function () { protoCore.Set([1]); }, "TypeError");
throws("Multiset without new throws TypeError", function () { protoCore.Multiset([1]); }, "TypeError");
throws("SparseList without new throws TypeError", function () { protoCore.SparseList(); }, "TypeError");

throws("Set method on a foreign receiver throws TypeError",
       function () { protoCore.Set.prototype.add.call({}, 1); }, "TypeError");
throws("Multiset method on a foreign receiver throws TypeError",
       function () { protoCore.Multiset.prototype.count.call({}, 1); }, "TypeError");
throws("SparseList method on a foreign receiver throws TypeError",
       function () { protoCore.SparseList.prototype.get.call({}, 1); }, "TypeError");

throws("Tuple of a non-array throws TypeError",
       function () { protoCore.Tuple("not an array"); }, "TypeError");
throws("ImmutableObject of a primitive throws TypeError",
       function () { protoCore.ImmutableObject(1); }, "TypeError");
throws("MutableObject of a primitive throws TypeError",
       function () { protoCore.MutableObject(1); }, "TypeError");

// ---- Result ---------------------------------------------------------------

if (failures.length) {
    console.log("protoCore_collections: " + failures.length + " check(s) failed");
    for (var i = 0; i < failures.length; i++) {
        console.log("  FAIL: " + failures[i]);
    }
    process.exit(1);
}
console.log("protoCore_collections: all checks passed");
