// Standard benchmark: function call overhead (no-op function called many times).
// Self-contained.

const ITERATIONS = 5;
const CALLS = 2e6;

function noop() {}

function runOne() {
    for (let i = 0; i < CALLS; i++) {
        noop();
    }
}

const times = [];
for (let k = 0; k < ITERATIONS; k++) {
    const start = Date.now();
    runOne();
    times.push(Date.now() - start);
}
times.sort(function (a, b) { return a - b; });
const median = times[Math.floor(ITERATIONS / 2)];
const result = { name: 'function_calls', time_ms: median, iterations: ITERATIONS, calls: CALLS };
console.log('__BENCH_RESULT__' + JSON.stringify(result));
