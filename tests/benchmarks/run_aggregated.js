#!/usr/bin/env node
// Aggregated benchmark runner: runs the standard comparison N times back-to-back
// and reports the median per-benchmark time + median geomean.  Each inner run
// already takes the median of 5 in-script samples, so 12 outer × 5 inner = 60
// time samples per benchmark — robust against single-run noise.
//
// Usage:
//   node run_aggregated.js [N]            # protoJS vs Node.js (default)
//   node run_aggregated.js [N] --quickjs  # protoJS vs QuickJS

const { execSync } = require('child_process');
const fs   = require('fs');
const path = require('path');

const args = process.argv.slice(2);
const useQuickjs  = args.includes('--quickjs');
const N = parseInt(args.find(a => /^\d+$/.test(a)) || '12', 10);

const RESULTS_DIR = path.join(__dirname, 'results');
const RUNNER_SCRIPT = useQuickjs
    ? path.join(__dirname, 'run_standard_comparison_quickjs.js')
    : path.join(__dirname, 'run_standard_comparison.js');
const REPORT_PATH = path.join(RESULTS_DIR,
    useQuickjs ? 'standard_comparison_quickjs.json' : 'standard_comparison.json');
const OTHER_RUNTIME       = useQuickjs ? 'QuickJS'                    : 'Node.js';
const OTHER_TIME_FIELD    = useQuickjs ? 'quickjs_time_ms'            : 'nodejs_time_ms';
const OTHER_GEOMEAN_FIELD = useQuickjs ? 'geoMeanQuickjsOverProto'    : 'geoMeanNodeOverProto';
const OUT_REPORT_NAME     = useQuickjs ? 'aggregated_comparison_quickjs.json'
                                       : 'aggregated_comparison.json';

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
const perBenchOther = {};
const perBenchSpeed = {};
const geomeans      = [];

console.log(`Running ${N} aggregated rounds (protoJS vs ${OTHER_RUNTIME})…`);
for (let i = 0; i < N; i++) {
    process.stdout.write(`  round ${i + 1}/${N} … `);
    const t0 = Date.now();
    execSync(`node "${RUNNER_SCRIPT}"`, { cwd: __dirname, stdio: 'pipe' });
    const data = JSON.parse(fs.readFileSync(REPORT_PATH, 'utf8'));
    geomeans.push(data.summary[OTHER_GEOMEAN_FIELD]);
    for (const r of data.results) {
        if (r.protojs_time_ms == null || r[OTHER_TIME_FIELD] == null) continue;
        (perBenchProto[r.name] ||= []).push(r.protojs_time_ms);
        (perBenchOther[r.name] ||= []).push(r[OTHER_TIME_FIELD]);
        (perBenchSpeed[r.name] ||= []).push(r.faster === OTHER_RUNTIME ? r.speedup : 1 / r.speedup);
    }
    console.log(`${((Date.now() - t0) / 1000).toFixed(1)}s, geomean ${geomeans[geomeans.length - 1].toFixed(2)}x`);
}

console.log('\n' + '='.repeat(78));
console.log(`Aggregated comparison (median of ${N} rounds, each round = median of 5 in-script)`);
console.log('='.repeat(78));
const otherCol = `${OTHER_RUNTIME}_med (ms)`;
console.log(`\nBenchmark               protoJS_med (ms)   ${otherCol.padEnd(15)}  speedup_med`);
console.log('-'.repeat(78));
const names = Object.keys(perBenchSpeed).sort();
for (const n of names) {
    const pm = median(perBenchProto[n]);
    const nm = median(perBenchOther[n]);
    const sm = median(perBenchSpeed[n]);
    console.log(`${n.padEnd(24)}${pm.toFixed(2).padStart(14)}   ${nm.toFixed(2).padStart(11)}   ${sm.toFixed(2).padStart(10)}x`);
}
console.log('-'.repeat(78));
const geomedAcross = median(geomeans);
const geoOfMedians = geomean(names.map(n => median(perBenchSpeed[n])));
console.log(`Median geomean across rounds:   ${OTHER_RUNTIME} ${geomedAcross.toFixed(2)}x faster`);
console.log(`Geomean of per-bench medians:   ${OTHER_RUNTIME} ${geoOfMedians.toFixed(2)}x faster`);

const out = {
    timestamp: new Date().toISOString(),
    rounds: N,
    other_runtime: OTHER_RUNTIME,
    geomeans_per_round: geomeans,
    median_geomean: geomedAcross,
    geomean_of_medians: geoOfMedians,
    per_benchmark: names.map(n => ({
        name: n,
        protojs_ms_median: median(perBenchProto[n]),
        other_ms_median:   median(perBenchOther[n]),
        speedup_median:    median(perBenchSpeed[n]),
        protojs_ms_samples: perBenchProto[n],
    })),
};
const outPath = path.join(RESULTS_DIR, OUT_REPORT_NAME);
fs.writeFileSync(outPath, JSON.stringify(out, null, 2));
console.log(`\nReport: ${outPath}`);
