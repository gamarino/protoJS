// Standard benchmark: array creation via loop and push (no Array.from).
// Self-contained; compatible with minimal engines.

const ITERATIONS = 5;
const SIZE = 100000;

function runOne() {
    const arr = [];
    for (let i = 0; i < SIZE; i++) {
        arr.push(i);
    }
    return arr.length;
}

const times = [];
for (let k = 0; k < ITERATIONS; k++) {
    const start = Date.now();
    runOne();
    times.push(Date.now() - start);
}
times.sort(function (a, b) { return a - b; });
const median = times[Math.floor(ITERATIONS / 2)];
const result = { name: 'array_literal', time_ms: median, iterations: ITERATIONS, size: SIZE };
console.log('__BENCH_RESULT__' + JSON.stringify(result));
