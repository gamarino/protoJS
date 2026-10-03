// Node.js side of the structure benchmarks (see README.md).
//
// The shared data is built once on the main thread. MODE=seq runs tasks
// 0..N-1 one after another on the main thread; MODE=par uses N
// worker_threads in Node's idiomatic way: the data is posted to each worker
// (a structured clone per worker) with the task number, and each worker
// posts its result back. Workers are started before the timed phase.
// Reported per repetition: end-to-end time, the time the main thread spent in
// postMessage (serialization), the longest send-to-receive delay seen by a
// worker (serialization + deserialization + queueing) and the longest compute.
// Environment: WORKLOAD, MODE, N, REPS, SCALE.
const { Worker } = require("worker_threads");
const { performance } = require("perf_hooks");

const WORKLOAD = process.env.WORKLOAD || "records";
const MODE = process.env.MODE || "seq";
const N = Number(process.env.N || 1);
const REPS = Number(process.env.REPS || 3);
const SCALE = Number(process.env.SCALE || 1);

const wl = require(__dirname + "/workloads/" + WORKLOAD + ".js");

function median(xs) {
    const s = xs.slice().sort((a, b) => a - b);
    return s[(s.length - 1) >> 1];
}

function startWorkers(n) {
    return Promise.all(Array.from({ length: n }, () => new Promise((resolve, reject) => {
        const w = new Worker(__dirname + "/node_worker.js", { workerData: { workload: WORKLOAD } });
        w.once("message", (m) => (m.ready ? resolve(w) : reject(new Error("bad hello"))));
        w.once("error", reject);
    })));
}

function runParallel(workers, data) {
    return new Promise((resolve, reject) => {
        const results = new Array(workers.length);
        const sentAt = new Array(workers.length);
        let pending = workers.length, cloneMax = 0, computeMax = 0;
        const t0 = performance.now();
        workers.forEach((w, k) => {
            w.once("message", (m) => {
                results[k] = m.result;
                cloneMax = Math.max(cloneMax, m.recvAt - sentAt[k]);
                computeMax = Math.max(computeMax, m.computeMs);
                if (--pending === 0)
                    resolve({ results, ms: performance.now() - t0, postMs, cloneMax, computeMax });
            });
            w.once("error", reject);
        });
        const p0 = performance.now();
        workers.forEach((w, k) => {
            sentAt[k] = performance.timeOrigin + performance.now();
            w.postMessage({ data, k });
        });
        const postMs = performance.now() - p0;
    });
}

async function main() {
    const tb = performance.now();
    const data = wl.build(SCALE);
    const buildMs = performance.now() - tb;

    let workers = null, startMs = 0;
    if (MODE === "par") {
        const ts = performance.now();
        workers = await startWorkers(N);
        startMs = performance.now() - ts;
    }

    const times = [], posts = [], clones = [], computes = [];
    let checksums = null;
    let ok = true;
    for (let rep = 0; rep < REPS; rep++) {
        let results;
        if (MODE === "seq") {
            const t0 = performance.now();
            results = [];
            for (let k = 0; k < N; k++) results.push(wl.task(data, k));
            times.push(performance.now() - t0);
        } else {
            const r = await runParallel(workers, data);
            results = r.results;
            times.push(r.ms); posts.push(r.postMs); clones.push(r.cloneMax); computes.push(r.computeMax);
        }
        const cs = results.map((r) => r.checksum);
        ok = ok && results.length === N && results.every((r, i) => r.k === i && typeof r.checksum === "number");
        if (checksums === null) checksums = cs;
        else ok = ok && cs.join() === checksums.join();
    }
    if (workers) await Promise.all(workers.map((w) => w.terminate()));
    const r = (x) => Math.round(x);
    console.log(JSON.stringify({
        bench: "structures", runtime: "node", workload: WORKLOAD, mode: MODE, n: N, scale: SCALE, ok,
        build_ms: r(buildMs), times_ms: times.map(r), median_ms: r(median(times)), checksums,
        worker_start_ms: r(startMs),
        post_ms: posts.length ? r(median(posts)) : null,
        clone_ms: clones.length ? r(median(clones)) : null,
        compute_ms: computes.length ? r(median(computes)) : null,
        peak_rss_kb: process.resourceUsage().maxRSS,
    }));
}

main().catch((e) => { console.log("FAILED: " + (e && e.stack || e)); process.exit(1); });
