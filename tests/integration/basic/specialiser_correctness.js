// The bytecode specialiser (src/runtime/BytecodeSpecialiser.h) must not
// change what a program computes.
//
// It adds fused instructions for `s += i`, `i < n` and `i < N` (N a global)
// loop shapes.  The pass used to replace the original instructions: it fused
// sequences that a jump entered in the middle (the ternaries below ran the
// wrong number of times), it did not re-encode `catch` and `gosub` offsets (a
// loop inside a try made the handler jump into garbage and the function
// return undefined), and the fused handlers had slow paths of their own that
// differed from `add` / `lt` for BigInts, objects and undeclared globals.
// Every case runs in a function, where the operands are locals, and the
// expected values are Node's.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

// `run` is called inside a try, so one failing case does not hide the others.
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

function throwsName(fn) {
    try { fn(); return "no exception"; } catch (e) { return e && e.name; }
}

// --- Jump targets inside a fusable sequence ---------------------------------

function ternaryLoopTest(x) {
    let i = 0, n = 4, count = 0;
    while ((x ? i : n) < n) { i++; count++; }
    return count;
}
check("a loop test entered from both arms of a ternary (x true)", () => ternaryLoopTest(true), 4);
check("a loop test entered from both arms of a ternary (x false)", () => ternaryLoopTest(false), 0);

function ternaryAccumulator(c) {
    let s = 0, a = 100;
    for (let i = 0; i < 3; i++) { s = (c ? a : s) + i; }
    return s;
}
check("an accumulator entered from both arms of a ternary (c true)", () => ternaryAccumulator(true), 102);
check("an accumulator entered from both arms of a ternary (c false)", () => ternaryAccumulator(false), 3);

// --- Code offsets of try / catch / finally ----------------------------------

function loopInTry() {
    let r = 0;
    try {
        for (let i = 0; i < 3; i++) { r += i; }
        throw new Error("x");
    } catch (e) {
        r += 100;
    }
    return r;
}
check("a fused loop inside try: the catch handler still runs", () => loopInTry(), 103);

function loopInTryFinally() {
    let r = 0;
    try {
        for (let i = 0; i < 4; i++) { r += i; }
    } finally {
        r += 1000;
    }
    return r;
}
check("a fused loop inside try/finally: the finally block still runs", () => loopInTryFinally(), 1006);

function loopAfterTry() {
    let r = 0;
    try { r = 1; } catch (e) { r = -1; }
    for (let i = 0; i < 5; i++) { r += i; }
    try { throw 7; } catch (e) { r += e; }
    return r;
}
check("fused loops between try blocks", () => loopAfterTry(), 18);

function nestedTryLoops() {
    let r = 0;
    for (let i = 0; i < 3; i++) {
        try {
            for (let j = 0; j < 3; j++) { r += j; }
            if (i === 1) throw i;
        } catch (e) {
            r += 10;
        } finally {
            r += 100;
        }
    }
    return r;
}
check("fused loops in nested try/catch/finally", () => nestedTryLoops(), 319);

// --- Operands that are not SmallIntegers ------------------------------------

function accObjectToPrimitive() {
    let s = 1;
    const o = { [Symbol.toPrimitive]() { return 41; } };
    for (let k = 0; k < 1; k++) { s += o; }
    return s;
}
check("s += o with Symbol.toPrimitive", () => accObjectToPrimitive(), 42);

function accArray() {
    let s = 1;
    const a = [2];
    for (let k = 0; k < 1; k++) { s += a; }
    return s;
}
check("s += [2] concatenates", () => accArray(), "12");

function accString() {
    let s = "a";
    let t = "b";
    for (let k = 0; k < 2; k++) { s += t; }
    return s;
}
check("s += t with strings", () => accString(), "abb");

function accDouble() {
    let s = 0.5;
    let d = 0.25;
    for (let k = 0; k < 2; k++) { s += d; }
    return s;
}
check("s += d with doubles", () => accDouble(), 1);

function accOverflow() {
    let s = 2 ** 53 - 2;
    let one = 1;
    for (let k = 0; k < 4; k++) { s += one; }
    return s;
}
check("s += 1 past the SmallInteger range", () => accOverflow(), 2 ** 53);

function accBigInt() {
    let s = 1n;
    let t = 2n;
    for (let k = 0; k < 3; k++) { s += t; }
    return s;
}
check("BigInt s += t", () => accBigInt(), 7n);

function accBigIntAddLoc() {
    var x = 1n, y = 2n;
    x += y;
    return x;
}
check("BigInt x += y (var locals)", () => accBigIntAddLoc(), 3n);

function ltBigInt() {
    let count = 0;
    let n = 3n;
    for (let i = 0n; i < n; i++) { count++; }
    return count;
}
check("BigInt loop test i < n", () => ltBigInt(), 3);

function ltMixed() {
    let count = 0;
    let n = 2.5;
    for (let i = 0; i < n; i++) { count++; }
    return count;
}
check("loop test against a double bound", () => ltMixed(), 3);

function ltObject() {
    let count = 0;
    const n = { valueOf() { return 3; } };
    for (let i = 0; i < n; i++) { count++; }
    return count;
}
check("loop test against an object with valueOf", () => ltObject(), 3);

function ltString() {
    let a = "a", b = "b";
    let r = 0;
    while (a < b) { r++; a = "c"; }
    return r;
}
check("loop test on strings", () => ltString(), 1);

function ltUndeclaredGlobal() {
    for (let i = 0; i < notDeclaredAnywhere_specialiser; i++) { }
    return "no exception";
}
check("loop test against an undeclared global throws ReferenceError",
      () => throwsName(ltUndeclaredGlobal), "ReferenceError");

var GLOBAL_BOUND = 5;
function ltGlobal() {
    let s = 0;
    for (let i = 0; i < GLOBAL_BOUND; i++) { s += i; }
    return s;
}
check("loop test against a global bound", () => ltGlobal(), 10);

var GLOBAL_BIG = 3n;
function ltGlobalBigInt() {
    let c = 0;
    for (let i = 0n; i < GLOBAL_BIG; i++) { c++; }
    return c;
}
check("loop test against a global BigInt bound", () => ltGlobalBigInt(), 3);

function tdzInLoop() {
    let s = 0;
    for (let k = 0; k < 1; k++) {
        s += late;  // eslint-disable-line no-use-before-define
    }
    let late = 1;
    return s;
}
check("an accumulator reading a let in its TDZ throws ReferenceError",
      () => throwsName(tdzInLoop), "ReferenceError");

function capturedAccumulator() {
    let s = 0;
    const get = () => s;
    for (let i = 0; i < 4; i++) { s += i; }
    return get();
}
check("an accumulator on a captured local writes through its cell", () => capturedAccumulator(), 6);

// --- A long loop body: short jumps around fused code ------------------------

function longBody(n) {
    let s = 0, t = 0;
    for (let i = 0; i < n; i++) {
        s += i; t += s; s += i; t += s; s += i; t += s; s += i; t += s;
        s += i; t += s; s += i; t += s; s += i; t += s; s += i; t += s;
        s += i; t += s; s += i; t += s; s += i; t += s; s += i; t += s;
    }
    return s + t;
}
check("a loop with many fused accumulators", () => longBody(10), 21330);

if (failures.length) {
    console.log("specialiser_correctness: " + failures.length + " check(s) failed");
    for (var f = 0; f < failures.length; f++) {
        console.log("  FAIL: " + failures[f]);
    }
    process.exit(1);
}
console.log("specialiser_correctness: all checks passed");
