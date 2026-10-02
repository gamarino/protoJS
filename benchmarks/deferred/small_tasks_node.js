// Task-overhead benchmark, Node.js: the same M small tasks as small_tasks.js.
//
//   VARIANT=pool     a pool of P worker_threads (default 12, the machine's
//                    hardware threads, as protoJS's Deferred pool); each task
//                    is one postMessage to a worker and one message back --
//                    the idiomatic way to run small tasks off the main thread
//   VARIANT=promise  M promises resolved on the main thread
//   VARIANT=sync     M plain calls
//
// The pool's start-up is included in `ms`, as the protoJS Deferred pool starts
// on the first Deferred. Environment: M, K, P, VARIANT.
const { Worker, isMainThread, parentPort, workerData } = require('worker_threads');
function work(i, K) {
    let s = 0;
    for (let j = 0; j < K; j++) s += j;
    return s + i;
}
if (!isMainThread) {
    parentPort.on('message', (i) => parentPort.postMessage(work(i, workerData.K)));
} else {
    const M = Number(process.env.M || 2000) | 0;
    const K = Number(process.env.K || 200) | 0;
    const P = Number(process.env.P || 12) | 0;
    const VARIANT = process.env.VARIANT || 'pool';
    const expected = M * (K * (K - 1) / 2) + (M * (M - 1)) / 2;
    const report = (total, ms) => console.log(JSON.stringify({ bench: 'small_tasks', runtime: 'node',
        variant: VARIANT, m: M, k: K, p: VARIANT === 'pool' ? P : undefined, ok: total === expected, ms,
        us_per_task: Math.round(ms * 1000 / M) }));
    const t0 = Date.now();
    if (VARIANT === 'sync') {
        let total = 0;
        for (let i = 0; i < M; i++) total += work(i, K);
        report(total, Date.now() - t0);
    } else if (VARIANT === 'promise') {
        const ps = [];
        for (let i = 0; i < M; i++) ps.push(new Promise((r) => r(work(i, K))));
        Promise.all(ps).then((rs) => report(rs.reduce((a, b) => a + b, 0), Date.now() - t0));
    } else {
        const workers = [];
        for (let p = 0; p < P; p++) workers.push(new Worker(__filename, { workerData: { K } }));
        let next = 0, done = 0, total = 0;
        const feed = (w) => { if (next < M) w.postMessage(next++); };
        for (const w of workers) {
            w.on('message', (r) => {
                total += r;
                if (++done === M) { report(total, Date.now() - t0); for (const x of workers) x.terminate(); }
                else feed(w);
            });
            // Keep a few tasks in flight per worker, as a queue would.
            for (let q = 0; q < 4; q++) feed(w);
        }
    }
}
