// Heavy parallel CPU benchmark: designed to expose protoJS multithreading advantage.
//
// - protoJS: uses Deferred (CPU thread pool) to run 4 tasks in parallel; wall time over 5 runs, median.
// - Node: same total CPU work run sequentially in main thread; median of 5 runs.
//
// Same total CPU work per run (4 * WORK_PER_TASK iterations). Runner uses __BENCH_RESULT__.time_ms for comparison.

var NUM_TASKS = 4;
var WORK_PER_TASK = 2e6;

// Use literal 2e6 so the function is self-contained when executed in worker (no closure refs).
function runOneChunk() {
    var sum = 0;
    for (var i = 0; i < 2e6; i++) {
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

function runParallelWithDeferred(callback) {
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

        function onDone() {
            completed++;
            if (completed === NUM_TASKS) {
                times.push(Date.now() - start);
                round++;
                runRound();
            }
        }

        for (var i = 0; i < NUM_TASKS; i++) {
            var d = new Deferred(runOneChunk);
            d.then(onDone);
            d.catch(function (err) {
                onDone();
            });
        }
    }

    runRound();
}

if (typeof Deferred !== 'undefined') {
    runParallelWithDeferred(function (median) {
        var result = { name: 'parallel_cpu', time_ms: median, iterations: 5, tasks: NUM_TASKS, parallel: true };
        console.log('__BENCH_RESULT__' + JSON.stringify(result));
    });
} else {
    var median = runSequential();
    var result = { name: 'parallel_cpu', time_ms: median, iterations: 5, tasks: NUM_TASKS, parallel: false };
    console.log('__BENCH_RESULT__' + JSON.stringify(result));
}
