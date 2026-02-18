// Worker script for parallel_cpu benchmark: runs one chunk of CPU work and posts result.
// Expects workerData.WORK_PER_TASK (default 2e6). parentPort.postMessage({}) when done.

var WORK_PER_TASK = 2e6;
if (typeof workerData !== 'undefined' && workerData && typeof workerData.WORK_PER_TASK === 'number') {
    WORK_PER_TASK = workerData.WORK_PER_TASK;
}

function runOneChunk() {
    var sum = 0;
    for (var i = 0; i < WORK_PER_TASK; i++) {
        sum += i;
    }
    return sum;
}

runOneChunk();
if (typeof parentPort !== 'undefined' && parentPort && parentPort.postMessage) {
    parentPort.postMessage({ done: true });
}
