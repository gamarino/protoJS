// Loose equality of objects, the truthiness of 0n, TDZ checks on assignment,
// computed-key conversion, and IteratorClose for for-of and destructuring.
//
// Before these fixes `({}) == ({})` and `[] == []` were true (both operands
// went through ToPrimitive), `0n` was truthy, assignments to a `let` in its
// TDZ succeeded, an object literal's computed key was converted after its
// value was evaluated,
// an exception thrown by return() on `break` was swallowed and an exception
// thrown in a for-of body did not call return().  The expected values are
// Node's.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, run, expected) {
    var actual;
    try {
        actual = run();
    } catch (e) {
        actual = "exception " + (e && e.name) + ": " + (e && e.message);
    }
    if (actual !== expected) {
        failures.push(name + " — got " + String(actual) + ", expected " + String(expected));
    }
}

function errorName(run) {
    try { run(); return "no exception"; } catch (e) { return e && e.name; }
}

// --- Loose equality ------------------------------------------------------------

check("({}) == ({}) is false", function () { return ({}) == ({}); }, false);
check("[] == [] is false", function () { return [] == []; }, false);
check("an object equals itself", function () { var o = {}; return o == o; }, true);
check("({}) != ({}) is true", function () { return ({}) != ({}); }, true);
check("an object never equals null", function () {
    return ({ valueOf: function () { return null; } }) == null;
}, false);
check("an object never equals undefined", function () { return ({}) == undefined; }, false);
check("[1] == 1 converts the array", function () { return [1] == 1; }, true);
check("[] == '' converts the array", function () { return [] == ""; }, true);
check("an object with valueOf equals a number", function () {
    return ({ valueOf: function () { return 3; } }) == 3;
}, true);
check("[0] == false", function () { return [0] == false; }, true);
check("two functions are not equal", function () {
    return (function () {}) == (function () {});
}, false);
check("null == undefined", function () { return null == undefined; }, true);
check("1n == 1", function () { return 1n == 1; }, true);

// --- Truthiness of BigInt --------------------------------------------------------

check("0n is falsy", function () { return 0n ? "truthy" : "falsy"; }, "falsy");
check("!0n", function () { return !0n; }, true);
check("1n is truthy", function () { return 1n ? "truthy" : "falsy"; }, "truthy");
check("-1n is truthy", function () { return !!-1n; }, true);
check("a large BigInt is truthy", function () { return !!(2n ** 80n); }, true);
check("Object(0n) is truthy", function () { return !!Object(0n); }, true);
check("0n || 'x'", function () { return 0n || "x"; }, "x");
check("Boolean(0n)", function () { return Boolean(0n); }, false);

// --- TDZ on assignment -------------------------------------------------------------

check("assigning a local let before its declaration throws", function () {
    return errorName(function () { x = 1; let x; });
}, "ReferenceError");
check("compound assignment to a local let in its TDZ throws", function () {
    return errorName(function () { x += 1; let x = 0; });
}, "ReferenceError");
check("assigning a captured let before its declaration throws", function () {
    return errorName(function () {
        function set() { y = 1; }
        set();
        let y;
    });
}, "ReferenceError");
check("assigning a let after its declaration works", function () {
    let z; z = 3; return z;
}, 3);
check("a closure assigning a captured let after its declaration works", function () {
    let w = 1;
    (function () { w = 2; })();
    return w;
}, 2);
check("assigning a global let before its declaration throws", function () {
    return errorName(function () { (0, eval)("tdzGlobalLet = 1; let tdzGlobalLet;"); });
}, "ReferenceError");
check("typeof a global let in its TDZ throws", function () {
    return errorName(function () { (0, eval)("typeof tdzGlobalLet2; let tdzGlobalLet2;"); });
}, "ReferenceError");

// --- Computed keys -------------------------------------------------------------------

// An object literal converts a computed key before the value expression runs
// (OP_to_propkey); an assignment target converts it when the value is
// stored, after the right-hand side, and `o[k] += 1` converts it for the read
// and again for the write, as in Node.
check("computed keys are converted where the specification says", function () {
    var order = [];
    var key = { toString: function () { order.push("key"); return "k"; } };
    var o = {};
    o[key] = (order.push("value"), 1);
    var lit = { [key]: (order.push("value2"), 2) };
    return order.join(",") + "|" + o.k + lit.k;
}, "value,key,key,value2|12");
check("o[k] += 1 converts k for the read and the write", function () {
    var n = 0;
    var key = { toString: function () { n++; return "c"; } };
    var o = { c: 1 };
    o[key] += 1;
    return n + ":" + o.c;
}, "2:2");
check("a key's toString is preferred to valueOf", function () {
    var key = { toString: function () { return "t"; }, valueOf: function () { return "v"; } };
    var o = {};
    o[key] = 1;
    return Object.keys(o).join(",");
}, "t");

// --- IteratorClose ---------------------------------------------------------------------

function makeIterable(log, opts) {
    opts = opts || {};
    var it = {
        i: 0,
        next: function () { this.i++; return { value: this.i, done: this.i > 5 }; },
        return: function () {
            log.push("return");
            if (opts.throwInReturn) throw new Error("from return");
            return opts.badResult ? 1 : {};
        }
    };
    var iterable = {};
    iterable[Symbol.iterator] = function () { return it; };
    return iterable;
}

check("break calls return()", function () {
    var log = [];
    for (var v of makeIterable(log)) { if (v === 2) break; }
    return log.join(",");
}, "return");
check("an exception from return() on break propagates", function () {
    var log = [];
    return errorName(function () {
        for (var v of makeIterable(log, { throwInReturn: true })) { break; }
    }) + ":" + log.join(",");
}, "Error:return");
check("a non-object result of return() on break is a TypeError", function () {
    var log = [];
    return errorName(function () {
        for (var v of makeIterable(log, { badResult: true })) { break; }
    });
}, "TypeError");
check("an exception in the body calls return() and propagates", function () {
    var log = [];
    try {
        for (var v of makeIterable(log)) { throw new RangeError("body"); }
    } catch (e) {
        log.push(e.name);
    }
    return log.join(",");
}, "return,RangeError");
check("an exception in the body wins over one from return()", function () {
    var log = [];
    try {
        for (var v of makeIterable(log, { throwInReturn: true })) { throw new RangeError("body"); }
    } catch (e) {
        log.push(e.name);
    }
    return log.join(",");
}, "return,RangeError");
check("an exception leaving the function calls return()", function () {
    var log = [];
    function inner() { for (var v of makeIterable(log)) { throw new TypeError("t"); } }
    try { inner(); } catch (e) { log.push(e.name); }
    return log.join(",");
}, "return,TypeError");
check("return from inside for-of calls return()", function () {
    var log = [];
    function inner() { for (var v of makeIterable(log)) { return v; } }
    var r = inner();
    return r + ":" + log.join(",");
}, "1:return");
check("a completed loop does not call return()", function () {
    var log = [];
    for (var v of makeIterable(log)) { }
    return log.length;
}, 0);
check("nested loops: break out of the inner one closes only it", function () {
    var log = [];
    var outer = 0;
    for (var a of [1, 2]) {
        for (var b of makeIterable(log)) { break; }
        outer++;
    }
    return outer + ":" + log.join(",");
}, "2:return,return");
check("destructuring that does not exhaust the iterator calls return()", function () {
    var log = [];
    var [p, q] = makeIterable(log);
    return p + q + ":" + log.join(",");
}, "3:return");
check("a generator's finally runs when for-of breaks", function () {
    var log = [];
    function* g() { try { yield 1; yield 2; } finally { log.push("finally"); } }
    for (var v of g()) { break; }
    return log.join(",");
}, "finally");
check("a generator's finally runs when the for-of body throws", function () {
    var log = [];
    function* g() { try { yield 1; yield 2; } finally { log.push("finally"); } }
    try { for (var v of g()) { throw new Error("x"); } } catch (e) { log.push("caught"); }
    return log.join(",");
}, "finally,caught");

if (failures.length) {
    console.log("semantics_equality_tdz_iterators: " + failures.length + " check(s) failed");
    for (var f = 0; f < failures.length; f++) {
        console.log("  FAIL: " + failures[f]);
    }
    process.exit(1);
}
console.log("semantics_equality_tdz_iterators: all checks passed");
