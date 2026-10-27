// Array.prototype.push keeps the `length` property up to date. Its fast
// path wrote only the elements list (9f45980ad); a.length synthesised the
// length from the list, but every other reader of the property saw the
// length before the first push: [...a], f(...a), f.apply(o, a),
// Reflect.get(a, "length"), a["length"] through a variable, the property
// descriptor, an object inheriting from the array. Before
// 2026-10-27 `[...a]` of an array built by push was empty. A push on an
// array with trailing holes (new Array(3)) also wrote index 0 instead of 3.
// Expected values are Node.js 22's. Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}
function count() { return arguments.length; }
var a = [];
a.push(1); a.push(2, 3);
var k = "length";
check("a.length", a.length, 3);
check("spread", [...a].length, 3);
check("spread call", count(...a), 3);
check("apply", count.apply(null, a), 3);
check("Reflect.apply", Reflect.apply(count, null, a), 3);
check("Reflect.get", Reflect.get(a, "length"), 3);
check("computed key", a[k], 3);
check("descriptor", Object.getOwnPropertyDescriptor(a, "length").value, 3);
check("descriptors", Object.getOwnPropertyDescriptors(a).length.value, 3);
check("inherited", Object.create(a).length, 3);
check("Proxy", new Proxy(a, {}).length, 3);
check("new Array(...a)", new Array(...a).length, 3);
check("bind", count.bind(null, ...a)(), 3);
check("rest", (function (...r) { return r.length; })(...a), 3);
check("lastIndexOf", a.lastIndexOf(3), 2);
check("hasOwn", Object.hasOwn(a, "2"), true);
var h = new Array(3);
h.push(1);
check("push after holes: length", h.length, 4);
check("push after holes: index", h[3], 1);
check("push after holes: hole", 0 in h, false);
var g = [1, 2];
g.length = 5;
g.push(9);
check("push after length grow", g.length + ":" + g[5] + ":" + (2 in g), "6:9:false");
var big = [];
for (var i = 0; i < 5000; i++) big.push(i);
check("spread of 5000", [...big].length, 5000);
check("Math.max(...)", Math.max(...big), 4999);

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("array_push_length: all checks passed");
