// Shared-data benchmark, Node.js: the same object graph and aggregates as
// shared_read.js, in the two idiomatic ways to give the data to workers.
//
//   VARIANT=clone  postMessage(data) to each worker: the structured clone
//                  serialises the whole array of objects once per worker, and
//                  each worker deserialises its own copy.
//   VARIANT=sab    encode v and w into an Int32Array on a SharedArrayBuffer
//                  (possible here because the fields are small integers) and
//                  hand the buffer to every worker; no per-worker copy.
//   N=0            sequential on the main thread.
//
// `ms` is from the start of the hand-over (encoding included for sab, cloning
// for clone) to the last result, as in the protoJS version; worker start-up is
// included. Environment: N, SIZE, VARIANT.
const { Worker, isMainThread, parentPort, workerData } = require('worker_threads');

function aggregateObjects(data, t) {
    const k = t + 1;
    let s = 0;
    for (let i = 0; i < data.length; i++) { const e = data[i]; s += e.v * k + e.w; }
    return s;
}
function aggregateShared(ints, size, t) {
    const k = t + 1;
    let s = 0;
    for (let i = 0; i < size; i++) s += ints[2 * i] * k + ints[2 * i + 1];
    return s;
}

if (!isMainThread) {
    parentPort.once('message', (msg) => {
        if (msg.sab) parentPort.postMessage(aggregateShared(new Int32Array(msg.sab), msg.size, workerData.t));
        else parentPort.postMessage(aggregateObjects(msg.data, workerData.t));
    });
} else {
    const N = Number(process.env.N || 4) | 0;
    const SIZE = Number(process.env.SIZE || 1000000) | 0;
    const VARIANT = process.env.VARIANT || 'clone';
    const tb = Date.now();
    const data = [];
    let SV = 0, SW = 0;
    for (let i = 0; i < SIZE; i++) {
        const v = i % 97, w = (i * 7) % 13;
        data.push({ id: i, v, w });
        SV += v; SW += w;
    }
    const buildMs = Date.now() - tb;
    const report = (variant, sums, ms) => {
        const tasks = N === 0 ? 4 : N;
        let ok = sums.length === tasks;
        for (let t = 0; ok && t < tasks; t++) ok = sums[t] === (t + 1) * SV + SW;
        console.log(JSON.stringify({ bench: 'shared_read', runtime: 'node', variant, n: N, size: SIZE, ok,
            build_ms: buildMs, ms, checksum: sums.reduce((a, b) => a + b, 0) }));
    };
    const t0 = Date.now();
    if (N === 0) {
        const sums = [];
        for (let t = 0; t < 4; t++) sums.push(aggregateObjects(data, t));
        report('sequential', sums, Date.now() - t0);
    } else {
        let msg;
        if (VARIANT === 'sab') {
            const sab = new SharedArrayBuffer(SIZE * 2 * 4);
            const ints = new Int32Array(sab);
            for (let i = 0; i < SIZE; i++) { ints[2 * i] = data[i].v; ints[2 * i + 1] = data[i].w; }
            msg = { sab, size: SIZE };
        } else {
            msg = { data };
        }
        const jobs = [];
        for (let t = 0; t < N; t++) {
            jobs.push(new Promise((resolve, reject) => {
                const w = new Worker(__filename, { workerData: { t } });
                w.once('message', (r) => { resolve(r); w.terminate(); });
                w.once('error', reject);
                w.postMessage(msg);
            }));
        }
        Promise.all(jobs).then((sums) => report(VARIANT, sums, Date.now() - t0));
    }
}
