// Standard benchmark: string concatenation in a loop.
// Self-contained; no template literals required.

const ITERATIONS = 5;
const OPS = 50000;

function runOne() {
    let s = '';
    for (let i = 0; i < OPS; i++) {
        s += 'x';
    }
    return s.length;
}

const times = [];
for (let k = 0; k < ITERATIONS; k++) {
    const start = Date.now();
    runOne();
    times.push(Date.now() - start);
}
times.sort(function (a, b) { return a - b; });
const median = times[Math.floor(ITERATIONS / 2)];
const result = { name: 'string_concat', time_ms: median, iterations: ITERATIONS, ops: OPS };
console.log('__BENCH_RESULT__' + JSON.stringify(result));
