// require() of a native addon (shared library), ABI v2.
//
// Under ABI v1 the addon exported QuickJS values, which `TypeBridge::fromJS`
// turned into empty objects: `typeof addon.sum` was "object" and calling it
// threw "is not a function". Under v2 the addon builds protoCore objects and
// its functions are called directly by the interpreter.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " — " + detail : ""));
    }
}

// The addon is built next to the binary, so look under both build directories.
// A relative specifier resolves against THIS script's directory, so the paths
// are written relative to tests/integration/native_addons/ and work whether
// protojs was given a relative or an absolute path for this file.
var candidates = [
    '../../../build_release/tests/native_addons/simple/simple',
    '../../../build/tests/native_addons/simple/simple'
];

var addon = null;
var lastError = null;
for (var i = 0; i < candidates.length && addon === null; i++) {
    try {
        addon = require(candidates[i]);
    } catch (e) {
        lastError = e;
    }
}

if (addon === null) {
    console.log("test_native_require: could not load the simple addon from any of:");
    for (var c = 0; c < candidates.length; c++) console.log("  " + candidates[c]);
    console.log("  last error: " + (lastError && lastError.message));
    process.exit(1);
}

// --- values ---------------------------------------------------------------

check("addon exports a number", addon.version === 1, "version = " + addon.version);

// --- callable functions ---------------------------------------------------

check("exported function has type 'function'", typeof addon.sum === "function",
      "typeof sum = " + typeof addon.sum);
if (typeof addon.sum === "function") {
    check("exported function is callable", addon.sum(2, 3) === 5,
          "sum(2,3) = " + addon.sum(2, 3));
    check("exported function handles other values", addon.sum(0.5, 0.25) === 0.75,
          "sum(0.5,0.25) = " + addon.sum(0.5, 0.25));
}

// --- exceptions raised by the addon ---------------------------------------

check("throwing helper is a function", typeof addon.boom === "function",
      "typeof boom = " + typeof addon.boom);
if (typeof addon.boom === "function") {
    var threw = null;
    try {
        addon.boom();
    } catch (e) {
        threw = e;
    }
    check("an addon exception is catchable", threw !== null, "boom() did not throw");
    check("the addon exception keeps its type",
          threw && threw.name === "TypeError",
          "name = " + (threw && threw.name));
    check("the addon exception keeps its message",
          threw && String(threw.message).indexOf("addon refused") !== -1,
          "message = " + (threw && threw.message));
}

// --- identity -------------------------------------------------------------

var again = null;
for (var j = 0; j < candidates.length && again === null; j++) {
    try {
        again = require(candidates[j]);
    } catch (e) {
        // try the next candidate
    }
}
check("requiring the addon twice returns the same exports", again === addon);

// --- the runtime is still healthy -----------------------------------------

check("native calls still work after an addon exception", JSON.parse("1") === 1);

if (failures.length) {
    console.log("test_native_require: " + failures.length + " check(s) failed");
    for (var f = 0; f < failures.length; f++) {
        console.log("  FAIL: " + failures[f]);
    }
    process.exit(1);
}
console.log("test_native_require: all checks passed");
