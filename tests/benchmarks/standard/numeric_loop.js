// Standard benchmark: integer loop and sum (CPU-bound, no allocations).
// Inspired by SunSpider/Kraken-style math. Self-contained; no dependencies.
// Outputs __BENCH_RESULT__<json> on the last line for runner to parse.

const ITERATIONS = 5;
const INNER = 1e6;

function runOne() {
    let sum = 0;
    for (let i = 0; i < INNER; i++) {
        sum += i;
    }
    return sum;
}

const times = [];
for (let k = 0; k < ITERATIONS; k++) {
    const start = Date.now();
    runOne();
    times.push(Date.now() - start);
}
times.sort(function (a, b) { return a - b; });
const median = times[Math.floor(ITERATIONS / 2)];
const result = { name: 'numeric_loop', time_ms: median, iterations: ITERATIONS, inner: INNER };
console.log('__BENCH_RESULT__' + JSON.stringify(result));
