// Standard benchmark: incremental concat of large chunks.
//
// Different from string_concat.js (the existing one), which appends
// ONE character at a time.  Here each chunk is ~200 characters and we
// accumulate CHUNKS of them, ending with a (CHUNKS × 200)-char string.
//
// In a flat-buffer engine every concat must allocate a buffer of the
// new total size and memcpy the existing content into it.  Cumulative
// memcpy work per sequence: sum from 200 to (CHUNKS × 200) ≈
// O(CHUNKS²) bytes.
//
// In a rope-based engine every concat is a single new internal node
// pointing at the prior root and the new chunk; balancing requires
// O(log N) work down the right spine.  Total per sequence: O(CHUNKS
// × log CHUNKS) cells.
//
// IMPORTANT note on sizing: protoJS's current strConcat
// implementation pays a large constant per concat (~600 µs for the
// rope-rebalance recursion path).  Numbers below are kept small so
// the bench completes in seconds on protoJS.  The cumulative work
// nevertheless stays in the regime where the structural-sharing
// advantage SHOULD show up over flat-buffer copy; the fact that
// protoJS still loses at these sizes is itself the result.
//
// Outputs __BENCH_RESULT__<json> on the last line for runner to parse.

const ITERATIONS = 5;
const OUTER      = 1;
const CHUNKS     = 200;
const CHUNK_SIZE = 200;

let chunkSeed = 'x';
while (chunkSeed.length < CHUNK_SIZE) {
    chunkSeed = chunkSeed + chunkSeed;
}
const CHUNK = chunkSeed.slice(0, CHUNK_SIZE);

let SINK = 0;

function runOne() {
    for (let o = 0; o < OUTER; o++) {
        let s = '';
        for (let i = 0; i < CHUNKS; i++) {
            s = s + CHUNK;
        }
        SINK = SINK ^ s.charCodeAt(0);
    }
    return SINK;
}

const times = [];
for (let k = 0; k < ITERATIONS; k++) {
    const start = Date.now();
    runOne();
    times.push(Date.now() - start);
}
times.sort(function (a, b) { return a - b; });
const median = times[Math.floor(ITERATIONS / 2)];
const result = {
    name: 'string_concat_large_chunks',
    time_ms: median,
    iterations: ITERATIONS,
    outer: OUTER,
    chunks: CHUNKS,
    chunk_size: CHUNK_SIZE,
};
console.log('__BENCH_RESULT__' + JSON.stringify(result));
