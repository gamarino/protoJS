// %TypedArray%.prototype.sort(comparefn) (ECMA-262 §23.2.3.29). protoJS
// ignored the comparator and always sorted numerically, and accepted a
// non-callable comparator. The comparator's result goes through ToNumber, NaN
// counts as +0, an exception stops the sort, and without a comparator -0 sorts
// before +0 and NaN last. Expected values were checked against Node.js.

let failures = 0;
function check(actual, expected, label) {
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) { failures++; console.log("FAIL " + label + ": expected " + e + ", got " + a); }
}
function thrownName(fn) { try { fn(); return "no throw"; } catch (e) { return e && e.name ? e.name : String(e); } }
const show = (ta) => Array.from(ta, (x) => Object.is(x, -0) ? "-0" : String(x));

check(show(new Int32Array([1, 5, 3, 2]).sort((x, y) => y - x)), ["5", "3", "2", "1"], "descending comparator");
check(show(new Float64Array([0.5, -2, 3]).sort((x, y) => y - x)), ["3", "0.5", "-2"], "float comparator");
check(show(new BigInt64Array([1n, 5n, 3n]).sort((x, y) => (x < y ? 1 : x > y ? -1 : 0))), ["5", "3", "1"], "BigInt comparator");
check(show(new Int32Array([1, 5, 3, 2]).sort((x, y) => ({ valueOf() { return x - y; } }))), ["1", "2", "3", "5"], "comparator result through ToNumber");
check(show(new Int32Array([1, 5, 3, 2]).sort(() => NaN)), ["1", "5", "3", "2"], "NaN result counts as +0 (stable)");
check(show(new Int32Array([1, 5, 3, 2]).sort((x, y) => String(y - x))), ["5", "3", "2", "1"], "string result through ToNumber");

// Stability: equal keys keep their order.
check(show(new Int32Array([21, 12, 23, 14, 25]).sort((x, y) => (x % 10) - (y % 10))),
      ["21", "12", "23", "14", "25"], "stable on the comparator's equal keys");

// Arguments: two elements, this undefined.
let argsOk = true;
new Uint8Array([3, 1, 2]).sort(function (x, y) {
    "use strict";
    if (arguments.length !== 2 || typeof x !== "number" || this !== undefined) argsOk = false;
    return x - y;
});
check(argsOk, true, "comparator receives two numbers and this undefined");

// The comparator's exception stops the sort and propagates.
let calls = 0;
check(thrownName(() => new Int8Array([3, 2, 1]).sort(() => { calls++; throw new RangeError("stop"); })), "RangeError", "comparator throws");
check(calls, 1, "the sort stops at the exception");
check(thrownName(() => new Int8Array([3, 2, 1]).sort(() => Symbol())), "TypeError", "ToNumber of a Symbol throws");

// A non-callable comparator other than undefined is a TypeError, even on an
// empty array; undefined means the default order.
check(thrownName(() => new Int8Array(0).sort(1)), "TypeError", "non-callable comparator");
check(thrownName(() => new Int8Array(2).sort(null)), "TypeError", "null comparator");
check(show(new Uint8Array([2, 1]).sort(undefined)), ["1", "2"], "undefined comparator");

// Default order: -0 before +0, NaN last.
check(show(new Float64Array([3, 1, -0, 0, NaN, 2, -1]).sort()), ["-1", "-0", "0", "1", "2", "3", "NaN"], "default order");
check(show(new Float32Array([0, -0, 0, -0]).sort()), ["-0", "-0", "0", "0"], "-0 before +0");

// sort returns the same object.
const same = new Int16Array([2, 1]);
check(same.sort((x, y) => x - y) === same, true, "returns this");

if (failures) { console.log(failures + " failure(s)"); process.exit(1); }
console.log("typed_array_sort: OK");
