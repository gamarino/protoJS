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

// The benchmark reports the value it computed and checks it, so a crash or an
// early stop cannot pass for a fast run: state starts at 1 and every call adds 1.
const expectedState = 1 + ITERATIONS * CALLS;
if (state !== expectedState) {
    throw new Error('function_calls: state = ' + state + ', expected ' + expectedState);
}

const result = { name: 'function_calls', time_ms: median, iterations: ITERATIONS, calls: CALLS, state: state };
console.log('__BENCH_RESULT__' + JSON.stringify(result));
