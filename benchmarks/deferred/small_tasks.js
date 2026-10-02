// Task-overhead benchmark, protoJS: M small tasks (each sums K integers).
//
//   VARIANT=deferred  M Deferreds started at once, awaited with Promise.all
//   VARIANT=promise   M promises resolved on the main thread (no parallelism:
//                     the cost of the promise machinery alone)
//   VARIANT=sync      M plain calls (the work alone)
//
// Self-reporting: the sum of all task results must equal M*K*(K-1)/2 + the sum
// of the task indices. `us_per_task` = ms * 1000 / M.
// Environment: M (default 2000), K (default 200), VARIANT (default deferred).
const M = Number(process.env.M || 2000) | 0;
const K = Number(process.env.K || 200) | 0;
const VARIANT = process.env.VARIANT || 'deferred';

function work(i) {
    let s = 0;
    for (let j = 0; j < K; j++) s += j;
    return s + i;
}
const expected = M * (K * (K - 1) / 2) + (M * (M - 1)) / 2;

function report(total, ms) {
    console.log(JSON.stringify({ bench: 'small_tasks', runtime: 'protojs', variant: VARIANT, m: M, k: K,
        ok: total === expected, ms, us_per_task: Math.round(ms * 1000 / M) }));
}

const t0 = Date.now();
if (VARIANT === 'sync') {
    let total = 0;
    for (let i = 0; i < M; i++) total += work(i);
    report(total, Date.now() - t0);
} else {
    const tasks = [];
    for (let i = 0; i < M; i++) {
        tasks.push(VARIANT === 'promise' ? new Promise((resolve) => resolve(work(i)))
                                         : new Deferred(() => work(i)));
    }
    Promise.all(tasks).then((rs) => {
        let total = 0;
        for (const r of rs) total += r;
        report(total, Date.now() - t0);
    });
}
