// Standard benchmark: insertion into the middle of a large string.
//
// JavaScript doesn't have a primitive "insert into middle" — we
// express it as `s.slice(0, mid) + chunk + s.slice(mid)`.  Both
// engines see the same source code; the cost is structural.
//
// In a flat-buffer engine every insertion materialises a new buffer
// of size (s.length + chunk.length) and memcpy's both halves of the
// original plus the inserted chunk.
//
// In a rope-based engine every insertion splits the rope at the
// insert position (O(log N)), allocates O(log N) new internal nodes,
// and concatenates the three pieces.  No byte copying.
//
// Note: protoJS does not currently implement String.prototype.length,
// so we cannot use `s.length / 2` to find the midpoint.  We track the
// current length explicitly via an integer counter.
//
// Sizing kept small to bound protoJS wall time.
//
// Outputs __BENCH_RESULT__<json> on the last line for runner to parse.

const ITERATIONS  = 5;
const BASE_LEN    = 1000;
const INSERTS     = 100;
const CHUNK_LEN   = 50;

function buildBlock(targetLen) {
    let b = 'x';
    let len = 1;
    while (len < targetLen) {
        b = b + b;
        len = len * 2;
    }
    return b.slice(0, targetLen);
}
const BASE  = buildBlock(BASE_LEN);
const CHUNK = buildBlock(CHUNK_LEN);

let SINK = 0;

function runOne() {
    let s = BASE;
    let curLen = BASE_LEN;
    for (let i = 0; i < INSERTS; i++) {
        const mid = (curLen / 2) | 0;
        s = s.slice(0, mid) + CHUNK + s.slice(mid);
        curLen = curLen + CHUNK_LEN;
    }
    SINK = SINK ^ s.charCodeAt(0);
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
    name: 'string_insert_middle',
    time_ms: median,
    iterations: ITERATIONS,
    base_len: BASE_LEN,
    inserts: INSERTS,
    chunk_len: CHUNK_LEN,
};
console.log('__BENCH_RESULT__' + JSON.stringify(result));
