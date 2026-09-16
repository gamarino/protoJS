// Heavy parallel CPU benchmark: designed to expose protoJS multithreading advantage.
//
// - protoJS: 4 tasks in parallel (protoCore.runInThread or Deferred); wall time over 5 runs, median.
// - Node: same total CPU work run sequentially in main thread; median of 5 runs.
//
// Workload: LCG (linear congruential) loop — data-dependent, no closed-form, not trivially
// optimizable by JIT. Same total work per run (4 * WORK_PER_TASK iterations).
// Runner uses __BENCH_RESULT__.time_ms for comparison.

var NUM_TASKS = 4;
// One workload for every runtime, so that the arms of the comparison measure
// the same amount of work.  This used to be 2e5 under protojs and 2e6
// elsewhere, which made the reported ratio meaningless.  The reduction was
// introduced when a runner timeout was a concern; a whole protojs run at 2e6
// now completes in well under a second, far below the runner's 120 s timeout.
var WORK_PER_TASK = 2e6;

// LCG workload: state = (state * A + C) mod 2^32, sum += state. Not reducible by JIT.
// Use literal so Deferred worker has no closure refs.
function runOneChunk() {
    var state = 1;
    var sum = 0;
    var i = 0;
    var n = WORK_PER_TASK;
    while (i < n) {
        state = ((state * 1103515245 + 12345) >>> 0);
        sum += state;
        i++;
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

// protoCore native multithreading: no serialization; same ProtoSpace, result in shared memory.
function runParallelWithProtoCore(callback) {
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

        // Stagger ProtoThread creation with setImmediate so the main thread yields between newThread
        // calls, avoiding lock contention when creating several threads in quick succession.
        function scheduleNext(i) {
            if (i >= NUM_TASKS) return;
            var d = protoCore.runInThread('cpuChunk', [WORK_PER_TASK]);
            d.then(onDone);
            d.catch(function (err) {
                onDone();
            });
            if (i + 1 < NUM_TASKS && typeof setImmediate === 'function') {
                setImmediate(function () { scheduleNext(i + 1); });
            } else if (i + 1 < NUM_TASKS && typeof setTimeout === 'function') {
                setTimeout(function () { scheduleNext(i + 1); }, 0);
            }
        }
        scheduleNext(0);
    }

    runRound();
}

// Every run states the work it performed, so that a change of workload — or a
// run that silently did far less work than intended — cannot look like a
// result.  `executor` names what actually ran the tasks, which is NOT the same
// code in every runtime: under protojs the loop runs inside the native C++
// `cpuChunk` worker, while node and QuickJS run the JavaScript loop above.
function report(median, parallel, executor) {
    var result = {
        name: 'parallel_cpu',
        time_ms: median,
        iterations: 5,
        tasks: NUM_TASKS,
        parallel: parallel,
        work_per_task: WORK_PER_TASK,
        total_work: NUM_TASKS * WORK_PER_TASK,
        executor: executor
    };
    console.log('parallel_cpu: ' + NUM_TASKS + ' tasks x ' + WORK_PER_TASK +
                ' LCG steps = ' + (NUM_TASKS * WORK_PER_TASK) +
                ' steps per round, 5 rounds, median ' + median + ' ms' +
                ', parallel=' + parallel + ', executor=' + executor);
    console.log('__BENCH_RESULT__' + JSON.stringify(result));
}

if (typeof protoCore !== 'undefined' && typeof protoCore.runInThread === 'function') {
    runParallelWithProtoCore(function (median) {
        report(median, true, 'protoCore.runInThread (native cpuChunk worker, ' + NUM_TASKS + ' protoCore threads)');
    });
} else if (typeof Deferred !== 'undefined') {
    runParallelWithDeferred(function (median) {
        report(median, true, 'Deferred (JavaScript loop on the event loop)');
    });
} else {
    report(runSequential(), false, 'sequential JavaScript loop');
}
