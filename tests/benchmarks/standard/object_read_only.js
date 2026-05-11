// Isolated benchmark: Object property READ only.
// Pre-populates the object and pre-creates the keys to isolate access from string allocation.

const ITERATIONS = 5;
const OPS = 100000;
const KEY_COUNT = 100;

const keys = [];
for (let i = 0; i < KEY_COUNT; i++) {
    keys.push('k' + i);
}

const obj = {};
for (let i = 0; i < KEY_COUNT; i++) {
    obj[keys[i]] = i;
}

function runOne() {
    let sum = 0;
    for (let i = 0; i < OPS; i++) {
        const _v = obj[keys[i % KEY_COUNT]];
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
const result = { name: 'object_read_only', time_ms: median, iterations: ITERATIONS, ops: OPS };
console.log('__BENCH_RESULT__' + JSON.stringify(result));
