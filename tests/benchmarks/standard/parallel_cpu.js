// Heavy parallel CPU benchmark: designed to expose protoJS multithreading advantage.
//
// Intended design:
//   - protoJS: 4 Deferred tasks (CPU thread pool) in parallel → wall time ~1/4 of sequential.
//   - Node: same total work sequentially in main thread.
// Current limitation: Deferred requires serializable functions (no complex closures), so we
// run sequential for both and output __BENCH_RESULT__. When Deferred supports simpler
// executors or Worker .on('message') is available, protoJS can switch to parallel and
// the runner can use process wall time for protoJS (see run_standard_comparison.js).
//
// Same total CPU work (4 * 2e6 iterations). Median of 5 runs.

var NUM_TASKS = 4;
var WORK_PER_TASK = 2e6;

function runOneChunk() {
    var sum = 0;
    for (var i = 0; i < WORK_PER_TASK; i++) {
        sum += i;
    }
    return sum;
}

var times = [];
for (var k = 0; k < 5; k++) {
    var start = Date.now();
    for (var t = 0; t < NUM_TASKS; t++) {
        runOneChunk();
    }
    times.push(Date.now() - start);
}
times.sort(function (a, b) { return a - b; });
var median = times[Math.floor(times.length / 2)];
var result = { name: 'parallel_cpu', time_ms: median, iterations: 5, tasks: NUM_TASKS };
console.log('__BENCH_RESULT__' + JSON.stringify(result));
