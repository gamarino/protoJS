#!/usr/bin/env node
// Standard benchmark comparison: protoJS vs Node.js
// Runs self-contained benchmarks in tests/benchmarks/standard/ that output
// __BENCH_RESULT__<json> with in-process time_ms. Uses that for fair comparison.

const { exec } = require('child_process');
const fs = require('fs');
const path = require('path');

const STANDARD_DIR = path.join(__dirname, 'standard');
const RESULTS_DIR = path.join(__dirname, 'results');

const BENCH_RESULT_PREFIX = '__BENCH_RESULT__';

function findProtojs() {
    // Allow caller to pin the protojs binary via env var (handy for
    // running against an experimental build without nuking ../build).
    if (process.env.PROTOJS_BIN) {
        if (fs.existsSync(process.env.PROTOJS_BIN)) return process.env.PROTOJS_BIN;
        console.error(`PROTOJS_BIN=${process.env.PROTOJS_BIN} does not exist`);
        return null;
    }
    const candidates = [
        path.join(__dirname, '../../build/protojs'),
        path.join(__dirname, '../build/protojs'),
        path.resolve(__dirname, '../../build/protojs'),
        path.resolve(__dirname, '../build/protojs'),
    ];
    for (const p of candidates) {
        if (fs.existsSync(p)) return p;
    }
    return null;
}

function runBenchmark(benchmarkFile, runtime) {
    return new Promise((resolve, reject) => {
        const scriptPath = path.join(STANDARD_DIR, benchmarkFile);
        const cmd = runtime === 'protojs'
            ? `"${findProtojs()}" "${scriptPath}"`
            : `node "${scriptPath}"`;
        const startWall = Date.now();
        const execTimeoutMs = 120000; // per-benchmark timeout (parallel_cpu with protojs uses 2e5 iter to complete in time)
        exec(cmd, { cwd: __dirname, maxBuffer: 2 * 1024 * 1024, timeout: execTimeoutMs }, (error, stdout, stderr) => {
            const wallClockMs = Date.now() - startWall;
            if (error) {
                reject({ runtime, file: benchmarkFile, error: error.message, stderr: stderr || '' });
                return;
            }
            const lines = (stdout || '').trim().split('\n');
            let parsed = null;
            for (let i = lines.length - 1; i >= 0; i--) {
                const idx = lines[i].indexOf(BENCH_RESULT_PREFIX);
                if (idx !== -1) {
                    try {
                        parsed = JSON.parse(lines[i].substring(idx + BENCH_RESULT_PREFIX.length));
                        break;
                    } catch (e) {
                        parsed = null;
                    }
                }
            }
            resolve({
                runtime,
                file: benchmarkFile,
                stdout: stdout || '',
                stderr: stderr || '',
                parsed,
                wallClockMs,
            });
        });
    });
}

async function compareOne(benchmarkFile) {
    const protojsResult = await runBenchmark(benchmarkFile, 'protojs');
    const nodeResult = await runBenchmark(benchmarkFile, 'node');

    const protojsTime = protojsResult.parsed && typeof protojsResult.parsed.time_ms === 'number'
        ? protojsResult.parsed.time_ms
        : null;
    const nodeTime = nodeResult.parsed && typeof nodeResult.parsed.time_ms === 'number'
        ? nodeResult.parsed.time_ms
        : null;

    // When Node finishes in under 1 ms its in-script median floors to 0; that
    // does not mean the bench failed to parse, it means it ran below the
    // Date.now() resolution.  Floor to 0.5 ms (half the timer tick) for ratio
    // purposes so the comparison still reports a meaningful "Node Nx faster"
    // rather than dropping the row entirely.  The same applies symmetrically
    // for protoJS, but in practice protoJS is never sub-millisecond on these
    // benches, so the floor only affects the Node side.
    const TIMER_FLOOR_MS = 0.5;
    const protojsForRatio = (protojsTime != null && protojsTime <= 0) ? TIMER_FLOOR_MS : protojsTime;
    const nodeForRatio    = (nodeTime    != null && nodeTime    <= 0) ? TIMER_FLOOR_MS : nodeTime;

    let speedup = null;
    let faster = null;
    if (protojsForRatio != null && nodeForRatio != null && protojsForRatio > 0) {
        speedup = nodeForRatio / protojsForRatio;
        faster = speedup > 1 ? 'protoJS' : 'Node.js';
        if (speedup < 1) speedup = 1 / speedup;
    }

    return {
        benchmark: benchmarkFile,
        name: (protojsResult.parsed && protojsResult.parsed.name) || benchmarkFile.replace('.js', ''),
        protojs_time_ms: protojsTime,
        nodejs_time_ms: nodeTime,
        speedup,
        faster,
        protojs_ok: protojsResult.parsed != null,
        nodejs_ok: nodeResult.parsed != null,
        protojs_stderr: (protojsResult.stderr || '').trim(),
        nodejs_stderr: (nodeResult.stderr || '').trim(),
    };
}

function generateReport(results) {
    const successful = results.filter(r => r.protojs_time_ms != null && r.nodejs_time_ms != null && r.speedup != null);
    const failed = results.filter(r => r.protojs_time_ms == null || r.nodejs_time_ms == null);

    console.log('\n' + '='.repeat(70));
    console.log('Standard benchmark comparison (in-process time_ms)');
    console.log('='.repeat(70));

    if (successful.length > 0) {
        console.log('\nBenchmark results (median time_ms from 5 runs):');
        console.log('-'.repeat(70));
        // For the geomean, reuse the per-row speedup (which already uses the
        // sub-tick TIMER_FLOOR_MS for either side) so 0 ms rows do not
        // multiply the running product by zero and zero out the whole mean.
        let geo = 1;
        successful.forEach(r => {
            // r.speedup is always ≥ 1, oriented by r.faster.  Re-orient back
            // to nodejs/protojs so the geomean has a single signed direction.
            const ratio = r.faster === 'Node.js' ? r.speedup : (1 / r.speedup);
            if (ratio > 0) geo *= ratio;
            console.log(`${r.name}: protoJS ${r.protojs_time_ms.toFixed(2)} ms, Node ${r.nodejs_time_ms.toFixed(2)} ms => ${r.faster} ${r.speedup.toFixed(2)}x`);
        });
        geo = Math.pow(geo, 1 / successful.length);
        const nodeFasterX = geo < 1 ? (1 / geo).toFixed(2) : geo.toFixed(2);
        console.log('-'.repeat(70));
        console.log(`Geometric mean: Node.js ${nodeFasterX}x faster than protoJS (in-process time)`);
        console.log(`protoJS wins: ${successful.filter(r => r.faster === 'protoJS').length}/${successful.length}`);
    }

    if (failed.length > 0) {
        console.log('\nFailed or no result:');
        failed.forEach(r => {
            console.log(`  ${r.benchmark}: protojs_ok=${r.protojs_ok}, nodejs_ok=${r.nodejs_ok}`);
            if (r.protojs_stderr) console.log(`    protojs stderr: ${r.protojs_stderr.slice(0, 200)}`);
            if (r.nodejs_stderr) console.log(`    node stderr: ${r.nodejs_stderr.slice(0, 200)}`);
        });
    }

    const reportPath = path.join(RESULTS_DIR, 'standard_comparison.json');
    if (!fs.existsSync(RESULTS_DIR)) fs.mkdirSync(RESULTS_DIR, { recursive: true });
    fs.writeFileSync(reportPath, JSON.stringify({
        timestamp: new Date().toISOString(),
        results,
        summary: {
            total: results.length,
            successful: successful.length,
            failed: failed.length,
            geoMeanNodeOverProto: successful.length
                ? Math.pow(
                    successful.reduce(
                        (g, r) => g * (r.faster === 'Node.js' ? r.speedup : (1 / r.speedup)),
                        1),
                    1 / successful.length)
                : null,
        },
    }, null, 2));
    console.log(`\nJSON report: ${reportPath}`);
}

async function main() {
    const protojsPath = findProtojs();
    if (!protojsPath) {
        console.error('protojs not found. Build with: cmake -B build -S . && cmake --build build');
        process.exit(1);
    }
    console.log('Using protojs:', protojsPath);
    console.log('Standard benchmarks dir:', STANDARD_DIR);

    let files = [];
    try {
        files = fs.readdirSync(STANDARD_DIR).filter(f => f.endsWith('.js') && !f.endsWith('_worker.js'));
    } catch (e) {
        console.error('Cannot read standard dir:', e.message);
        process.exit(1);
    }
    if (files.length === 0) {
        console.error('No .js files in standard/');
        process.exit(1);
    }
    files.sort();

    const results = [];
    for (const f of files) {
        console.log(`\nRunning: ${f}`);
        try {
            const r = await compareOne(f);
            results.push(r);
            if (r.speedup != null) {
                console.log(`  => ${r.faster} ${r.speedup.toFixed(2)}x (protoJS ${r.protojs_time_ms.toFixed(2)} ms, Node ${r.nodejs_time_ms.toFixed(2)} ms)`);
            } else {
                console.log('  => no in-process time (parse failed or error)');
            }
        } catch (e) {
            console.error('  Error:', e.message);
            results.push({ benchmark: f, error: e.message, protojs_time_ms: null, nodejs_time_ms: null });
        }
    }

    generateReport(results);
    console.log('\nDone.');
}

if (require.main === module) {
    main().catch(err => {
        console.error(err);
        process.exit(1);
    });
}

module.exports = { compareOne, runBenchmark, findProtojs, main };
