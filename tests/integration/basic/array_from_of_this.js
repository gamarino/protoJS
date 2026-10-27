// Array.from and Array.of construct their result with `this` (ECMA-262
// §23.1.2.1, §23.1.2.3): Construct(C) or Construct(C, «len»). A class
// constructor was called instead of constructed, so `MyA.from(...)` of a
// class that extends Array was a plain Array and a base class threw
// "Class constructor cannot be invoked without 'new'". The species
// constructor of slice, map, filter, splice, flat and concat had the same
// bug.
// Expected values are Node.js 22's. Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}
class MyA extends Array {}
var a = MyA.from([1, 2, 3]), b = MyA.of(4, 5);
check("from: instanceof", a instanceof MyA, true);
check("from: length", a.length, 3);
check("from: constructor", a.constructor === MyA, true);
check("from: Array.isArray", Array.isArray(a), true);
// ArraySpeciesCreate constructs the subclass the same way (§10.4.2.3): it
// called Array's inherited native constructor and gave a plain Array.
check("species: map", a.map(function (x) { return x * 2; }) instanceof MyA, true);
check("species: map result", a.map(function (x) { return x * 2; }).join(), "2,4,6");
check("species: filter", a.filter(function (x) { return x > 1; }) instanceof MyA, true);
check("species: concat", a.concat([4]) instanceof MyA, true);
check("of: instanceof", b instanceof MyA, true);
check("of: elements", b.join(), "4,5");
check("from iterable", MyA.from(new Set([1, 2])) instanceof MyA, true);
check("from string", MyA.from("ab").length, 2);
class Base { constructor(n) { this.n = arguments.length ? n : "none"; } }
var c = Array.from.call(Base, [1, 2]);
check("base class: instanceof", c instanceof Base, true);
check("base class: no argument for an iterable", c.n, "none");
check("base class: elements", c.length + ":" + c[1], "2:2");
var d = Array.from.call(Base, { length: 2, 0: "x", 1: "y" });
check("base class: array-like gets the length", d.n, 2);
var e = Array.of.call(Base, 7, 8);
check("of on a base class", e instanceof Base && e.n === 2 && e[1] === 8, true);
function F() { this.args = arguments.length; }
var f = Array.from.call(F, { length: 2, 0: "x", 1: "y" });
check("function: instanceof", f instanceof F, true);
check("function: length argument", f.args, 1);
check("plain call", Array.from.call(undefined, [1]).constructor === Array, true);
check("Array itself", Array.from([1, 2]).constructor === Array, true);
check("not a constructor", Array.from.call(Math.max, [1]).constructor === Array, true);

if (failures.length) {
    for (var x of failures) console.log("FAIL " + x);
    process.exit(1);
}
console.log("array_from_of_this: all checks passed");
