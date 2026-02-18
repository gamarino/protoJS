// Worker script for parallel_cpu benchmark: heavy CPU work per worker.
// Used by both protoJS and Node when running in parallel mode.
var WORK = 2e6;  // iterations per worker

function doWork() {
    var sum = 0;
    for (var i = 0; i < WORK; i++) {
        sum += i;
    }
    return sum;
}

doWork();

if (typeof parentPort !== 'undefined' && parentPort.postMessage) {
    parentPort.postMessage('done');
} else if (typeof self !== 'undefined' && self.postMessage) {
    self.postMessage('done');
}
