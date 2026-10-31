// protoCore.Multiset counts occurrences under SameValueZero: equal long
// strings and equal boxed doubles share one count, NaN is one value, -0 is
// +0, and two unequal values whose hashes collide keep separate counts
// (see pc_set_collisions.js for the colliding pair). proto::ProtoMultiset
// keys its counts by hash alone, so it merges the colliding pair and splits
// -0 from +0.
// Exits 1 on the first failed check.

function check(name, cond) {
    if (!cond) { console.log("FAILED: " + name); process.exit(1); }
}

function str(i) { return ["a-long-runtime-string-", String(i)].join(""); }
function doubleFromBits(hi, lo) {
    var dv = new DataView(new ArrayBuffer(8));
    dv.setUint32(0, hi);
    dv.setUint32(4, lo);
    return dv.getFloat64(0);
}

var m = new protoCore.Multiset([str(1), str(1), str(2)]);
check("equal long strings share a count", m.count(str(1)) === 2 && m.count(str(2)) === 1);
m.add(3 / 2).add(Number("1.5"));
check("equal doubles share a count", m.count(1.5) === 2);
m.add(0 / 0).add(Number("x"));
check("NaN is one value", m.count(Math.sqrt(-1)) === 2);
m.add(-1 / Infinity).add(0);
check("-0 is +0", m.count(0) === 2 && m.count(-0) === 2);
check("size counts occurrences", m.size() === 9);

var tiny = doubleFromBits(0x9e3779b9, 0x7f4a7c14);
var c = new protoCore.Multiset([1, 1, tiny]);
check("colliding values keep separate counts",
      c.count(1) === 2 && c.count(doubleFromBits(0x9e3779b9, 0x7f4a7c14)) === 1);
c.remove(doubleFromBits(0x9e3779b9, 0x7f4a7c14));
check("removing the double leaves 1's count", c.count(1) === 2 && c.count(tiny) === 0 && c.size() === 2);

m.remove(str(1));
check("remove(equal copy) decrements", m.count(str(1)) === 1 && m.size() === 8);
m.remove(str(1));
m.remove(str(1));
check("remove past zero is a no-op", m.count(str(1)) === 0 && m.size() === 7);

console.log("pc_multiset_equality: all checks passed");
