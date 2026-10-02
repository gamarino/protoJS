// A worker thread recurses as deeply as the main thread.
//
// Every JavaScript call is a native runBytecode frame, so the recursion depth a
// thread can reach is set by its native stack. The main thread gets 8 MiB on
// Linux and macOS and the 64 MiB /STACK reservation on Windows; a thread created
// without a size gets the platform's default instead -- 512 KiB on macOS. A
// worker therefore died (SIGSEGV, the whole process) at a depth the main thread
// reaches easily. Workers now run on a thread with the main thread's
// reservation (src/platform/SizedThread.h).
//
// DEPTH is what the main thread itself must reach on every platform: protoJS
// does not raise RangeError on native stack exhaustion, it ends the process, so
// the depth is kept well inside the smallest main-thread budget (about 1,000
// calls with GCC's 7.7 KiB frame in 8 MiB).
//
// Asserting test: exits 1 when the worker does not report the full depth.

var DEPTH = 700;

function depth(n) { return n === 0 ? 0 : 1 + depth(n - 1); }

var mainDepth = depth(DEPTH);
if (mainDepth !== DEPTH) {
    console.log("FAIL: main thread reached " + mainDepth + " of " + DEPTH);
    process.exit(1);
}

var wt = require('worker_threads');
var w = new wt.Worker(__dirname + '/deep_recursion_child.js', { workerData: DEPTH });
var got = null;
w.on('message', function (m) { got = m; });
w.on('error', function (e) {
    console.log("FAIL: worker error: " + e);
    process.exit(1);
});
w.on('exit', function () {
    if (got !== DEPTH) {
        console.log("FAIL: the worker reported " + got + ", expected " + DEPTH);
        process.exit(1);
    }
    console.log("workers/deep_recursion: main and worker both reached depth " + DEPTH);
});
