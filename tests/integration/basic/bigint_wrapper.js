// BigInt wrapper objects (ECMA-262 §7.1.18 ToObject, §21.2.3): Object(1n) is
// an object whose [[BigIntData]] is 1n -- typeof "object", distinct from the
// primitive, inheriting BigInt.prototype -- and BigInt.prototype's methods
// accept it as their receiver (thisBigIntValue, §21.2.3).  A sloppy function
// called with a BigInt `this` sees such a wrapper.  Writing a property of a
// BigInt primitive follows the rule of every other primitive (PutValue,
// §6.2.5.6): a TypeError in strict code, ignored in sloppy code.
//
// protoJS carries a BigInt primitive as an object cell; before this test
// Object(1n) answered the primitive itself.  Passes under Node.js.

var failures = 0;
var checks = 0;
function check(name, cond, detail) {
    checks++;
    if (!cond) {
        failures++;
        console.log("FAILED: " + name + (detail !== undefined ? " -- " + detail : ""));
    }
}
function threw(fn, Ctor) {
    try { fn(); } catch (e) { return e instanceof Ctor; }
    return false;
}

// --- Object(bigint) --------------------------------------------------------------
var w = Object(1n);
check("typeof Object(1n)", typeof w === "object", typeof w);
check("new Object(1n)", typeof new Object(1n) === "object");
check("wrapper is not the primitive", w !== 1n);
check("wrappers are distinct", Object(1n) !== Object(1n));
check("Object(wrapper) is the wrapper", Object(w) === w);
check("[[Prototype]] is BigInt.prototype", Object.getPrototypeOf(w) === BigInt.prototype);
check("instanceof BigInt", w instanceof BigInt);
check("constructor", w.constructor === BigInt);
check("valueOf gives the primitive", w.valueOf() === 1n && typeof w.valueOf() === "bigint");
check("BigInt.prototype.valueOf.call(wrapper)", BigInt.prototype.valueOf.call(Object(9n)) === 9n);
check("toString", w.toString() === "1" && Object(255n).toString(16) === "ff");
check("BigInt.prototype.toString.call(wrapper)", BigInt.prototype.toString.call(Object(9n)) === "9");
check("toLocaleString", typeof Object(5n).toLocaleString() === "string");
check("Object.prototype.toString", Object.prototype.toString.call(w) === "[object BigInt]");
check("String(wrapper)", String(Object(3n)) === "3" && `${Object(3n)}` === "3");
check("no own properties", Object.getOwnPropertyNames(w).length === 0);
check("JSON.stringify of a wrapper throws", threw(function () { JSON.stringify(Object(1n)); }, TypeError));

// The wrapper converts back to its BigInt wherever a primitive is needed.
check("wrapper + bigint", Object(1n) + 2n === 3n);
check("wrapper * wrapper", Object(2n) * Object(3n) === 6n);
check("loose equality", Object(1n) == 1n && Object(1n) == 1);
check("comparison", Object(2n) > 1n && Object(2n) < 3);
check("BigInt(wrapper)", BigInt(Object(7n)) === 7n);
check("unary minus", -Object(4n) === -4n);
check("division", Object(9n) / 3n === 3n && 7n / Object(2n) === 3n);
check("wrapper / number throws", threw(function () { return Object(1n) / 1; }, TypeError));
check("BigInt vs Number comparison", 2n > 1 && 3 > 2n && !(2n < NaN) && 2n < 2.5);

// A wrapper is an ordinary object: it holds properties.
w.extra = 42;
check("wrapper holds a property", w.extra === 42);

// thisBigIntValue rejects other receivers.
check("valueOf on a plain object throws", threw(function () { BigInt.prototype.valueOf.call({}); }, TypeError));
check("toString on BigInt.prototype throws", threw(function () { BigInt.prototype.toString.call(BigInt.prototype); }, TypeError));
check("valueOf on a Number wrapper throws", threw(function () { BigInt.prototype.valueOf.call(Object(1)); }, TypeError));

// --- this ------------------------------------------------------------------------
function sloppyThis() { return this; }
function strictThis() { "use strict"; return this; }
var t = sloppyThis.call(7n);
check("sloppy this is a wrapper", typeof t === "object" && t instanceof BigInt && t.valueOf() === 7n);
check("strict this is the primitive", strictThis.call(7n) === 7n);
check("method call on a primitive keeps it", (function () {
    BigInt.prototype.__selfTest = function () { "use strict"; return typeof this; };
    var r = (5n).__selfTest();
    delete BigInt.prototype.__selfTest;
    return r === "bigint";
})());

// --- writes to a BigInt primitive --------------------------------------------------
var b = 5n;
b.foo = 1;
check("sloppy write is ignored", b.foo === undefined);
check("strict write throws", threw(function () { "use strict"; var c = 5n; c.bar = 1; }, TypeError));
check("strict indexed write throws", threw(function () { "use strict"; var c = 5n; c[0] = 1; }, TypeError));
check("strict symbol-keyed write throws",
      threw(function () { "use strict"; var c = 5n; c[Symbol.iterator] = 1; }, TypeError));
check("strict write of an inherited method name throws",
      threw(function () { "use strict"; var c = 5n; c.toString = 1; }, TypeError));

if (failures) {
    console.log("bigint_wrapper: " + failures + " of " + checks + " checks failed");
    process.exit(1);
}
console.log("bigint_wrapper: all " + checks + " checks passed");
