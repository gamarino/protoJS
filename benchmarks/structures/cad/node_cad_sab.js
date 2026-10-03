// Node.js CAD variant with the model encoded in SharedArrayBuffers
// (soa.js): workers share the columns without copying. For each N in NS the
// a fresh set of N workers runs tasks 0..N-1, each worker one task.
// Reports the object-model build time, the encoding time, and per-N times.
// Environment: PARTS, NS, REPS.
const { Worker } = require("worker_threads");
const { performance } = require("perf_hooks");
const m = require(__dirname + "/model.js");
const soa = require(__dirname + "/soa.js");
const PARTS = Number(process.env.PARTS || 10000);
const NS = (process.env.NS || "1,2,4,6,12").split(",").map(Number);
const REPS = Number(process.env.REPS || 3);
function median(xs) { const s = xs.slice().sort((a, b) => a - b); return s[(s.length - 1) >> 1]; }
const r0 = (x) => Math.round(x);

async function main() {
    const t0 = performance.now();
    let model = m.build(PARTS);
    const buildMs = performance.now() - t0;
    const nmax = Math.max(...NS);
    const ref = [];
    for (let k = 0; k < nmax; k++) ref.push(m.task(model, k).checksum);
    const t1 = performance.now();
    const s = soa.encode(model);
    const encodeMs = performance.now() - t1;
    const counts = model.counts;
    model = null;  // workers and tasks use the columns only
    if (global.gc) global.gc();
    const seq = [], seqTimes = [];
    for (let k = 0; k < nmax; k++) {
        const ts = performance.now();
        seq.push(soa.task(s, k).checksum);
        seqTimes.push(Math.round(performance.now() - ts));
    }
    let ok = seq.join() === ref.join();
    const results = [];
    for (const n of NS) {
        const workers = await Promise.all(Array.from({ length: n }, () => new Promise((resolve, reject) => {
            const w = new Worker(__dirname + "/node_cad_sab_worker.js");
            w.once("message", () => resolve(w));
            w.once("error", reject);
        })));
        const times = [];
        for (let rep = 0; rep < REPS; rep++) {
            const t2 = performance.now();
            const res = await Promise.all(workers.map((w, k) => new Promise((resolve, reject) => {
                w.once("message", resolve);
                w.once("error", reject);
                w.postMessage({ s, k });
            })));
            times.push(performance.now() - t2);
            for (let k = 0; k < n; k++) ok = ok && res[k].checksum === ref[k];
        }
        await Promise.all(workers.map((w) => w.terminate()));
        results.push({ n, times_ms: times.map(r0), median_ms: r0(median(times)) });
    }
    console.log(JSON.stringify({ bench: "cad", runtime: "node", variant: "sab", parts: PARTS, ok, counts,
        build_ms: r0(buildMs), encode_ms: r0(encodeMs), seq_task_ms: seqTimes, checksums: seq, results,
        peak_rss_kb: process.resourceUsage().maxRSS }));
}
main().catch((e) => { console.log("FAILED: " + (e && e.stack || e)); process.exit(1); });
