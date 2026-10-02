// CPU-bound benchmark, protoJS: count the primes below LIMIT by trial
// division, split into N tasks.
//
//   N=0  sequential, on the main thread (no Deferred)
//   N>0  N Deferreds, run on the Deferred pool
//
// The range is cut into blocks of BLOCK numbers dealt round-robin to the
// tasks, so every task gets the same mix of small (cheap) and large
// (expensive) numbers. Integer arithmetic is kept integral with `| 0`:
// protoJS stores a non-integral-typed number (Math.ceil, Number(string)) as a
// double, and its double arithmetic is about 3x slower than its integer path.
//
// Self-reporting: prints one JSON line with the computed count, the expected
// count and the wall time; the runner rejects a run whose result is wrong.
// Environment: N (default 4), LIMIT (default 300000).
const N = Number(process.env.N || 4) | 0;
const LIMIT = Number(process.env.LIMIT || 300000) | 0;
const BLOCK = 1000;
const EXPECTED = { 100000: 9592, 300000: 25997, 1000000: 78498, 2000000: 148933 };

function isPrime(n) {
    if (n < 2) return false;
    for (let d = 2; d * d <= n; d++) if (n % d === 0) return false;
    return true;
}

// Count primes in the blocks b, b + stride, b + 2*stride, ...
function countTask(task, stride, limit) {
    let c = 0;
    for (let lo = task * BLOCK; lo < limit; lo += stride * BLOCK) {
        const hi = lo + BLOCK < limit ? lo + BLOCK : limit;
        for (let n = lo; n < hi; n++) if (isPrime(n)) c++;
    }
    return c;
}

function report(result, ms) {
    const expected = EXPECTED[LIMIT];
    console.log(JSON.stringify({ bench: 'cpu_primes', runtime: 'protojs', n: N, limit: LIMIT,
        result, expected, ok: expected === undefined ? null : result === expected, ms }));
}

const t0 = Date.now();
if (N === 0) {
    report(countTask(0, 1, LIMIT), Date.now() - t0);
} else {
    const tasks = [];
    for (let t = 0; t < N; t++) tasks.push(new Deferred(() => countTask(t, N, LIMIT)));
    Promise.all(tasks).then((counts) => {
        let total = 0;
        for (const c of counts) total += c;
        report(total, Date.now() - t0);
    });
}
