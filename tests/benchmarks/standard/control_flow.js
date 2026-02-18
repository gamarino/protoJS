// Standard benchmark: control flow (conditionals and loops).
// Self-contained; no allocations in hot path.

const ITERATIONS = 5;
const INNER = 1e6;

function runOne() {
    let a = 0;
    let b = 0;
    for (let i = 0; i < INNER; i++) {
        if (i % 2 === 0) {
            a += i;
        } else {
            b += i;
        }
    }
    return a + b;
}

const times = [];
for (let k = 0; k < ITERATIONS; k++) {
    const start = Date.now();
    runOne();
    times.push(Date.now() - start);
}
times.sort(function (a, b) { return a - b; });
const median = times[Math.floor(ITERATIONS / 2)];
const result = { name: 'control_flow', time_ms: median, iterations: ITERATIONS, inner: INNER };
console.log('__BENCH_RESULT__' + JSON.stringify(result));
