// Standard benchmark: repeated string doubling (s = s + s).
//
// Canonical case where flat-buffer string storage pays cumulative
// O(N) memcpy per step: at each doubling the engine must allocate a
// new buffer of double size and memcpy the entire current state into
// it.  Over D doublings starting from 1 char, total bytes copied is
// 1 + 2 + 4 + ... + 2^D ≈ 2 × 2^D, with the work distributed across
// memory bandwidth.
//
// For a structural-sharing rope every doubling is ONE internal node
// whose left and right both point at the previous root.  D
// allocations total, O(1) depth growth per step, no byte copying.
//
// We do OUTER independent doubling sequences inside `runOne()` to
// make even small-string work measurable on Node/V8 — a single
// 20-doubling run completes faster than the millisecond timer
// resolution.
//
// Caveat: protoJS does not currently expose String.prototype.length
// (returns undefined), so `final_length` is not reported.  The work
// itself is unaffected.
//
// Outputs __BENCH_RESULT__<json> on the last line for runner to parse.

const ITERATIONS = 5;
const OUTER      = 200;
const DOUBLINGS  = 18;   // per sequence, final ≈ 262 144 chars

// Sink that the engines cannot prove is dead — forces the doubling
// loop to actually execute on aggressively-optimising engines.
let SINK = 0;

function runOne() {
    for (let o = 0; o < OUTER; o++) {
        let s = 'x';
        for (let i = 0; i < DOUBLINGS; i++) {
            s = s + s;
        }
        // Touch the resulting string in a way the optimiser can't
        // eliminate.  charCodeAt(0) on a non-empty string returns a
        // well-defined integer; we XOR it into the sink.
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
    name: 'string_repeated_doubling',
    time_ms: median,
    iterations: ITERATIONS,
    outer: OUTER,
    doublings: DOUBLINGS,
};
console.log('__BENCH_RESULT__' + JSON.stringify(result));
