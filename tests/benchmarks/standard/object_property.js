// Standard benchmark: object property read/write in a loop.
// Self-contained; no dependencies.

const ITERATIONS = 5;
const OPS = 200000;

function runOne() {
    const obj = {};
    for (let i = 0; i < OPS; i++) {
        obj['k' + (i % 100)] = i;
    }
    let sum = 0;
    for (let i = 0; i < OPS; i++) {
        sum += obj['k' + (i % 100)];
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
const result = { name: 'object_property', time_ms: median, iterations: ITERATIONS, ops: OPS };
console.log('__BENCH_RESULT__' + JSON.stringify(result));
