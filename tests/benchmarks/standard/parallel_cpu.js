// Heavy parallel CPU benchmark: designed to expose protoJS multithreading advantage.
//
// - protoJS: uses worker_threads (4 workers) when available; measures wall time over 5 runs, reports median.
// - Node: same total CPU work run sequentially in main thread (no workers); median of 5 runs.
//
// Same total CPU work per run (4 * WORK_PER_TASK iterations). Runner uses __BENCH_RESULT__.time_ms for comparison.

var NUM_TASKS = 4;
var WORK_PER_TASK = 2e6;

function runOneChunk() {
    var sum = 0;
    for (var i = 0; i < WORK_PER_TASK; i++) {
        sum += i;
    }
    return sum;
}

function runSequential() {
    var times = [];
    for (var k = 0; k < 5; k++) {
        var start = Date.now();
        for (var t = 0; t < NUM_TASKS; t++) {
            runOneChunk();
        }
        times.push(Date.now() - start);
    }
    times.sort(function (a, b) { return a - b; });
    return times[Math.floor(times.length / 2)];
}

function runParallelWithWorkers(callback) {
    var path = require('path');
    var workerPath = path.join(process.cwd(), 'standard', 'parallel_cpu_worker.js');
    var times = [];
    var round = 0;
    var totalRounds = 5;

    function runRound() {
        if (round >= totalRounds) {
            times.sort(function (a, b) { return a - b; });
            var median = times[Math.floor(times.length / 2)];
            callback(median);
            return;
        }
        var start = Date.now();
        var completed = 0;
        var workers = [];
        for (var w = 0; w < NUM_TASKS; w++) {
            var worker = new workerThreads.Worker(workerPath, { workerData: { WORK_PER_TASK: WORK_PER_TASK } });
            workers.push(worker);
            worker.on('message', function () {
                completed++;
                if (completed === NUM_TASKS) {
                    times.push(Date.now() - start);
                    for (var i = 0; i < workers.length; i++) {
                        workers[i].terminate();
                    }
                    round++;
                    runRound();
                }
            });
        }
    }

    runRound();
}

var workerThreads = typeof worker_threads !== 'undefined' ? worker_threads : (typeof require !== 'undefined' ? require('worker_threads') : null);
// Workers disabled for both runtimes until worker message delivery is fully verified (protoJS) and benchmark flow is confirmed (Node).
var useWorkers = false; // workerThreads && workerThreads.Worker && (typeof process !== 'undefined' && process.versions && process.versions.node);
if (useWorkers) {
    runParallelWithWorkers(function (median) {
        var result = { name: 'parallel_cpu', time_ms: median, iterations: 5, tasks: NUM_TASKS, parallel: true };
        console.log('__BENCH_RESULT__' + JSON.stringify(result));
    });
} else {
    var median = runSequential();
    var result = { name: 'parallel_cpu', time_ms: median, iterations: 5, tasks: NUM_TASKS, parallel: false };
    console.log('__BENCH_RESULT__' + JSON.stringify(result));
}
