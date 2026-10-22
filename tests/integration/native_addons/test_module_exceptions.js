// Errors of a module written in C++ are caught by JavaScript like its own.
//
// protoCore's example geometry module (protoCore/examples/exceptions/) defines
// an error hierarchy -- GeometryError, and NegativeRadius and
// DegenerateTriangle below it -- as plain protoCore prototypes, and raises
// instances of them as proto::ProtoException.  protoJS throws every JavaScript
// exception as that same C++ type, so a `catch` here receives the module's
// error value unchanged, and selects it by its prototype chain
// (docs/EXCEPTIONS.md):
//
//   - `e instanceof geometry.NegativeRadius`: the module's type is not a
//     function, which `instanceof` requires; for a type defined outside
//     JavaScript protoJS tests it against e's prototype chain instead
//     (a platform extension, docs/CONFORMANCE.md);
//   - `Object.prototype.isPrototypeOf.call(geometry.NegativeRadius, e)`: the
//     spec-only idiom, for code that must run unchanged elsewhere.
//
// It also checks the two other ways an error reaches JavaScript from native
// code: protoCore's own errors arrive as JavaScript errors through the
// runtime's error factory (a malformed integer literal is a SyntaxError, a
// BigInt remainder by zero a RangeError), and a C++ standard exception a
// module lets escape (legacyArea throws std::runtime_error) arrives as a
// JavaScript Error carrying what().
//
// The module is reached by protoJS's own module import: the geometry addon
// (tests/native_addons/geometry) registers the module's UMD provider, and the
// script then requires the bare specifier 'example/geometry'.  ctest passes
// the addon's path; run by hand, the script falls back to the conventional
// build directories.
//
// Asserting test: prints one PASS / FAIL line per check, exits 1 on a failure.
// There is no Node oracle: Node cannot load a protoCore module.

var failures = 0;
function check(name, condition, detail) {
    if (condition) {
        console.log("PASS: " + name);
    } else {
        failures++;
        console.log("FAIL: " + name + (detail !== undefined ? " -- " + detail : ""));
    }
}

var candidates = process.argv.length > 2
    ? [process.argv[2]]
    : ['../../../build_release/tests/native_addons/geometry/geometry',
       '../../../build/tests/native_addons/geometry/geometry'];
var installed = null;
for (var i = 0; i < candidates.length && installed === null; i++) {
    try { installed = require(candidates[i]); } catch (e) { /* the next one */ }
}
if (installed === null) {
    console.log("FAIL: could not load the geometry addon from " + candidates.join(", "));
    process.exit(1);
}

var geometry = require('example/geometry');
check("the module is imported through UMD", typeof geometry.circleArea === "function");
check("its error types are values the script can name",
      typeof geometry.NegativeRadius === "object" && typeof geometry.GeometryError === "object");

// --- No error: the module answers a value. ---------------------------------
var area = geometry.circleArea(2);
check("circleArea(2) is 4*pi", area > 12.56 && area < 12.57, String(area));

// --- circleArea(-1) raises NegativeRadius. ----------------------------------
var e1 = null;
try {
    geometry.circleArea(-1);
} catch (e) {
    e1 = e;
}
check("circleArea(-1) raises", e1 !== null);
check("caught naming NegativeRadius", e1 instanceof geometry.NegativeRadius);
check("caught naming its base type GeometryError", e1 instanceof geometry.GeometryError);
check("not a DegenerateTriangle", !(e1 instanceof geometry.DegenerateTriangle));
check("not a JavaScript Error", !(e1 instanceof Error));
check("Object.prototype.isPrototypeOf.call(NegativeRadius, e)",
      Object.prototype.isPrototypeOf.call(geometry.NegativeRadius, e1) === true);
check("Object.prototype.isPrototypeOf.call(GeometryError, e)",
      Object.prototype.isPrototypeOf.call(geometry.GeometryError, e1) === true);
check("Object.prototype.isPrototypeOf.call(DegenerateTriangle, e) is false",
      Object.prototype.isPrototypeOf.call(geometry.DegenerateTriangle, e1) === false);
check("e.name is the type's name", e1 && e1.name === "NegativeRadius", e1 && e1.name);
check("e.message is the module's message",
      e1 && e1.message === "circleArea: the radius -1.000000 is negative", e1 && e1.message);
check("String(e) is \"<name>: <message>\"",
      String(e1) === "NegativeRadius: circleArea: the radius -1.000000 is negative", String(e1));

// --- A handler selects by type and rethrows what it does not handle. ---------
function areaOrZero(a, b, c) {
    try {
        return geometry.triangleArea(a, b, c);
    } catch (e) {
        if (!(e instanceof geometry.GeometryError)) throw e;   // not ours
        return 0;
    }
}
check("triangleArea(3, 4, 5) is 6", areaOrZero(3, 4, 5) === 6);
check("a degenerate triangle is caught as a GeometryError", areaOrZero(1, 2, 10) === 0);

var e2 = null;
try {
    geometry.triangleArea(1, 2, 10);
} catch (e) {
    e2 = e;
}
check("triangleArea(1, 2, 10) raises DegenerateTriangle",
      e2 instanceof geometry.DegenerateTriangle && !(e2 instanceof geometry.NegativeRadius));

// A handler for an unrelated type -- a JavaScript class, and another of the
// module's own types -- does not take the error; the same value reaches the
// next handler out.
class NetworkError extends Error {}
var innerRan = false, inner = null, outer = null;
try {
    try {
        geometry.circleArea(-5);
    } catch (e) {
        inner = e;
        if (!(e instanceof NetworkError) && !(e instanceof geometry.DegenerateTriangle)) throw e;
        innerRan = true;
    }
} catch (e) {
    outer = e;
}
check("an unrelated handler does not take it", !innerRan && outer !== null);
check("the rethrown error is the same value", outer === inner && outer instanceof geometry.NegativeRadius);

// --- protoCore's own errors arrive as JavaScript errors (the factory). --------
var e3 = null;
try {
    geometry.parseRadius("ten");
} catch (e) {
    e3 = e;
}
check("parseRadius(\"ten\") raises a SyntaxError (InvalidArgument)",
      e3 instanceof SyntaxError, e3 && (e3.name + ": " + e3.message));
check("... whose message is protoCore's", e3 && typeof e3.message === "string" && e3.message.length > 0);

var e4 = null;
try {
    var r = 1n % 0n;
} catch (e) {
    e4 = e;
}
check("1n % 0n raises a RangeError (DivisionByZero)", e4 instanceof RangeError,
      e4 && (e4.name + ": " + e4.message));
check("... \"Division by zero\"", e4 && e4.message === "Division by zero", e4 && e4.message);

var e5 = null;
try {
    var q = 1n / 0n;
} catch (e) {
    e5 = e;
}
check("1n / 0n raises a RangeError", e5 instanceof RangeError && e5.message === "Division by zero");

// --- A C++ standard exception arrives as a JavaScript Error. -----------------
var e6 = null;
try {
    geometry.legacyArea(-1);
} catch (e) {
    e6 = e;
}
check("legacyArea(-1) (std::runtime_error) raises an Error",
      e6 instanceof Error && e6.constructor === Error, e6 && e6.name);
check("... whose message is what()", e6 && e6.message === "legacyArea: negative radius",
      e6 && e6.message);
check("legacyArea(2) still answers a value", geometry.legacyArea(2) > 12.56);

// Through a native that calls back (Array.prototype.map) and a finally.
var e7 = null, finallyRan = false;
try {
    try {
        [1, -1].map(function (r) { return geometry.legacyArea(r); });
    } finally {
        finallyRan = true;
    }
} catch (e) {
    e7 = e;
}
check("through a callback and a finally", finallyRan && e7 instanceof Error
      && e7.message === "legacyArea: negative radius");

// --- An async function rejects with the module's error. -----------------------
async function asyncArea(r) {
    return geometry.circleArea(r);
}
asyncArea(-2).then(
    function () { check("an async function rejects with NegativeRadius", false); finish(); },
    function (e) {
        check("an async function rejects with NegativeRadius", e instanceof geometry.NegativeRadius);
        finish();
    });

function finish() {
    if (failures) {
        console.log("test_module_exceptions: " + failures + " check(s) failed");
        process.exit(1);
    }
    console.log("test_module_exceptions: all checks passed");
}
