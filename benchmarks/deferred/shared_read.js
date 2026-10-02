// Shared-data benchmark, protoJS: a large object graph built once on the main
// thread, then N tasks that each compute an aggregate over ALL of it.
//
//   N=0  sequential: the N aggregates computed one after another, on the main
//        thread (no Deferred)
//   N>0  N Deferreds; each reads the main thread's array of objects directly
//        -- no copy, no serialisation: a Deferred runs in the same protoCore
//        space and closes over `data`.
//
// Task t computes sum over all entries e of (e.v * (t + 1) + e.w), which is
// (t + 1) * SV + SW for SV = sum of v and SW = sum of w; the runner rejects a
// run whose aggregates are wrong. Build time and task time are reported
// separately; `ms` is the task phase (dispatch to last result).
// Environment: N (default 4), SIZE (default 1000000).
const N = Number(process.env.N || 4) | 0;
const SIZE = Number(process.env.SIZE || 1000000) | 0;

const tb = Date.now();
const data = [];
let SV = 0, SW = 0;
for (let i = 0; i < SIZE; i++) {
    const v = i % 97, w = (i * 7) % 13;
    data.push({ id: i, v, w });
    SV += v; SW += w;
}
const buildMs = Date.now() - tb;

function aggregate(t) {
    const k = t + 1;
    let s = 0;
    for (let i = 0; i < data.length; i++) {
        const e = data[i];
        s += e.v * k + e.w;
    }
    return s;
}

function report(sums, ms) {
    const tasks = N === 0 ? 4 : N;
    let ok = sums.length === tasks;
    for (let t = 0; ok && t < tasks; t++) ok = sums[t] === (t + 1) * SV + SW;
    console.log(JSON.stringify({ bench: 'shared_read', runtime: 'protojs', variant: N === 0 ? 'sequential' : 'deferred',
        n: N, size: SIZE, ok, build_ms: buildMs, ms, checksum: sums.reduce((a, b) => a + b, 0) }));
}

const t0 = Date.now();
if (N === 0) {
    // Same total work as N=4 tasks, on one thread.
    const sums = [];
    for (let t = 0; t < 4; t++) sums.push(aggregate(t));
    report(sums, Date.now() - t0);
} else {
    const tasks = [];
    for (let t = 0; t < N; t++) tasks.push(new Deferred(() => aggregate(t)));
    Promise.all(tasks).then((sums) => report(sums, Date.now() - t0));
}
