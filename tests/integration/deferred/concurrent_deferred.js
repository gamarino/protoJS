// Several Deferreds in flight at the same time.
//
// A Deferred runs its function on a later turn of the event loop, on the
// thread that runs the script — not on a worker thread. This test checks that
// several pending Deferreds all settle, and that the values are not mixed up.
// For genuinely parallel CPU work see protoCore.runInThread.
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

var COUNT = 5;
var results = [];

function makeDeferred(index) {
    var d = new Deferred(function () {
        var sum = 0;
        for (var j = 0; j < 100000; j++) {
            sum += j;
        }
        return index * 1000000 + sum;
    });
    d.then(function (value) { results[index] = value; });
    d['catch'](function (e) { results[index] = "rejected: " + e; });
    return d;
}

var deferreds = [];
for (var i = 0; i < COUNT; i++) {
    deferreds.push(makeDeferred(i));
}
check("created " + COUNT + " deferreds", deferreds.length === COUNT);

var hops = 0;
function step() {
    if (++hops < 30) {
        setImmediate(step);
        return;
    }
    // sum of 0..99999
    var expectedSum = 4999950000;
    for (var k = 0; k < COUNT; k++) {
        check("deferred " + k + " settled", results[k] !== undefined,
              "no callback ran");
        check("deferred " + k + " fulfils with its own value",
              results[k] === k * 1000000 + expectedSum, "got " + results[k]);
    }

    if (failures.length) {
        console.log("concurrent_deferred: " + failures.length + " check(s) failed");
        for (var f = 0; f < failures.length; f++) {
            console.log("  FAIL: " + failures[f]);
        }
        process.exit(1);
    }
    console.log("concurrent_deferred: all checks passed");
}

setImmediate(step);
