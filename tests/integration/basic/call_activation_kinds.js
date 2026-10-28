// Function activations skip the prologue steps that belong to other kinds.
//
// The interpreter's prologue decides, from flags computed once when the
// bytecode is loaded, which steps an activation needs: the async / generator
// resume hand-offs only for functions that suspend, the iterator-record close
// on unwinding only for bodies that open iterator records, the closure-slot
// pass only when a captured slot was left empty or an arrow binds `this`, and
// the class-constructor check at a call site only for class constructors.
// Each check below exercises one of those decisions on both sides.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " — " + detail : ""));
    }
}

function throwsTypeError(fn) {
    try { fn(); } catch (e) { return e instanceof TypeError; }
    return false;
}

// Class constructors called without `new`: direct, as a method, nested.
class C { constructor() { this.x = 1; } }
class D extends C { constructor() { super(); this.y = 2; } }
check("class ctor call throws", throwsTypeError(function () { C(); }));
check("derived class ctor call throws", throwsTypeError(function () { D(); }));
check("class ctor method call throws", throwsTypeError(function () { ({ C: C }).C(); }));
check("class ctor call from a nested call throws",
      throwsTypeError(function () { return (function () { return C(); })(); }));
check("class ctor with new works", new D().x === 1 && new D().y === 2);

// Plain functions with the marker-free fast path still run.
function add(a, b) { return a + b; }
var obj = { k: 3, m: function (v) { return this.k + v; } };
var sum = 0;
for (var i = 0; i < 1000; i++) sum = add(sum, i);
check("plain call", sum === 499500, "sum = " + sum);
check("plain method call", obj.m(4) === 7);

// Closures: captured locals, captured globals, an arrow binding `this`.
var G = 10;
function outer(n) {
    var local = n;
    return function inner(v) { return local + v + G; };
}
check("closure over local and global", outer(1)(2) === 13);
G = 20;
check("closure sees the updated global", outer(1)(2) === 23);
function Holder() {
    this.v = 5;
    this.get = () => this.v;
}
var h = new Holder();
var detached = h.get;
check("arrow binds this", detached() === 5);
function lateGlobal() { return laterDefined; }
var laterDefined = "late";
check("global declared after the function", lateGlobal() === "late");

// Generators and async functions still resume.
function* gen() { var a = yield 1; var b = yield a + 1; return b * 2; }
var it = gen();
var r1 = it.next(), r2 = it.next(5), r3 = it.next(7);
check("generator resumes", r1.value === 1 && r2.value === 6 && r3.value === 14 && r3.done,
      [r1.value, r2.value, r3.value].join(","));
function plainInsideGenerator(x) { return x * 3; }
function* gen2() { yield plainInsideGenerator(2); yield plainInsideGenerator(3); }
var vals = [];
for (var v of gen2()) vals.push(v);
check("plain calls inside a generator", vals.join(",") === "6,9", vals.join(","));

// `arguments` and rest parameters.
function countArgs() { return arguments.length; }
function restArgs(a, ...rest) { return rest.length; }
check("arguments", countArgs(1, 2, 3) === 3);
check("rest", restArgs(1, 2, 3) === 2);

// Iterator records are closed when an exception leaves a for-of body.
var closed = 0;
var iterable = {
    [Symbol.iterator]() {
        var n = 0;
        return {
            next() { return { value: n++, done: n > 5 }; },
            return() { closed++; return {}; }
        };
    }
};
function loopAndThrow() { for (var x of iterable) { if (x === 2) throw new Error("stop"); } }
try { loopAndThrow(); } catch (e) {}
check("for-of closes its iterator on throw", closed === 1, "closed = " + closed);
function throwsPlain() { throw new Error("plain"); }
function callsThrowsPlain() { for (var x of iterable) { throwsPlain(); } }
try { callsThrowsPlain(); } catch (e) {}
check("for-of closes its iterator when a callee throws", closed === 2, "closed = " + closed);

// Direct eval in a function still sees its locals.
function withEval() { var hidden = 41; return eval("hidden + 1"); }
check("direct eval", withEval() === 42);

var asyncDone = false;
async function asyncAdd(a, b) { await null; return add(a, b); }
asyncAdd(2, 3).then(function (v) {
    asyncDone = v === 5;
    check("async function resumes", asyncDone, "v = " + v);
    finish();
}, function (e) {
    check("async function resumes", false, String(e));
    finish();
});

function finish() {
    if (failures.length) {
        console.log("call_activation_kinds: " + failures.length + " check(s) failed");
        for (var f = 0; f < failures.length; f++) {
            console.log("  FAIL: " + failures[f]);
        }
        process.exit(1);
    }
    console.log("call_activation_kinds: all checks passed");
}
