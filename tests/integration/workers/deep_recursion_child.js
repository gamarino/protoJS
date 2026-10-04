// Worker half of deep_recursion.js: recurses to the depth the main thread
// passed as workerData, then without bound until the RangeError, and posts
// what it reached.
function depth(n) { return n === 0 ? 0 : 1 + depth(n - 1); }
function probe() {
    var reached = 0;
    function down() { reached++; down(); }
    try { down(); return { ok: false, depth: reached, error: 'no exception' }; }
    catch (e) {
        return { ok: e instanceof RangeError && e.message === 'Maximum call stack size exceeded',
                 depth: reached, error: String(e) };
    }
}
// protoJS exposes parentPort and workerData as globals in a worker (and has no
// require there); Node only through the module.
var wt = (typeof require === 'function') ? require('worker_threads') : null;
var port = (typeof parentPort !== 'undefined') ? parentPort : wt.parentPort;
var data = (typeof workerData !== 'undefined') ? workerData : wt.workerData;
var target = (typeof data === 'number') ? data : -1;
var result = { fixed: target < 0 ? -1 : depth(target), probe: probe() };
result.after = target < 0 ? -1 : depth(target);
port.postMessage(result);
