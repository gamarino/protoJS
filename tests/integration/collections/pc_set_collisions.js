// Two unequal elements whose hashes collide are both kept and told apart by
// SameValueZero (src/HashedCollection.cpp: keyHash). The pairs below collide
// in protoJS's own hash, not by chance:
//  - the integer 1 hashes as mix(1 ^ K) with K = 0x9e3779b97f4a7c15, and the
//    double whose bit pattern is K ^ 1 (a tiny negative fraction) hashes as
//    mix of its bits, the same word;
//  - -Infinity hashes as mix(0xfff0000000000000), and the BigInt
//    -4503552002004202n as mix(its two's complement ^ 0xb16b16b16), the
//    same word.
// Exits 1 on the first failed check.

function check(name, cond) {
    if (!cond) { console.log("FAILED: " + name); process.exit(1); }
}

function doubleFromBits(hi, lo) {
    var dv = new DataView(new ArrayBuffer(8));
    dv.setUint32(0, hi);
    dv.setUint32(4, lo);
    return dv.getFloat64(0);
}

// K ^ 1 = 0x9e3779b9_7f4a7c14
var tiny = doubleFromBits(0x9e3779b9, 0x7f4a7c14);
check("the colliding double is a negative fraction", tiny < 0 && tiny > -1e-100);

var s = new protoCore.Set([1, tiny]);
check("1 and its colliding double are both kept", s.size() === 2);
check("has(1)", s.has(1) === true);
check("has(colliding double, fresh copy)", s.has(doubleFromBits(0x9e3779b9, 0x7f4a7c14)) === true);
s.add(1);
s.add(doubleFromBits(0x9e3779b9, 0x7f4a7c14));
check("re-adding either does not grow the set", s.size() === 2);
s.remove(1);
check("removing 1 keeps the colliding double",
      s.size() === 1 && s.has(1) === false && s.has(doubleFromBits(0x9e3779b9, 0x7f4a7c14)) === true);
s.add(1);
s.remove(doubleFromBits(0x9e3779b9, 0x7f4a7c14));
check("removing the double keeps 1", s.size() === 1 && s.has(1) === true && s.has(tiny) === false);

var b = new protoCore.Set();
b.add(-Infinity);
b.add(-4503552002004202n);
check("-Infinity and its colliding BigInt are both kept", b.size() === 2);
check("has(-Infinity, fresh copy)", b.has(-1 / 0) === true);
check("has(colliding BigInt, fresh copy)", b.has(BigInt("-4503552002004202")) === true);
check("the number with the BigInt's value is not a member", b.has(-4503552002004202) === false);
var vals = b.values();
check("values() lists both, in insertion order",
      vals.length === 2 && vals[0] === -Infinity && vals[1] === -4503552002004202n);

console.log("pc_set_collisions: all checks passed");
