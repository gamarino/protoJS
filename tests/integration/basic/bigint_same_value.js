// SameValue / SameValueZero / IsStrictlyEqual on BigInts compare values
// (§6.1.6.2.13 BigInt::equal), not identity: every evaluation of `5n` is a
// distinct wrapper in protoJS, so identity is never the answer.
//
// Until 2026-10 Object.is(5n, 5n), [5n].includes(5n) and [5n].indexOf(5n)
// all missed (false / false / -1).  Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + String(got) + ", expected " + String(expected));
}

var big = 2n ** 70n;
function mk(n) { return BigInt(n); }

// SameValue (Object.is).
check("Object.is(5n, 5n)", Object.is(5n, 5n), true);
check("Object.is(5n, BigInt(5))", Object.is(5n, mk(5)), true);
check("Object.is(2n**70n, 2n**70n)", Object.is(big, 2n ** 70n), true);
check("Object.is(-0n, 0n)", Object.is(-0n, 0n), true);
check("Object.is(5n, 6n)", Object.is(5n, 6n), false);
check("Object.is(5n, 5)", Object.is(5n, 5), false);
check("Object.is(5, 5n)", Object.is(5, 5n), false);
check("Object.is(5n, '5')", Object.is(5n, "5"), false);
check("Object.is(Object(5n), Object(5n))", Object.is(Object(5n), Object(5n)), false);
var w = Object(5n);
check("Object.is(w, w)", Object.is(w, w), true);
check("Object.is(w, 5n)", Object.is(w, 5n), false);

// SameValueZero (includes) and IsStrictlyEqual (indexOf / lastIndexOf).
var arr = [1, 5n, big, 7];
check("includes(5n)", arr.includes(5n), true);
check("includes(BigInt(5))", arr.includes(mk(5)), true);
check("includes(2n**70n)", arr.includes(2n ** 70n), true);
check("includes(5)", arr.includes(5), false);
check("includes(6n)", arr.includes(6n), false);
check("includes(1n)", arr.includes(1n), false);
check("indexOf(5n)", arr.indexOf(5n), 1);
check("indexOf(2n**70n)", arr.indexOf(2n ** 70n), 2);
check("indexOf(7n)", arr.indexOf(7n), -1);
check("lastIndexOf(5n)", [5n, 1, 5n].lastIndexOf(5n), 2);
check("includes in a holey array", [, 5n].includes(5n), true);

// Already right, kept as a guard: ===, Map / Set keys.
check("5n === 5n", 5n === 5n, true);
check("Set has", new Set([big]).has(2n ** 70n), true);
check("Map get", new Map([[5n, "x"]]).get(5n), "x");

// A non-configurable, non-writable BigInt property may be redefined with the
// same value (ValidateAndApplyPropertyDescriptor uses SameValue).
var o = {};
Object.defineProperty(o, "b", { value: 5n, writable: false, configurable: false });
try {
    Object.defineProperty(o, "b", { value: 5n });
    check("redefine with the same BigInt", true, true);
} catch (e) {
    failures.push("redefine with the same BigInt: threw " + e);
}
try {
    Object.defineProperty(o, "b", { value: 6n });
    failures.push("redefine with another BigInt: no TypeError");
} catch (e) {
    check("redefine with another BigInt throws TypeError", e instanceof TypeError, true);
}

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("bigint_same_value: all checks passed");
