// A worker_threads worker has its own job queue: promise reactions and awaits
// in the worker's script run in the worker's checkpoint, in job order, and the
// result reaches the main thread.
//
// Asserting test: exits 1 when the worker does not report the expected sum
// and order.
var N = 50;
var wt = require('worker_threads');
var w = new wt.Worker(__dirname + '/promises_in_worker_child.js', { workerData: N });
var got = null;
w.on('message', function (m) { got = m; });
w.on('error', function (e) {
    console.log("FAIL: worker error: " + e);
    process.exit(1);
});
w.on('exit', function () {
    var expectedSum = N * (N + 1) / 2;
    if (!got || got.sum !== expectedSum || got.order !== 'sync,reaction,await') {
        console.log("FAIL: the worker reported " + JSON.stringify(got));
        process.exit(1);
    }
    // The main thread's own queue keeps working alongside.
    Promise.resolve(got.sum).then(function (s) {
        console.log("workers/promises_in_worker: sum " + s + ", order " + got.order);
    });
});
