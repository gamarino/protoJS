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
        const execTimeoutMs = 60000; // per-benchmark timeout so heavy/async benchmarks (e.g. parallel_cpu) don't hang
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

    let speedup = null;
    let faster = null;
    if (protojsTime != null && nodeTime != null && nodeTime > 0) {
        speedup = nodeTime / protojsTime;
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
        let geo = 1;
        successful.forEach(r => {
            const ratio = r.nodejs_time_ms / r.protojs_time_ms;
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
            geoMeanNodeOverProto: successful.length ? Math.pow(successful.reduce((g, r) => g * (r.nodejs_time_ms / r.protojs_time_ms), 1), 1 / successful.length) : null,
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
