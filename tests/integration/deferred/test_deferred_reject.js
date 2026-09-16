// Regression test for Deferred rejection semantics.
//
// Covers the defects reported as J5:
//   - `new Deferred(fn)` threw "function is not a constructor";
//   - an exception thrown by the worker function FULFILLED the Deferred with
//     undefined instead of rejecting it, so `catch` never ran;
//   - `then(onFulfilled, onRejected)` ignored its second argument;
//   - a callback that threw left the interpreter's pending-exception flag set
//     for whatever native code ran next.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.
// Run: protojs tests/integration/deferred/test_deferred_reject.js

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " — " + detail : ""));
    }
}

var seen = {};
function mark(key, value) { seen[key] = value; }
function saw(key) { return Object.prototype.hasOwnProperty.call(seen, key); }

// ---- Synchronous surface -------------------------------------------------

check("typeof Deferred === 'function'", typeof Deferred === "function",
      "got " + typeof Deferred);

var constructed = null;
var constructError = null;
try {
    constructed = new Deferred(function () { return 7; });
} catch (e) {
    constructError = e;
}
check("new Deferred(fn) does not throw", constructError === null,
      constructError && (constructError.name + ": " + constructError.message));
check("new Deferred(fn) returns an object", constructed !== null && typeof constructed === "object");
if (constructed) {
    check("new Deferred(fn) instanceof Deferred", constructed instanceof Deferred);
    check("instance has then", typeof constructed.then === "function");
    check("instance has catch", typeof constructed['catch'] === "function");
}

// Deferred without a function must fail immediately, not hang the event loop
// until the 180 s drain timeout.
var noFnError = null;
try {
    Deferred();
    noFnError = "no throw";
} catch (e) {
    noFnError = e;
}
check("Deferred() without a function throws",
      noFnError !== "no throw" && noFnError && noFnError.name === "TypeError",
      "got " + (noFnError && (noFnError.name || noFnError)));

var badFnError = null;
try {
    Deferred(42);
    badFnError = "no throw";
} catch (e) {
    badFnError = e;
}
check("Deferred(non-callable) throws TypeError",
      badFnError !== "no throw" && badFnError && badFnError.name === "TypeError",
      "got " + (badFnError && (badFnError.name || badFnError)));

// ---- Fulfilment ----------------------------------------------------------

if (constructed) {
    constructed.then(function (v) { mark("fulfilValue", v); });
}

// ---- Rejection with an Error object --------------------------------------

var thrownError = new Error("boom");
var dErr = new Deferred(function () { throw thrownError; });
dErr['catch'](function (e) { mark("catchError", e); });
dErr.then(function (v) { mark("thenRanOnRejection", v); });

// ---- Rejection with a primitive ------------------------------------------

var dPrim = new Deferred(function () { throw 42; });
dPrim['catch'](function (e) { mark("catchPrimitive", e); });

// ---- then(onFulfilled, onRejected) ---------------------------------------

var dTwoArg = new Deferred(function () { throw new Error("two-arg"); });
dTwoArg.then(
    function (v) { mark("twoArgOnFulfilled", v); },
    function (e) { mark("twoArgOnRejected", e && e.message); }
);

// A fulfilling Deferred must still call the first argument.
var dTwoArgOk = new Deferred(function () { return 5; });
dTwoArgOk.then(
    function (v) { mark("twoArgFulfilValue", v); },
    function (e) { mark("twoArgUnexpectedReject", e); }
);

// ---- A callback that throws must not corrupt later work ------------------

var dThrowingCallback = new Deferred(function () { return 1; });
dThrowingCallback.then(function () { throw new Error("callback blows up"); });

// ---- Watchdog ------------------------------------------------------------
// Deferred callbacks run on later event-loop turns. Chain setImmediate hops,
// attach a late catch on the way, then verify everything that had to happen.

var hops = 0;
var TOTAL_HOPS = 40;
var lateCatchAttached = false;

function step() {
    hops++;

    // Attach `catch` well after the Deferred has already been rejected.
    if (hops === 10 && !lateCatchAttached) {
        lateCatchAttached = true;
        dPrim['catch'](function (e) { mark("lateCatch", e); });
    }

    // After the throwing callback has run, native calls and new Deferreds
    // must still work (no stale pending-exception flag).
    if (hops === 20) {
        var parsed = null;
        try {
            parsed = JSON.parse("1");
        } catch (e) {
            parsed = "threw: " + e;
        }
        mark("jsonAfterThrow", parsed);

        var dAfter = new Deferred(function () { return 99; });
        dAfter.then(function (v) { mark("deferredAfterThrow", v); });
    }

    if (hops < TOTAL_HOPS) {
        setImmediate(step);
        return;
    }
    finish();
}

function finish() {
    check("fulfil value delivered to then", seen.fulfilValue === 7,
          "got " + seen.fulfilValue);

    check("throw inside the worker rejects: catch ran", saw("catchError"),
          "catch callback never ran");
    check("catch receives the thrown object unchanged", seen.catchError === thrownError,
          "got " + seen.catchError);
    check("then does not run for a rejected Deferred", !saw("thenRanOnRejection"),
          "then ran with " + seen.thenRanOnRejection);

    check("throwing a primitive rejects with that value", seen.catchPrimitive === 42,
          "got " + seen.catchPrimitive);

    check("then(onFulfilled, onRejected) calls onRejected",
          seen.twoArgOnRejected === "two-arg", "got " + seen.twoArgOnRejected);
    check("then(onFulfilled, onRejected) does not call onFulfilled on rejection",
          !saw("twoArgOnFulfilled"), "onFulfilled ran with " + seen.twoArgOnFulfilled);
    check("then(onFulfilled, onRejected) calls onFulfilled on fulfilment",
          seen.twoArgFulfilValue === 5, "got " + seen.twoArgFulfilValue);
    check("then(onFulfilled, onRejected) does not call onRejected on fulfilment",
          !saw("twoArgUnexpectedReject"));

    check("catch attached after rejection still fires", seen.lateCatch === 42,
          "got " + seen.lateCatch);

    check("native calls work after a callback threw", seen.jsonAfterThrow === 1,
          "got " + seen.jsonAfterThrow);
    check("a later Deferred still resolves after a callback threw",
          seen.deferredAfterThrow === 99, "got " + seen.deferredAfterThrow);

    if (failures.length) {
        console.log("test_deferred_reject: " + failures.length + " check(s) failed");
        for (var i = 0; i < failures.length; i++) {
            console.log("  FAIL: " + failures[i]);
        }
        process.exit(1);
    }
    console.log("test_deferred_reject: all checks passed");
}

setImmediate(step);
