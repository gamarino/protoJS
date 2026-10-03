// Node.js side of the CAD benchmark, object model (model.js).
//   VARIANT=main     build the model and run the tasks on the main thread
//                    (sequential); run with the default heap or with
//                    --max-old-space-size to see whether the model fits.
//   VARIANT=workers  for N = NS[0] (one N per process): N worker_threads,
//                    each receiving the whole model by postMessage (a
//                    structured clone) and running one task.
// Environment: PARTS, NS, REPS.
const { Worker } = require("worker_threads");
const { performance } = require("perf_hooks");
const v8 = require("v8");
const m = require(__dirname + "/model.js");
const PARTS = Number(process.env.PARTS || 10000);
const NS = (process.env.NS || "1,2,4,6,12").split(",").map(Number);
const REPS = Number(process.env.REPS || 3);
const VARIANT = process.env.VARIANT || "main";

function median(xs) { const s = xs.slice().sort((a, b) => a - b); return s[(s.length - 1) >> 1]; }
const r0 = (x) => Math.round(x);

async function main() {
    const t0 = performance.now();
    const model = m.build(PARTS);
    const buildMs = performance.now() - t0;
    const heapLimitMB = Math.round(v8.getHeapStatistics().heap_size_limit / 1048576);
    const afterBuild = { rss_mb: r0(process.memoryUsage().rss / 1048576), heap_used_mb: r0(process.memoryUsage().heapUsed / 1048576) };
    const nmax = Math.max(...NS);
    const seq = [], seqTimes = [];
    for (let k = 0; k < nmax; k++) {
        const ts = performance.now();
        seq.push(m.task(model, k));
        seqTimes.push(Math.round(performance.now() - ts));
    }
    const out = { bench: "cad", runtime: "node", variant: VARIANT, parts: PARTS, ok: true, heap_limit_mb: heapLimitMB,
                  counts: model.counts, build_ms: r0(buildMs), after_build: afterBuild, seq_task_ms: seqTimes,
                  checksums: seq.map((r) => r.checksum), results: [] };
    if (VARIANT === "workers") {
        const n = NS[0];
        const workers = await Promise.all(Array.from({ length: n }, () => new Promise((resolve, reject) => {
            const w = new Worker(__dirname + "/node_cad_worker.js");
            w.once("message", () => resolve(w));
            w.once("error", reject);
        })));
        const times = [], posts = [], clones = [], computes = [];
        for (let rep = 0; rep < REPS; rep++) {
            const r = await new Promise((resolve, reject) => {
                const res = new Array(n), sentAt = new Array(n);
                let pending = n, cloneMax = 0, computeMax = 0;
                const t1 = performance.now();
                workers.forEach((w, k) => {
                    w.once("message", (msg) => {
                        res[k] = msg.result;
                        cloneMax = Math.max(cloneMax, msg.recvAt - sentAt[k]);
                        computeMax = Math.max(computeMax, msg.computeMs);
                        if (--pending === 0) resolve({ res, ms: performance.now() - t1, postMs, cloneMax, computeMax });
                    });
                    w.once("error", reject);
                });
                const p0 = performance.now();
                workers.forEach((w, k) => { sentAt[k] = performance.timeOrigin + performance.now(); w.postMessage({ model, k }); });
                const postMs = performance.now() - p0;
            });
            for (let k = 0; k < n; k++) out.ok = out.ok && r.res[k].checksum === seq[k].checksum;
            times.push(r.ms); posts.push(r.postMs); clones.push(r.cloneMax); computes.push(r.computeMax);
        }
        await Promise.all(workers.map((w) => w.terminate()));
        out.results.push({ n, times_ms: times.map(r0), median_ms: r0(median(times)), post_ms: r0(median(posts)),
                           clone_ms: r0(median(clones)), compute_ms: r0(median(computes)) });
    }
    out.peak_rss_kb = process.resourceUsage().maxRSS;
    console.log(JSON.stringify(out));
}
main().catch((e) => { console.log("FAILED: " + (e && e.stack || e)); process.exit(1); });
