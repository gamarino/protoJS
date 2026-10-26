// The modules eval() compiles live while something can still run their code.
//
// The eval cache keeps at most 64 compilations (src/runtime/EvalModuleCache.h);
// a module that leaves it stays alive while the eval runs or while a closure it
// created is reachable, and is released once neither holds. Closures of every
// kind made by eval code are kept here while hundreds of other distinct sources
// push their modules out of the cache, under a small heap ceiling so that
// collections run (tests/CMakeLists.txt sets PROTOCORE_HEAP_LIMIT_CELLS), and
// are called afterwards: a released module would run freed code. The expected
// values are Node's.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, actual, expected) {
    if (actual !== expected)
        failures.push(name + " -- got " + String(actual) + ", expected " + String(expected));
}

var junk = [];
function churn(tag, n) {
    // Distinct sources (each compiles and evicts) and garbage (collections).
    var s = 0;
    for (var i = 0; i < n; i++) {
        s += eval("(" + i + " + " + tag + ")");
        junk = [{ a: i, b: [i, i + 1, i + 2], c: "v" + i }];
    }
    return s;
}

// --- Closures of evicted modules ---------------------------------------------

var adders = [];
for (var k = 0; k < 200; k++)
    adders.push(eval("(function (a) { return function (b) { return a + b + " + k + "; }; })")(k));
var classes = [];
for (var k2 = 0; k2 < 100; k2++)
    classes.push(eval("(class C" + k2 + " { constructor(x) { this.x = x; } get twice() { return this.x * 2 + " + k2 + "; } static make(x) { return new this(x); } })"));
var gen = eval("(function* () { var i = 0; while (true) yield i++ * 10; })")();
check("generator first value", gen.next().value, 0);
var arrows = [];
for (var k3 = 0; k3 < 100; k3++)
    arrows.push(eval("(x => y => x * y + " + k3 + ")")(3));
var methods = eval("({ m() { return 'm'; }, get g() { return 'g'; }, ['c' + 1]() { return 'c1'; } })");

check("churn", churn(1000, 3000), 3000 * 1000 + (3000 * 2999) / 2);

var adderSum = 0;
for (var j = 0; j < adders.length; j++) adderSum += adders[j](1);
check("adders after eviction", adderSum, 200 * 1 + 2 * (199 * 200) / 2);
var classSum = 0;
for (var j2 = 0; j2 < classes.length; j2++) classSum += classes[j2].make(j2).twice;
check("classes after eviction", classSum, 3 * (99 * 100) / 2);
check("generator after eviction", gen.next().value + gen.next().value, 30);
var arrowSum = 0;
for (var j3 = 0; j3 < arrows.length; j3++) arrowSum += arrows[j3](2);
check("arrows after eviction", arrowSum, 600 + (99 * 100) / 2);
check("methods after eviction", methods.m() + methods.g + methods.c1(), "mgc1");

// --- A closure made by eval code made by eval code ---------------------------

var inner = eval("(function () { return eval('(function (z) { return z + 1; })'); })")();
churn(2000, 500);
check("nested eval closure", inner(41), 42);

// --- Evals that run while others evict them ----------------------------------

function nested(k) {
    // Each level evals a distinct source that evals 100 more distinct sources.
    return eval("var r = 0; for (var j = 0; j < 100; j++) r += eval('j + " + k + "'); r");
}
var nestedSum = 0;
for (var n = 0; n < 20; n++) nestedSum += nested(n);
check("nested evals", nestedSum, 20 * 4950 + 100 * (19 * 20) / 2);

function recurse(depth) {
    // 120 distinct evals active at once: more than the cache holds. (Deeper
    // recursion through eval reaches QuickJS's 1 MB parser stack check, on
    // master as well, at about 145 levels.)
    return depth === 0 ? 0 : eval("recurse(" + (depth - 1) + ") + 1");
}
check("recursive evals deeper than the cache", recurse(120), 120);

// --- Repeated source: one compilation, same results ---------------------------

function repeated(n) {
    var s = 0;
    for (var i = 0; i < n; i++) s += eval("i * 2");
    return s;
}
check("repeated eval", repeated(20000), 20000 * 19999);
check("indirect eval", (0, eval)("typeof globalThis"), "object");

// --- Async functions made by eval, resumed after eviction ---------------------

var asyncFn = eval("(async function (v) { await null; await null; return v * 7; })");
var pending = asyncFn(6);
churn(3000, 500);
pending.then(function (v) {
    check("async after eviction", v, 42);
    finish();
}, function (e) {
    failures.push("async after eviction -- rejected: " + e);
    finish();
});

function finish() {
    if (failures.length) {
        for (var f = 0; f < failures.length; f++) console.log("  FAIL: " + failures[f]);
        process.exit(1);
    }
    console.log("eval_module_lifetime: all checks passed");
}
