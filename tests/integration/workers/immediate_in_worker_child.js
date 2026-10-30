// Child of immediate_in_worker.js: setImmediate, setTimeout and setInterval
// throw in a worker, which has no event loop of its own.
var r = [];
function probe(name, f) {
    try { f(); r.push(name + ":no-throw"); }
    catch (e) { r.push(name + ":" + (e instanceof Error ? e.message : String(e))); }
}
probe("setImmediate", function () { setImmediate(function () { r.push("immediate-ran"); }); });
probe("setTimeout", function () { setTimeout(function () {}, 1); });
probe("setInterval", function () { setInterval(function () {}, 1); });
parentPort.postMessage(r.join("|"));
