// protoCore.Set follows SameValueZero: every NaN is one element and -0 is
// +0. A NaN computed at run time is a fresh boxed double, which a
// proto::ProtoSet compares by pointer from protoCore 3.5.0 on; -0 is a double
// and +0 a SmallInteger, whose protoCore hashes differ.
// Exits 1 on the first failed check.

function check(name, cond) {
    if (!cond) { console.log("FAILED: " + name); process.exit(1); }
}

function nan() { return Number("not a number"); }
function negZero() { return -1 / Infinity; }

var s = new protoCore.Set();
s.add(nan());
check("NaN found via another NaN", s.has(0 / 0) === true && s.has(nan()) === true);
s.add(nan());
s.add(Math.sqrt(-1));
check("NaN stored once", s.size() === 1);

var z = new protoCore.Set([negZero()]);
check("-0 found via +0", z.has(0) === true);
check("-0 found via -0", z.has(negZero()) === true);
z.add(0);
check("-0 and +0 are one element", z.size() === 1);
var stored = z.values()[0];
check("-0 is stored as +0", stored === 0 && 1 / stored === Infinity);

var p = new protoCore.Set([0]);
check("+0 found via -0", p.has(negZero()) === true);
p.remove(negZero());
check("remove(-0) removes +0", p.size() === 0 && p.has(0) === false);

s.remove(nan());
check("remove(NaN) removes NaN", s.size() === 0 && s.has(nan()) === false);

console.log("pc_set_nan_zero: all checks passed");
