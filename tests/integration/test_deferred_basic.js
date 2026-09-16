// Basic Deferred test.
//
// The Deferred function takes NO arguments: its return value fulfils the
// Deferred, and an exception it throws rejects it. The Promise-style
// `(resolve, reject)` signature this test used to assume is not supported —
// see docs/DEFERRED_USAGE.md.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " — " + detail : ""));
    }
}

check("Deferred is a function", typeof Deferred === "function",
      "got " + typeof Deferred);

var fulfilledWith;
var rejectedWith;

var d = new Deferred(function () {
    var sum = 0;
    for (var i = 0; i < 1000; i++) {
        sum += i;
    }
    return sum;
});
check("new Deferred(fn) returns an instance", d instanceof Deferred);
d.then(function (value) { fulfilledWith = value; });

var dErr = new Deferred(function () {
    throw new Error("Test error");
});
dErr['catch'](function (e) { rejectedWith = e && e.message; });

var hops = 0;
function step() {
    if (++hops < 20) {
        setImmediate(step);
        return;
    }
    check("fulfils with the value returned by the function",
          fulfilledWith === 499500, "got " + fulfilledWith);
    check("rejects with the error thrown by the function",
          rejectedWith === "Test error", "got " + rejectedWith);

    if (failures.length) {
        console.log("test_deferred_basic: " + failures.length + " check(s) failed");
        for (var i = 0; i < failures.length; i++) {
            console.log("  FAIL: " + failures[i]);
        }
        process.exit(1);
    }
    console.log("test_deferred_basic: all checks passed");
}

setImmediate(step);
