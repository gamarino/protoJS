// Direct and indirect eval, the with statement, delete of a global, and
// eval with a spread argument.
//
// These constructs compile to opcodes the VM had no handler for (with_*,
// make_*_ref, get_ref_value, put_ref_value, delete_var, apply_eval): the
// function that reached one printed a diagnostic and returned undefined, and
// a script went on as if nothing had happened.  Direct eval returned
// undefined and the global eval threw SyntaxError.  The expected values are
// Node's.  The file is sloppy-mode code on purpose.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function errorNameOf(run) {
    try { run(); return "no exception"; } catch (e) { return e && e.name; }
}

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

// --- Direct eval -------------------------------------------------------------

check("direct eval returns the completion value", function () { return eval("1 + 1"); }, 2);
check("direct eval of a non-string returns it", function () { var o = {}; return eval(o) === o; }, true);
check("direct eval reads a local", function () { var a = 40; return eval("a + 2"); }, 42);
check("direct eval reads an argument", function () {
    return (function (p) { return eval("p * 2"); })(21);
}, 42);
check("direct eval writes a local", function () { var a = 1; eval("a = 5"); return a; }, 5);
check("direct eval reads a let in a block", function () {
    let r;
    { let b = 7; r = eval("b"); }
    return r;
}, 7);
check("direct eval sees a captured variable of an outer function", function () {
    var outer = 3;
    return (function () { return eval("outer + 1"); })();
}, 4);
check("a var declared by a sloppy direct eval is visible to the function",
      function () { eval("var ev = 3"); return ev; }, 3);
check("a function declared by a sloppy direct eval is callable",
      function () { eval("function g() { return 9; }"); return g(); }, 9);
check("direct eval sees this", function () {
    var o = { v: 11, m: function () { return eval("this.v"); } };
    return o.m();
}, 11);
check("a closure created by eval keeps the caller's variable", function () {
    var n = 1;
    var f = eval("(function () { return ++n; })");
    f(); f();
    return n;
}, 3);
check("direct eval in a loop", function () {
    var s = 0;
    for (var i = 0; i < 5; i++) s += eval("i");
    return s;
}, 10);
check("a strict direct eval keeps its vars to itself", function () {
    "use strict";
    eval("var hidden = 1");
    return typeof hidden;
}, "undefined");
check("a syntax error in direct eval is a SyntaxError", function () {
    try { eval("1 +"); return "no exception"; } catch (e) { return e instanceof SyntaxError; }
}, true);
check("an exception thrown by eval code propagates", function () {
    try { eval("throw new RangeError('r')"); return "no exception"; } catch (e) { return e.name; }
}, "RangeError");
// The specification makes eval(...args) a direct eval (§13.3.6.1; test262
// language/expressions/call/eval-spread.js).  Node 22 treats it as indirect
// and throws ReferenceError here; this case follows the specification.
check("eval with a spread argument is a direct eval", function () {
    var a = 6;
    var args = ["a * 7"];
    return eval(...args);
}, 42);
check("a renamed eval is indirect", function () {
    var a = "local";
    var e = eval;
    globalThis.a_eval_and_with = "global";
    return e("typeof a_eval_and_with");
}, "string");

// --- Indirect eval -----------------------------------------------------------

globalThis.globalForIndirect = 5;
check("indirect eval runs as global code", function () {
    var globalForIndirect = 1;
    return (0, eval)("globalForIndirect");
}, 5);
check("indirect eval declares a global var", function () {
    (0, eval)("var declaredByIndirect = 8");
    return globalThis.declaredByIndirect;
}, 8);
check("indirect eval returns the completion value", function () { return (0, eval)("2 * 21"); }, 42);
check("a let in an indirect eval stays in the eval", function () {
    (0, eval)("let letInIndirectEval = 1;");
    return typeof letInIndirectEval;
}, "undefined");

// --- with --------------------------------------------------------------------

check("with reads a property", function () {
    var o = { a: 1 };
    with (o) { return a; }
}, 1);
check("with falls back to the enclosing scope", function () {
    var o = { a: 1 };
    var b = 2;
    with (o) { return a + b; }
}, 3);
check("with writes a property", function () {
    var o = { a: 1 };
    with (o) { a = 10; }
    return o.a;
}, 10);
check("with: a name the object lacks writes the local", function () {
    var o = {};
    var c = 1;
    with (o) { c = 4; }
    return c + ":" + ("c" in o);
}, "4:false");
check("with: compound assignment", function () {
    var o = { a: 1 };
    with (o) { a += 5; }
    return o.a;
}, 6);
check("with: a method called with the object as this", function () {
    var o = { v: 3, m: function () { return this.v; } };
    with (o) { return m(); }
}, 3);
check("with: Symbol.unscopables hides a property", function () {
    var x = "outer";
    var o = { x: "inner" };
    o[Symbol.unscopables] = { x: true };
    with (o) { return x; }
}, "outer");
check("with: a getter runs", function () {
    var o = { get g() { return 12; } };
    with (o) { return g; }
}, 12);
check("with: typeof of a property", function () {
    var o = { f: function () {} };
    with (o) { return typeof f; }
}, "function");
check("with: delete of a property", function () {
    var o = { d: 1 };
    with (o) { delete d; }
    return "d" in o;
}, false);
check("with: the statement continues after the block", function () {
    var o = { a: 1 }, r = 0;
    with (o) { r = a; }
    return r + 1;
}, 2);
check("with: a closure created inside sees the object", function () {
    var o = { a: 1 };
    var f;
    with (o) { f = function () { return a; }; }
    o.a = 2;
    return f();
}, 2);
check("with: an assignment to an undeclared name creates a global", function () {
    var o = {};
    with (o) { createdInWith_eval_and_with = 7; }
    return globalThis.createdInWith_eval_and_with;
}, 7);
check("with: for-in target in the object", function () {
    var o = { k: null };
    with (o) { for (k in { p: 1 }) {} }
    return o.k;
}, "p");

check("with (undefined) throws TypeError", function () {
    return errorNameOf(function () { with (undefined) { } });
}, "TypeError");
check("with: a method call falls back to a global function", function () {
    var o = {};
    with (o) { return typeof Object(); }
}, "object");
// EvalDeclarationInstantiation checks every declaration before making any
// (§19.2.1.3; test262 eval-code/indirect/non-definable-function-with-
// function.js).  Node 22 defines the first function before throwing.
check("an eval declaring a function over a non-configurable global throws TypeError", function () {
    var r;
    try { (0, eval)("function definedByEval_x() {} function NaN() {}"); r = "no exception"; }
    catch (e) { r = e.name; }
    return r + ":" + typeof globalThis.definedByEval_x;
}, "TypeError:undefined");
// --- delete of a global ------------------------------------------------------

globalThis.deletableGlobal = 1;
check("delete of a configurable global property", function () {
    return eval("delete deletableGlobal") + ":" + ("deletableGlobal" in globalThis);
}, "true:false");
var notDeletable = 1;
check("delete of a var global is false", function () { return eval("delete notDeletable"); }, false);
check("delete of an undeclared name is true", function () { return eval("delete neverDeclared_x"); }, true);

if (failures.length) {
    console.log("eval_and_with: " + failures.length + " check(s) failed");
    for (var f = 0; f < failures.length; f++) {
        console.log("  FAIL: " + failures[f]);
    }
    process.exit(1);
}
console.log("eval_and_with: all checks passed");
