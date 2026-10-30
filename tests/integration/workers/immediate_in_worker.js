// setImmediate, setTimeout and setInterval inside a worker or a Deferred
// function throw: only the main script's thread runs the event loop.  Until
// 2026-10 setImmediate in a worker queued its callback on the main loop, and
// the main thread ran the worker's function concurrently with the worker.
// (Node runs a loop per worker; protoJS documents the deviation.)
// Asserting test: exits 1 on any failure.
var wt = require("worker_threads");
var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + String(got) + ", expected " + String(expected));
}

var expected = [
    "setImmediate:setImmediate() is not available in a worker thread",
    "setTimeout:setTimeout() is not available in a worker thread",
    "setInterval:setInterval() is not available in a worker thread",
].join("|");

var d = new Deferred(function () {
    try { setImmediate(function () {}); return "no-throw"; }
    catch (e) { return e.message; }
});

var w = new wt.Worker(__dirname + "/immediate_in_worker_child.js");
w.on("message", function (m) {
    check("worker", m, expected);
    d.then(function (msg) {
        check("Deferred", msg, "setImmediate() is not available inside a Deferred function");
        // The main thread still runs its own immediates.
        setImmediate(function () {
            if (failures.length) {
                for (var f of failures) console.log("FAIL " + f);
                process.exit(1);
            }
            console.log("immediate_in_worker: all checks passed");
        });
    });
});
w.on("error", function (e) { console.log("FAIL worker error " + e); process.exit(1); });
