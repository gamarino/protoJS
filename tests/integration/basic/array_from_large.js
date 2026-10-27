// Array.from produces every element of its source, however many there are.
//
// The general (non-string) iterator path stopped after 100,001 elements, so
// Array.from over a longer generator, Set or user iterator silently returned
// a truncated array; Object.fromEntries had the same cap, and Map.groupBy /
// Object.groupBy / the Set methods stopped at 1,048,576 or 1,000,000 steps.
// The array-like path also turned {length: Infinity} into an empty array
// where ToLength gives 2^53 - 1 and ArrayCreate throws a RangeError.
//
// Expected values are Node.js's (v22). Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}

// An iterator that is not a generator: a plain object with next().
function counter(n) {
    var i = 0;
    var it = { next: function () { return i < n ? { value: i++, done: false } : { value: undefined, done: true }; } };
    it[Symbol.iterator] = function () { return it; };
    return it;
}
function* gen(n) { for (var i = 0; i < n; i++) yield i; }

var N = 150000;
var a = Array.from(counter(N));
check("iterator length", a.length, N);
check("iterator last", a[N - 1], N - 1);
check("iterator past the old cap", a[100001], 100001);

var m = Array.from(counter(N), function (x, i) { return x + i; });
check("mapped length", m.length, N);
check("mapped last", m[N - 1], 2 * (N - 1));

var g = Array.from(gen(120000));
check("generator length", g.length, 120000);
check("generator last", g[119999], 119999);

var set = new Set(); for (var i = 0; i < 110000; i++) set.add(i);
var fs = Array.from(set);
check("Set length", fs.length, 110000);
check("Set last", fs[109999], 109999);

var big = Array.from({ length: 250000 }, function (v, i) { return i; });
check("array-like length", big.length, 250000);
check("array-like last", big[249999], 249999);

var fe = Object.fromEntries(Array.from(counter(110000), function (i) { return ["k" + i, i]; }));
check("fromEntries size", Object.keys(fe).length, 110000);
check("fromEntries last", fe.k109999, 109999);

// Results are ordinary dense arrays: no holes, own indices, push works.
var h = Array.from({ length: 3 });
check("array-like of undefined has index 0", 0 in h, true);
check("array-like of undefined keys", Object.keys(h).join(), "0,1,2");
check("array-like of undefined JSON", JSON.stringify(h), "[null,null,null]");
var sparse = Array.from({ length: 4, 0: "x", 2: "z" });
check("missing array-like element is undefined, not a hole", 1 in sparse, true);
check("sparse array-like JSON", JSON.stringify(sparse), '["x",null,"z",null]');
var u = Array.from((function* () { yield undefined; yield; })());
check("yielded undefined is an element", (0 in u) && (1 in u) && u.length === 2, true);
var p = Array.from(counter(3));
p.push(9);
check("push after Array.from", p.join(), "0,1,2,9");
check("is an array", Array.isArray(p), true);
check("own names", Object.getOwnPropertyNames(Array.from(counter(2))).join(), "0,1,length");

// ToLength and ArrayCreate.
function errorName(f) { try { f(); return "none"; } catch (e) { return e.constructor.name; } }
check("length Infinity", errorName(function () { Array.from({ length: Infinity }); }), "RangeError");
check("length 2^32", errorName(function () { Array.from({ length: Math.pow(2, 32) }); }), "RangeError");
check("length -1", Array.from({ length: -1 }).length, 0);
check("length NaN", Array.from({ length: NaN }).length, 0);

// Object.groupBy and Map.groupBy over an iterator, past nothing in particular:
// the loop has no step limit any more.
var grouped = Object.groupBy(counter(1000), function (x) { return x % 2 ? "odd" : "even"; });
check("groupBy", grouped.odd.length + grouped.even.length, 1000);
check("Map.groupBy", Map.groupBy(counter(1000), function (x) { return x % 3; }).get(2).length, 333);

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("array_from_large: all checks passed");
