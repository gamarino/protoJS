// require() of a relative JavaScript file.
//
// The module body used to never run: `executeModule` compiled the wrapper,
// ran it against a TypeBridge copy of the QuickJS global, kept the bytecode
// module in a stack local, and dropped any exception, so `require('./x.js')`
// returned an object with no exports.
//
// The invariant this test exists to protect: a function exported by a module
// resolves its bytecode against the module that created it, never against the
// module that happens to be executing when it is called. Two modules with
// same-named functions must not resolve to each other's bodies.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " — " + detail : ""));
    }
}

// Call fn and describe the outcome without ever aborting the run: when the
// feature is broken the callee is typically not callable, and a raw call would
// end the script instead of reporting every failing check.
function safeCall(fn, args) {
    if (typeof fn !== "function") return "<" + typeof fn + ", not callable>";
    try {
        return fn.apply(null, args || []);
    } catch (e) {
        return "<threw " + e.name + ": " + e.message + ">";
    }
}

// --- exports.x form --------------------------------------------------------

var a = require('./fixtures/exports_form.js');
check("exports form: module is an object", a && typeof a === "object",
      "got " + typeof a);
check("exports form: value is exported", a && a.x === 42, "x = " + (a && a.x));
check("exports form: function is exported and callable",
      a && typeof a.add === "function" && a.add(2, 3) === 5,
      "typeof add = " + (a && typeof a.add));

// --- module.exports = {...} form ------------------------------------------

var b = require('./fixtures/module_exports_form.js');
check("module.exports form: replaced object is returned",
      b && b.y === 7, "y = " + (b && b.y));
check("module.exports form: function is callable",
      b && typeof b.f === "function" && b.f() === 7,
      "typeof f = " + (b && typeof b.f));

// --- closures over module locals ------------------------------------------

var c = require('./fixtures/closure_counter.js');
check("closure module: inc is a function", c && typeof c.inc === "function",
      "typeof inc = " + (c && typeof c.inc));
if (c && typeof c.inc === "function") {
    check("closure keeps module state across calls (1)", c.inc() === 1);
    check("closure keeps module state across calls (2)", c.inc() === 2);
}
check("module sees its own __filename",
      c && typeof c.filename === "string" && c.filename.indexOf("closure_counter.js") !== -1,
      "__filename = " + (c && c.filename));
check("module sees its own __dirname",
      c && typeof c.dirname === "string" && c.dirname.indexOf("fixtures") !== -1,
      "__dirname = " + (c && c.dirname));

// --- nested require resolves relative to the requiring module -------------

var d = require('./fixtures/nested_outer.js');
check("nested require works", d && d.v === 42, "v = " + (d && d.v));

// --- caching ---------------------------------------------------------------

var e1 = require('./fixtures/side_effect.js');
var e2 = require('./fixtures/side_effect.js');
check("same module object on a second require", e1 === e2);
check("module body runs only once", e1 && e1.runs === 1,
      "runs = " + (e1 && e1.runs));

// --- a throwing module -----------------------------------------------------

var firstError = null;
try {
    require('./fixtures/throwing.js');
} catch (err) {
    firstError = err;
}
check("a throwing module propagates its error", firstError !== null,
      "no error was raised");
check("the error message reaches the caller",
      firstError && String(firstError.message).indexOf("module blew up") !== -1,
      "message = " + (firstError && firstError.message));

// A failed module must not be cached as a successful one.
var secondError = null;
try {
    require('./fixtures/throwing.js');
} catch (err) {
    secondError = err;
}
check("a throwing module is not cached as successful", secondError !== null,
      "the second require did not throw");

// --- two modules must never resolve to each other's bytecode ---------------
//
// same_name_a.js and same_name_b.js declare identically-named functions in the
// same order, so their bytecode IDs coincide. A closure that resolved its ID
// against the requiring script — or against whichever module is current on the
// thread — would not fail loudly here; it would quietly run the other module's
// body and return the wrong answer.

var sa = require('./fixtures/same_name_a.js');
var sb = require('./fixtures/same_name_b.js');

check("module A resolves its own bytecode", safeCall(sa && sa.whoami) === 'A',
      "A.whoami() = " + safeCall(sa && sa.whoami));
check("module B resolves its own bytecode", safeCall(sb && sb.whoami) === 'B',
      "B.whoami() = " + safeCall(sb && sb.whoami));
check("same-named functions stay independent",
      safeCall(sa && sa.scale, [10]) === 20 && safeCall(sb && sb.scale, [10]) === 30,
      "A.scale(10) = " + safeCall(sa && sa.scale, [10]) +
      ", B.scale(10) = " + safeCall(sb && sb.scale, [10]));
// Re-checked after B is loaded: a per-module stamp overwritten by the module
// loaded later would show up only on this second reading of A.
check("module A still resolves its own bytecode after B loaded",
      safeCall(sa && sa.whoami) === 'A', "A.whoami() = " + safeCall(sa && sa.whoami));

// --- closures created after the module body returned -----------------------

var fac = require('./fixtures/factory.js');
check("factory module: make is a function", fac && typeof fac.make === "function",
      "typeof make = " + (fac && typeof fac.make));
var add10 = safeCall(fac && fac.make, [10]);
check("factory returns a callable closure", typeof add10 === "function",
      "got " + (typeof add10 === "object" ? JSON.stringify(add10) : String(add10)));
check("a closure built after load captures its argument",
      safeCall(add10, [5]) === 15, "add10(5) = " + safeCall(add10, [5]));

// --- a require cycle terminates --------------------------------------------
//
// The module record is published before the body runs, so cycle_b sees
// cycle_a's half-filled exports instead of recursing forever.

var cyc = require('./fixtures/cycle_a.js');
check("a require cycle completes", cyc && cyc.after === 'a-after',
      "after = " + (cyc && cyc.after));
check("the cycle partner ran", cyc && cyc.bValue === 'b-value',
      "bValue = " + (cyc && cyc.bValue));
check("the partner saw the half-initialised exports",
      cyc && cyc.seenFromB === 'a-before', "seenFromB = " + (cyc && cyc.seenFromB));

// --- the runtime is still healthy afterwards -------------------------------

check("native calls still work after a module threw", JSON.parse("1") === 1);
check("built-in require still works", require('path') === path);

if (failures.length) {
    console.log("test_require_file: " + failures.length + " check(s) failed");
    for (var f = 0; f < failures.length; f++) {
        console.log("  FAIL: " + failures[f]);
    }
    process.exit(1);
}
console.log("test_require_file: all checks passed");
