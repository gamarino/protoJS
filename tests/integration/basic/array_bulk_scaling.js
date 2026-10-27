// Array methods that build a new array from a native one cost O(n log n),
// not O(n^2). slice, splice and flat wrote their result one
// CreateDataPropertyOrThrow at a time when ArraySpeciesCreate had left holes
// in it, and each write interned a per-index descriptor key ("__pd_<i>__")
// in protoCore's symbol table, whose lookup is linear in the number of
// symbols: 2 s for slice(1) of 40,000 elements, 6.5 s for a splice.
//
// The check is the ratio of the time over 4n elements to the time over n,
// as in string_access_scaling.js (alternating samples, best of three, a
// ratio above 10 measured once more; a run over 4n that takes under 5 ms
// passes). Asserting test: exits 1 on any failure.

var failures = [];
var N = 10000;

function sample(f, a) {
    var runs = 0, t0 = Date.now(), t;
    do { f(a); runs++; t = Date.now() - t0; } while (t < 60);
    return t / runs;
}

function measure(f, small, large) {
    f(large);
    var ts = Infinity, tl = Infinity;
    for (var r = 0; r < 3; r++) {
        ts = Math.min(ts, sample(f, small));
        tl = Math.min(tl, sample(f, large));
    }
    return { ts: ts, tl: tl, ratio: tl / ts };
}

function make(n) { var a = []; for (var i = 0; i < n; i++) a.push(i); return a; }

function scales(name, f) {
    var small = make(N), large = make(4 * N);
    var rs = f(small), rl = f(large);
    if (rs === rl) failures.push(name + ": same result for n and 4n (" + rs + ")");
    var m = measure(f, small, large);
    // Below 5 ms for 4n the ratio measures caches, not the algorithm (a
    // quadratic path takes seconds there).
    if (m.ratio > 10 && m.tl > 5) m = measure(f, small, large);
    if (m.ratio > 10 && m.tl > 5)
        failures.push(name + ": " + m.ts.toFixed(2) + " ms for n, " + m.tl.toFixed(2) + " ms for 4n (ratio " + m.ratio.toFixed(1) + ")");
}

scales("slice(1)", function (a) { var r = a.slice(1); return r.length + r[r.length - 1]; });
scales("slice()", function (a) { return a.slice().length; });
scales("splice(1, n - 2)", function (a) { var c = a.slice(), r = c.splice(1, c.length - 2); return r.length * 3 + c.length; });
scales("splice(1, 1, x, y)", function (a) { var c = a.slice(); c.splice(1, 1, "x", "y"); return c.length; });
scales("flat()", function (a) { var r = [a, [1, 2]].flat(); return r.length; });
scales("flat() of pairs", function (a) { var p = a.map(function (x) { return [x, x]; }); return p.flat().length; });
scales("flatMap", function (a) { return a.flatMap(function (x) { return [x]; }).length; });
scales("concat", function (a) { return a.concat(a).length; });
scales("map", function (a) { return a.map(function (x) { return x; }).length; });
scales("filter", function (a) { return a.filter(function (x) { return x % 2; }).length; });
scales("spread", function (a) { return [...a].length; });

// The results.
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}
var a = make(6);
check("slice", a.slice(2, 5).join(), "2,3,4");
check("slice negative", a.slice(-2).join(), "4,5");
var c = a.slice(), removed = c.splice(1, 3, "x");
check("splice removed", removed.join(), "1,2,3");
check("splice result", c.join(), "0,x,4,5");
check("splice length", c.length, 4);
check("splice length property", Object.getOwnPropertyDescriptor(c, "length").value, 4);
check("flat", [1, [2, [3, [4]]], , 5].flat(2).join("|"), "1|2|3|4|5");
check("flat holes and undefined", [[undefined, , 1]].flat().length, 2);
check("flat keeps undefined", 0 in [[undefined]].flat(), true);
// The bulk paths keep the per-element semantics: a hole reads through the
// prototype chain, and a species constructor's own properties are
// redefined by CreateDataPropertyOrThrow.
Array.prototype[1] = "inherited";
var holey = [0];
holey.length = 3;
var copy = holey.slice();
check("slice reads a hole through the prototype", Object.prototype.hasOwnProperty.call(copy, "1") && copy[1] === "inherited", true);
check("splice reads a hole through the prototype", Object.prototype.hasOwnProperty.call(holey.slice().splice(0, 3), "1"), true);
delete Array.prototype[1];
var sp = [1, 2];
sp.constructor = {};
sp.constructor[Symbol.species] = function () {
    var q = new Array(0);
    Object.defineProperty(q, 0, { value: 0, writable: false, configurable: true, enumerable: false });
    return q;
};
var spr = sp.slice(0), spd = Object.getOwnPropertyDescriptor(spr, "0");
check("species target redefined by slice", spd.value === 1 && spd.writable && spd.enumerable, true);
class Sub extends Array {}
var s = Sub.from([1, 2, 3]);
check("slice of a subclass", s.slice(1) instanceof Sub, true);
check("splice of a subclass", s.splice(0, 1) instanceof Sub, true);
check("flat of a subclass", Sub.from([[1], [2]]).flat() instanceof Sub, true);

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("array_bulk_scaling: all checks passed");
