#!/usr/bin/env node
// Aggregated benchmark runner: runs the standard comparison N times back-to-back
// and reports the median per-benchmark time + median geomean.  Each inner run
// already takes the median of 5 in-script samples, so 12 outer × 5 inner = 60
// time samples per benchmark — robust against single-run noise.
//
// Usage: node run_aggregated.js [N]      (default N=12)

const { execSync } = require('child_process');
const fs   = require('fs');
const path = require('path');

const N = parseInt(process.argv[2] || '12', 10);
const RESULTS_DIR = path.join(__dirname, 'results');
const REPORT_PATH = path.join(RESULTS_DIR, 'standard_comparison.json');

function median(arr) {
    const a = arr.slice().sort((x, y) => x - y);
    const m = Math.floor(a.length / 2);
    return a.length % 2 ? a[m] : (a[m - 1] + a[m]) / 2;
}
function geomean(arr) {
    let g = 1;
    for (const v of arr) g *= v;
    return Math.pow(g, 1 / arr.length);
}

const perBenchProto = {};
const perBenchNode  = {};
const perBenchSpeed = {};
const geomeans      = [];

console.log(`Running ${N} aggregated rounds…`);
for (let i = 0; i < N; i++) {
    process.stdout.write(`  round ${i + 1}/${N} … `);
    const t0 = Date.now();
    execSync(`node "${path.join(__dirname, 'run_standard_comparison.js')}"`, {
        cwd: __dirname, stdio: 'pipe',
    });
    const data = JSON.parse(fs.readFileSync(REPORT_PATH, 'utf8'));
    geomeans.push(data.summary.geoMeanNodeOverProto);
    for (const r of data.results) {
        if (r.protojs_time_ms == null || r.nodejs_time_ms == null) continue;
        (perBenchProto[r.name] ||= []).push(r.protojs_time_ms);
        (perBenchNode [r.name] ||= []).push(r.nodejs_time_ms);
        (perBenchSpeed[r.name] ||= []).push(r.faster === 'Node.js' ? r.speedup : 1 / r.speedup);
    }
    console.log(`${((Date.now() - t0) / 1000).toFixed(1)}s, geomean ${geomeans[geomeans.length - 1].toFixed(2)}x`);
}

console.log('\n' + '='.repeat(78));
console.log(`Aggregated comparison (median of ${N} rounds, each round = median of 5 in-script)`);
console.log('='.repeat(78));
console.log('\nBenchmark               protoJS_med (ms)   Node_med (ms)   speedup_med');
console.log('-'.repeat(78));
const names = Object.keys(perBenchSpeed).sort();
for (const n of names) {
    const pm = median(perBenchProto[n]);
    const nm = median(perBenchNode [n]);
    const sm = median(perBenchSpeed[n]);
    console.log(`${n.padEnd(24)}${pm.toFixed(2).padStart(14)}   ${nm.toFixed(2).padStart(11)}   ${sm.toFixed(2).padStart(10)}x`);
}
console.log('-'.repeat(78));
const geomedAcross = median(geomeans);
const geoOfMedians = geomean(names.map(n => median(perBenchSpeed[n])));
console.log(`Median geomean across rounds:   Node.js ${geomedAcross.toFixed(2)}x faster`);
console.log(`Geomean of per-bench medians:   Node.js ${geoOfMedians.toFixed(2)}x faster`);

const out = {
    timestamp: new Date().toISOString(),
    rounds: N,
    geomeans_per_round: geomeans,
    median_geomean: geomedAcross,
    geomean_of_medians: geoOfMedians,
    per_benchmark: names.map(n => ({
        name: n,
        protojs_ms_median: median(perBenchProto[n]),
        nodejs_ms_median:  median(perBenchNode [n]),
        speedup_median:    median(perBenchSpeed[n]),
        protojs_ms_samples: perBenchProto[n],
    })),
};
const outPath = path.join(RESULTS_DIR, 'aggregated_comparison.json');
fs.writeFileSync(outPath, JSON.stringify(out, null, 2));
console.log(`\nReport: ${outPath}`);
