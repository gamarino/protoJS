// protoCore.Set compares numbers by value (SameValueZero). Fractions,
// integers beyond 2**53 and the infinities are boxed doubles: two equal ones
// computed separately are two different objects, which a proto::ProtoSet
// compares by pointer from protoCore 3.5.0 on, so this test fails when the
// binding stores a ProtoSet.
// Exits 1 on the first failed check.

function check(name, cond) {
    if (!cond) { console.log("FAILED: " + name); process.exit(1); }
}

function half(n) { return n / 2; }

var s = new protoCore.Set([1.5]);
check("1.5 found via 3 / 2", s.has(half(3)) === true);
check("1.5 found via Number('1.5')", s.has(Number("1.5")) === true);
s.add(half(3));
s.add(Number("1.5"));
check("1.5 stored once", s.size() === 1);

var values = [0.1 + 0.2, -2.75, 1e300, -1e-300, Math.pow(2, 60), Infinity, -Infinity];
var t = new protoCore.Set();
for (var i = 0; i < values.length; i++) t.add(values[i]);
check("distinct doubles", t.size() === values.length);
check("0.1 + 0.2 by value", t.has(Number("0.30000000000000004")) === true);
check("-2.75 by value", t.has(half(-5.5)) === true);
check("1e300 by value", t.has(Number("1e300")) === true);
check("-1e-300 by value", t.has(-Number("1e-300")) === true);
check("2**60 by value", t.has(Math.pow(2, 30) * Math.pow(2, 30)) === true);
check("Infinity by value", t.has(half(Infinity)) === true);
check("-Infinity by value", t.has(-half(Infinity)) === true);
check("0.3 is not 0.1 + 0.2", t.has(0.3) === false);

// Integers and the equal integral doubles are one value.
var u = new protoCore.Set([2]);
check("2 found via 4 / 2", u.has(half(4)) === true);
check("2 found via Math.floor(2.5)", u.has(Math.floor(2.5)) === true);

t.remove(Number("1e300"));
check("remove(equal double)", t.has(1e300) === false && t.size() === values.length - 1);

console.log("pc_set_boxed_doubles: all checks passed");
