// Array.from and Array.of with a `this` that is a constructor other than
// Array (Array.from.call(C, items)) write each element by
// CreateDataPropertyOrThrow on the object C returns.  Each write used to
// reset the index's descriptor sidecar "__pd_<i>__" unconditionally, which
// interned one perennial symbol per index in protoCore's symbol table, whose
// lookups get slower as it grows.  Against protoCore fec4394c that took
// 1,072 ms for n = 10,000 (32 ms now), and the 1,000,002-element call at the
// end of this test did not finish in 600 s.  The sidecar is now reset only
// when it exists, so no symbol is interned per index.
//
// The scaling check is the ratio of the time over 4n elements to the time
// over n, as in array_bulk_scaling.js (best of three; a ratio above 10
// measured once more; a run over 4n that takes under 5 ms passes).  The
// 1,000,002-element call must cost at most 10 times a plain Array.from.
// The test prints the work it did.  Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}

function C() {}
var N = 10000;
var offset = 0;   // each sample stores different values

function fromArrayLike(n) {
    var src = { length: n };
    var r = Array.from.call(C, src, function (_, i) { return i + offset; });
    offset++;
    return r.length + r[n - 1];
}
function fromIterable(n) {
    var a = new Array(n);
    for (var i = 0; i < n; i++) a[i] = i;
    var r = Array.from.call(C, a);
    return r.length + r[n - 1];
}

function sample(f, n) {
    var runs = 0, t0 = Date.now(), t, work = 0;
    do { work += f(n); runs++; t = Date.now() - t0; } while (t < 60);
    return { ms: t / runs, work: work };
}
function measure(f) {
    f(4 * N);
    var ts = Infinity, tl = Infinity;
    for (var r = 0; r < 3; r++) {
        ts = Math.min(ts, sample(f, N).ms);
        tl = Math.min(tl, sample(f, 4 * N).ms);
    }
    return { ts: ts, tl: tl, ratio: tl / ts };
}
function scales(name, f) {
    var m = measure(f);
    if (m.ratio > 10 && m.tl > 5) m = measure(f);
    console.log(name + ": " + m.ts.toFixed(2) + " ms for n = " + N + ", " + m.tl.toFixed(2) +
                " ms for 4n (ratio " + m.ratio.toFixed(1) + ")");
    if (m.ratio > 10 && m.tl > 5) failures.push(name + ": ratio " + m.ratio.toFixed(1));
}

scales("Array.from.call(C, arrayLike)", fromArrayLike);
scales("Array.from.call(C, iterable)", fromIterable);
scales("Array.of.apply(C, list)", function (n) {
    var a = new Array(n);
    for (var i = 0; i < n; i++) a[i] = i;
    var r = Array.of.apply(C, a);
    return r.length + r[n - 1];
});

// The results are what the spec says.
var r = Array.from.call(C, { length: 3, 0: "a", 1: "b", 2: "c" });
check("instance of C", r instanceof C, true);
check("length", r.length, 3);
check("elements", r[0] + r[1] + r[2], "abc");
var d = Object.getOwnPropertyDescriptor(r, "1");
check("descriptor", JSON.stringify(d), JSON.stringify({ value: "b", writable: true, enumerable: true, configurable: true }));
check("keys", Object.keys(r).join(), "0,1,2,length");
// An index the constructor made read-only and hidden is redefined as a fresh
// data property (CreateDataProperty, not [[Set]]).
function D() { Object.defineProperty(this, "0", { value: "x", writable: false, enumerable: false, configurable: true }); }
var rd = Array.from.call(D, ["y"]);
var dd = Object.getOwnPropertyDescriptor(rd, "0");
check("redefined value", dd.value, "y");
check("redefined writable", dd.writable, true);
check("redefined enumerable", dd.enumerable, true);
var ro = Array.of.call(D, "z");
check("Array.of redefines", JSON.stringify(Object.getOwnPropertyDescriptor(ro, "0")),
      JSON.stringify({ value: "z", writable: true, enumerable: true, configurable: true }));
// A non-configurable index cannot be redefined: TypeError.
function E() { Object.defineProperty(this, "0", { value: 1, writable: true, enumerable: true, configurable: false }); }
var threw = false;
try { Array.from.call(E, [2]); } catch (e) { threw = e instanceof TypeError; }
check("non-configurable index throws", threw, true);
// Indices of seven digits and more.
// Its cost is that of a plain Array.from over the same input, give or take
// a small factor (1.7 here against protoCore fec4394c); at most 10.
var tPlain = Date.now();
var plain = Array.from({ length: 1000002 }, function (_, i) { return i; });
tPlain = Date.now() - tPlain;
var tBig = Date.now();
var big = Array.from.call(C, { length: 1000002 }, function (_, i) { return i; });
tBig = Date.now() - tBig;
console.log("1,000,002 elements: Array.from.call(C, ...) " + tBig + " ms, Array.from(...) " + tPlain + " ms");
check("plain Array.from result", plain[1000001], 1000001);
if (tBig > 10 * Math.max(tPlain, 50)) failures.push("Array.from.call(C) over 1,000,002 elements: " + tBig + " ms, more than 10x Array.from's " + tPlain + " ms");
check("seven-digit index", big[1000001], 1000001);
check("seven-digit length", big.length, 1000002);

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("array_from_ctor_scaling: all checks passed");
