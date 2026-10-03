// protoJS side of the CAD benchmark: the model is built once on the main
// thread; for each N, N Deferreds run tasks 0..N-1 over the shared model
// (no copy). Environment: PARTS, NS (e.g. "1,2,4,6,12"), REPS.
const m = require(__dirname + "/model.js");
const PARTS = Number(process.env.PARTS || 10000);
const NS = (process.env.NS || "1,2,4,6,12").split(",").map(Number);
const REPS = Number(process.env.REPS || 3);

function median(xs) { const s = xs.slice().sort((a, b) => a - b); return s[(s.length - 1) >> 1]; }
function rssMB() { return Math.round(process.memoryUsage().rss / 1048576); }

async function main() {
    const t0 = Date.now();
    const model = m.build(PARTS);
    const buildMs = Date.now() - t0;
    console.error("[cad] built " + PARTS + " parts in " + buildMs + " ms");
    const afterBuild = { rss_mb: rssMB(), heap_used_mb: Math.round(process.memoryUsage().heapUsed / 1048576),
                         gc: protoCore.gcStats() };
    // Sequential reference: each task once on the main thread.  Its time
    // for task k stands for every k' = k (mod 4) (same analysis, another
    // parameter), so the sequential time of N tasks is the sum over k < N.
    const nmax = Math.max(...NS);
    const seq = [], seqTimes = [];
    for (let k = 0; k < nmax; k++) {
        const ts = Date.now();
        seq.push(m.task(model, k));
        seqTimes.push(Date.now() - ts);
        console.error("[cad] sequential task " + k + ": " + seqTimes[k] + " ms");
        if (k >= 3 && process.env.FULL_SEQ !== "1") break;
    }
    const seqTaskMs = (k) => seqTimes[k < seqTimes.length ? k : k % 4];
    const results = [];
    let ok = true;
    let checksums = seq.map((r) => r.checksum);
    for (const n of NS) {
        const times = [];
        for (let rep = 0; rep < REPS; rep++) {
            const t1 = Date.now();
            const r = await Promise.all(Array.from({ length: n }, (_, k) => new Deferred(() => m.task(model, k))));
            times.push(Date.now() - t1);
            for (let k = 0; k < n; k++) ok = ok && r[k].k === k && (k >= seq.length || r[k].checksum === seq[k].checksum);
            checksums = r.map((x) => x.checksum);
        }
        let seqN = 0;
        for (let k = 0; k < n; k++) seqN += seqTaskMs(k);
        results.push({ n, times_ms: times, median_ms: median(times), seq_ms: seqN, rss_mb: rssMB() });
        console.error("[cad] N=" + n + ": " + times.join(",") + " ms");
    }
    const gc = protoCore.gcStats();
    console.log(JSON.stringify({ bench: "cad", runtime: "protojs", variant: "deferred", parts: PARTS, ok,
        counts: model.counts, build_ms: buildMs, after_build: afterBuild, seq_task_ms: seqTimes,
        checksums, results,
        peak_rss_kb: process.resourceUsage().maxRSS, gc_cycles: gc.cycles, heap_limit_cells: gc.heapLimitCells }));
}
main().catch((e) => { console.log("FAILED: " + (e && e.stack || e)); process.exit(1); });
