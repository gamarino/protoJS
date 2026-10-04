// Deep recursion on the main thread and in a worker: a fixed depth succeeds,
// and unbounded recursion throws a catchable RangeError instead of ending the
// process.
//
// Every JavaScript call is a native runBytecode frame, so the recursion depth a
// thread can reach is set by its native stack. The main thread gets 8 MiB on
// Linux and macOS and the /STACK reservation on Windows (256 MiB); a thread
// created without a size gets the platform's default instead -- 512 KiB on
// macOS. A worker therefore died (SIGSEGV, the whole process) at a depth the
// main thread reaches easily. Workers now run on a thread with the main
// thread's reservation (src/platform/SizedThread.h).
//
// Running out of native stack used to end the process (SIGSEGV; on Windows at
// about 720 calls with the 64 MiB reservation then in use, because MSVC gives
// runBytecode a frame of about 90 KiB). runBytecode now checks the remaining
// native stack on entry and throws RangeError ("Maximum call stack size
// exceeded") while a safety margin is left (src/runtime/NativeStackGuard.h).
//
// MIN_DEPTH is the depth every platform must reach before the RangeError: about
// 2,300 calls fit in Linux's 8 MiB with GCC's 3.3 KiB frame. The depths reached
// are printed, so the CI log records them for each platform.
//
// Asserting test: exits 1 on any failure.

var DEPTH = 700;
var MIN_DEPTH = 2000;

function depth(n) { return n === 0 ? 0 : 1 + depth(n - 1); }

// Recurse until the RangeError; report it and the depth reached.
function probe() {
    var reached = 0;
    function down() { reached++; down(); }
    try { down(); return { ok: false, depth: reached, error: 'no exception' }; }
    catch (e) {
        return { ok: e instanceof RangeError && e.message === 'Maximum call stack size exceeded',
                 depth: reached, error: String(e) };
    }
}

function verify(where, result) {
    var failures = [];
    if (result.fixed !== DEPTH) failures.push(where + ': reached ' + result.fixed + ' of ' + DEPTH);
    if (!result.probe.ok) failures.push(where + ': unbounded recursion gave ' + result.probe.error);
    else if (result.probe.depth < MIN_DEPTH)
        failures.push(where + ': RangeError at depth ' + result.probe.depth + ', below ' + MIN_DEPTH);
    if (result.after !== DEPTH) failures.push(where + ': after the RangeError reached ' + result.after);
    return failures;
}

var main = { fixed: depth(DEPTH), probe: probe() };
main.after = depth(DEPTH);   // the stack is usable again after the RangeError
var failures = verify('main thread', main);

var wt = require('worker_threads');
var w = new wt.Worker(__dirname + '/deep_recursion_child.js', { workerData: DEPTH });
var got = null;
w.on('message', function (m) { got = m; });
w.on('error', function (e) {
    console.log("FAIL: worker error: " + e);
    process.exit(1);
});
w.on('exit', function () {
    if (!got || typeof got !== 'object') failures.push('worker: reported ' + JSON.stringify(got));
    else failures = failures.concat(verify('worker', got));
    if (failures.length) {
        console.log('FAIL:\n  ' + failures.join('\n  '));
        process.exit(1);
    }
    console.log("workers/deep_recursion: main and worker reached depth " + DEPTH +
                "; RangeError at depth " + main.probe.depth + " (main) and " +
                got.probe.depth + " (worker)");
});
