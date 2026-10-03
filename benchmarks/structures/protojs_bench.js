// protoJS side of the structure benchmarks (see README.md).
//
// The shared data is built once on the main thread. MODE=seq runs tasks
// 0..N-1 one after another on the main thread; MODE=par runs them as N
// Deferreds, which read the main thread's data directly: nothing is copied.
// Every task returns a checksum of its results; the runner compares them
// with Node.js and across modes. Environment: WORKLOAD, MODE, N, REPS, SCALE.
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

async function main() {
    const tb = Date.now();
    const data = wl.build(SCALE);
    const buildMs = Date.now() - tb;
    const gcAfterBuild = protoCore.gcStats();

    if (MODE === "par") {
        // Start the pool (it starts on the first Deferred) outside the timing.
        const warm = [];
        for (let i = 0; i < N; i++) warm.push(new Deferred(() => i));
        await Promise.all(warm);
    }

    const times = [];
    let checksums = null;
    let ok = true;
    for (let rep = 0; rep < REPS; rep++) {
        const t0 = Date.now();
        let results;
        if (MODE === "seq") {
            results = [];
            for (let k = 0; k < N; k++) results.push(wl.task(data, k));
        } else {
            const tasks = [];
            for (let k = 0; k < N; k++) tasks.push(new Deferred(() => wl.task(data, k)));
            results = await Promise.all(tasks);
        }
        times.push(Date.now() - t0);
        const cs = results.map((r) => r.checksum);
        ok = ok && results.length === N && results.every((r, i) => r.k === i && typeof r.checksum === "number");
        if (checksums === null) checksums = cs;
        else ok = ok && cs.join() === checksums.join();
    }
    const gc = protoCore.gcStats();
    console.log(JSON.stringify({
        bench: "structures", runtime: "protojs", workload: WORKLOAD, mode: MODE, n: N, scale: SCALE, ok,
        build_ms: buildMs, times_ms: times, median_ms: median(times), checksums,
        peak_rss_kb: process.resourceUsage().maxRSS,
        heap_limit_cells: gc.heapLimitCells, gc_cycles: gc.cycles,
        live_cells_after_build: gcAfterBuild.liveCellsLastCycle, gc_cycles_after_build: gcAfterBuild.cycles,
    }));
}

main().catch((e) => { console.log("FAILED: " + (e && e.stack || e)); process.exit(1); });
