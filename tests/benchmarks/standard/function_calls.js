// Standard benchmark: function call overhead (with state dependency to prevent dead code elimination).
// Self-contained.

const ITERATIONS = 5;
const CALLS = 2e5; // Adjusted to keep protoJS time reasonable

let state = 1;

function work(val) {
    // Simple computation that cannot be entirely optimized away as dead code
    return (val + 1) | 0;
}

function runOne() {
    for (let i = 0; i < CALLS; i++) {
        state = work(state);
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

// Prevent dead code elimination of the final state
if (state === 0) console.log("State zero");

const result = { name: 'function_calls', time_ms: median, iterations: ITERATIONS, calls: CALLS };
console.log('__BENCH_RESULT__' + JSON.stringify(result));
