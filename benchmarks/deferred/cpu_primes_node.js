// CPU-bound benchmark, Node.js: the same algorithm and split as cpu_primes.js,
// with one worker_threads Worker per task (N=0: sequential on the main
// thread). The time includes starting the workers, as the protoJS time
// includes dispatching the Deferreds (its pool starts on first use).
const { Worker, isMainThread, parentPort, workerData } = require('worker_threads');
const BLOCK = 1000;
const EXPECTED = { 100000: 9592, 300000: 25997, 1000000: 78498, 2000000: 148933 };

function isPrime(n) {
    if (n < 2) return false;
    for (let d = 2; d * d <= n; d++) if (n % d === 0) return false;
    return true;
}
function countTask(task, stride, limit) {
    let c = 0;
    for (let lo = task * BLOCK; lo < limit; lo += stride * BLOCK) {
        const hi = lo + BLOCK < limit ? lo + BLOCK : limit;
        for (let n = lo; n < hi; n++) if (isPrime(n)) c++;
    }
    return c;
}

if (!isMainThread) {
    parentPort.postMessage(countTask(workerData.task, workerData.stride, workerData.limit));
} else {
    const N = Number(process.env.N || 4) | 0;
    const LIMIT = Number(process.env.LIMIT || 300000) | 0;
    const report = (result, ms) => {
        const expected = EXPECTED[LIMIT];
        console.log(JSON.stringify({ bench: 'cpu_primes', runtime: 'node', n: N, limit: LIMIT,
            result, expected, ok: expected === undefined ? null : result === expected, ms }));
    };
    const t0 = Date.now();
    if (N === 0) {
        report(countTask(0, 1, LIMIT), Date.now() - t0);
    } else {
        const jobs = [];
        for (let t = 0; t < N; t++) {
            jobs.push(new Promise((resolve, reject) => {
                const w = new Worker(__filename, { workerData: { task: t, stride: N, limit: LIMIT } });
                w.once('message', resolve);
                w.once('error', reject);
            }));
        }
        Promise.all(jobs).then((counts) => report(counts.reduce((a, b) => a + b, 0), Date.now() - t0));
    }
}
