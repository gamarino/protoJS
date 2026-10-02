// Worker half of deep_recursion.js: recurses to the depth the main thread
// passed as workerData and posts the depth it reached.
function depth(n) { return n === 0 ? 0 : 1 + depth(n - 1); }
var target = (typeof workerData === 'number') ? workerData : -1;
parentPort.postMessage(target < 0 ? -1 : depth(target));
