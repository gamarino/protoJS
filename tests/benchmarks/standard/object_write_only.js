// Isolated benchmark: Object property WRITE only.
// Pre-creates the keys to isolate property setting from string allocation.

const ITERATIONS = 5;
const OPS = 1000000;
const KEY_COUNT = 100;

const keys = [];
for (let i = 0; i < KEY_COUNT; i++) {
    keys.push('k' + i);
}

function runOne() {
    const obj = {};
    for (let i = 0; i < OPS; i++) {
        obj[keys[i % KEY_COUNT]] = i;
    }
    return obj;
}

const times = [];
for (let k = 0; k < ITERATIONS; k++) {
    const start = Date.now();
    runOne();
    times.push(Date.now() - start);
}
times.sort(function (a, b) { return a - b; });
const median = times[Math.floor(ITERATIONS / 2)];
const result = { name: 'object_write_only', time_ms: median, iterations: ITERATIONS, ops: OPS };
console.log('__BENCH_RESULT__' + JSON.stringify(result));
