// Standard benchmark: maintain immutable history of a growing list.
//
// On every step we extend the list by one element via .concat() (which
// returns a NEW array, by spec) and push the new array into a history
// list.  At the end every intermediate version is still live.
//
// For a flat-array engine, each .concat() materialises a fresh array
// of size (current+1) and copies every prior element.  Total entries
// kept across history is 1 + 2 + 3 + ... + N = N(N+1)/2 — quadratic.
//
// For a structural-sharing AVL-backed engine, each .concat() either:
//   (a) shares the prior root and adds an O(log N) spine of new nodes
//       (true structural concat), or
//   (b) builds a flat copy (worst case, matches the mutable engine).
//
// Sizing kept small to bound protoJS wall time (its current list
// concat path has a large per-call constant on append).
//
// Outputs __BENCH_RESULT__<json> on the last line for runner to parse.

const ITERATIONS = 5;
const N          = 200;

function runOne() {
    const history = [];
    let arr = [];
    for (let i = 0; i < N; i++) {
        arr = arr.concat([i]);
        history.push(arr);
    }
    // Force every snapshot to remain accessible
    let sum = 0;
    for (let j = 0; j < history.length; j++) {
        sum = sum + history[j].length;
    }
    return sum;
}

const times = [];
let lastSum = 0;
for (let k = 0; k < ITERATIONS; k++) {
    const start = Date.now();
    lastSum = runOne();
    times.push(Date.now() - start);
}
times.sort(function (a, b) { return a - b; });
const median = times[Math.floor(ITERATIONS / 2)];
const result = {
    name: 'list_snapshot_history',
    time_ms: median,
    iterations: ITERATIONS,
    snapshots: N,
    checksum: lastSum,
};
console.log('__BENCH_RESULT__' + JSON.stringify(result));
