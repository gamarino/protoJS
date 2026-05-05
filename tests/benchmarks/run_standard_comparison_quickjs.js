#!/usr/bin/env node
// Standard benchmark comparison: protoJS vs QuickJS (vanilla interpreter).
// Runs self-contained benchmarks in tests/benchmarks/standard/ that output
// __BENCH_RESULT__<json> with in-process time_ms. Keeps interpreter-vs-interpreter comparison.
// Use run_standard_comparison.js for protoJS vs Node.js.

const { exec } = require('child_process');
const fs = require('fs');
const path = require('path');

const STANDARD_DIR = path.join(__dirname, 'standard');
const RESULTS_DIR = path.join(__dirname, 'results');
const QUICKJS_SRC = path.join(__dirname, '../../deps/quickjs');

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

function findQjs() {
    const candidates = [
        path.join(__dirname, 'qjs_minimal'),
        path.join(__dirname, '../../qjs_raw'),
    ];
    for (const p of candidates) {
        if (fs.existsSync(p)) return p;
    }
    return null;
}

function runBenchmark(benchmarkFile, runtime, protojsPath, qjsPath) {
    return new Promise((resolve, reject) => {
        const scriptPath = path.join(STANDARD_DIR, benchmarkFile);
        let cmd;
        if (runtime === 'protojs') {
            cmd = `"${protojsPath}" "${scriptPath}"`;
        } else if (runtime === 'quickjs') {
            cmd = `"${qjsPath}" "${scriptPath}"`;
        } else {
            reject(new Error('unknown runtime ' + runtime));
            return;
        }
        const startWall = Date.now();
        exec(cmd, { cwd: __dirname, maxBuffer: 2 * 1024 * 1024 }, (error, stdout, stderr) => {
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

async function compareOne(benchmarkFile, protojsPath, qjsPath) {
    const protojsResult = await runBenchmark(benchmarkFile, 'protojs', protojsPath, qjsPath);
    const quickjsResult = await runBenchmark(benchmarkFile, 'quickjs', protojsPath, qjsPath);

    const protojsTime = protojsResult.parsed && typeof protojsResult.parsed.time_ms === 'number'
        ? protojsResult.parsed.time_ms
        : null;
    const quickjsTime = quickjsResult.parsed && typeof quickjsResult.parsed.time_ms === 'number'
        ? quickjsResult.parsed.time_ms
        : null;

    // See run_standard_comparison.js for rationale: floor a 0 ms in-script
    // median to 0.5 ms (half a Date.now() tick) so sub-millisecond benches
    // still report a valid ratio instead of being dropped as "parse failed".
    const TIMER_FLOOR_MS = 0.5;
    const protojsForRatio = (protojsTime != null && protojsTime <= 0) ? TIMER_FLOOR_MS : protojsTime;
    const quickjsForRatio = (quickjsTime != null && quickjsTime <= 0) ? TIMER_FLOOR_MS : quickjsTime;

    let speedup = null;
    let faster = null;
    if (protojsForRatio != null && quickjsForRatio != null && protojsForRatio > 0) {
        speedup = quickjsForRatio / protojsForRatio;
        faster = speedup > 1 ? 'protoJS' : 'QuickJS';
        if (speedup < 1) speedup = 1 / speedup;
    }

    return {
        benchmark: benchmarkFile,
        name: (protojsResult.parsed && protojsResult.parsed.name) || benchmarkFile.replace('.js', ''),
        protojs_time_ms: protojsTime,
        quickjs_time_ms: quickjsTime,
        speedup,
        faster,
        protojs_ok: protojsResult.parsed != null,
        quickjs_ok: quickjsResult.parsed != null,
        protojs_stderr: (protojsResult.stderr || '').trim(),
        quickjs_stderr: (quickjsResult.stderr || '').trim(),
    };
}

function generateReport(results) {
    const successful = results.filter(r => r.protojs_time_ms != null && r.quickjs_time_ms != null && r.speedup != null);
    const failed = results.filter(r => r.protojs_time_ms == null || r.quickjs_time_ms == null);

    console.log('\n' + '='.repeat(70));
    console.log('Standard benchmark comparison: protoJS vs QuickJS (in-process time_ms)');
    console.log('='.repeat(70));

    if (successful.length > 0) {
        console.log('\nBenchmark results (median time_ms from 5 runs):');
        console.log('-'.repeat(70));
        // Use the per-row speedup (already TIMER_FLOOR_MS-corrected) so that
        // sub-millisecond benches do not zero-out the geomean product.
        let geo = 1;
        successful.forEach(r => {
            const ratio = r.faster === 'QuickJS' ? r.speedup : (1 / r.speedup);
            if (ratio > 0) geo *= ratio;
            console.log(`${r.name}: protoJS ${r.protojs_time_ms.toFixed(2)} ms, QuickJS ${r.quickjs_time_ms.toFixed(2)} ms => ${r.faster} ${r.speedup.toFixed(2)}x`);
        });
        geo = Math.pow(geo, 1 / successful.length);
        const quickjsFasterX = geo < 1 ? (1 / geo).toFixed(2) : geo.toFixed(2);
        console.log('-'.repeat(70));
        console.log(`Geometric mean: QuickJS ${quickjsFasterX}x vs protoJS (in-process time)`);
        console.log(`protoJS wins: ${successful.filter(r => r.faster === 'protoJS').length}/${successful.length}`);
    }

    if (failed.length > 0) {
        console.log('\nFailed or no result:');
        failed.forEach(r => {
            console.log(`  ${r.benchmark}: protojs_ok=${r.protojs_ok}, quickjs_ok=${r.quickjs_ok}`);
            if (r.protojs_stderr) console.log(`    protojs stderr: ${r.protojs_stderr.slice(0, 200)}`);
            if (r.quickjs_stderr) console.log(`    quickjs stderr: ${r.quickjs_stderr.slice(0, 200)}`);
        });
    }

    const reportPath = path.join(RESULTS_DIR, 'standard_comparison_quickjs.json');
    if (!fs.existsSync(RESULTS_DIR)) fs.mkdirSync(RESULTS_DIR, { recursive: true });
    fs.writeFileSync(reportPath, JSON.stringify({
        timestamp: new Date().toISOString(),
        results,
        summary: {
            total: results.length,
            successful: successful.length,
            failed: failed.length,
            geoMeanQuickjsOverProto: successful.length
                ? Math.pow(
                    successful.reduce(
                        (g, r) => g * (r.faster === 'QuickJS' ? r.speedup : (1 / r.speedup)),
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
    const qjsPath = findQjs();
    if (!qjsPath) {
        console.error('QuickJS qjs not found. Build it with: cd deps/quickjs && make qjs');
        console.error('  Then run this script again from tests/benchmarks/');
        process.exit(1);
    }
    console.log('Using protojs:', protojsPath);
    console.log('Using QuickJS:', qjsPath);
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
            const r = await compareOne(f, protojsPath, qjsPath);
            results.push(r);
            if (r.speedup != null) {
                console.log(`  => ${r.faster} ${r.speedup.toFixed(2)}x (protoJS ${r.protojs_time_ms.toFixed(2)} ms, QuickJS ${r.quickjs_time_ms.toFixed(2)} ms)`);
            } else {
                console.log('  => no in-process time (parse failed or error)');
            }
        } catch (e) {
            console.error('  Error:', e.message);
            results.push({ benchmark: f, error: e.message, protojs_time_ms: null, quickjs_time_ms: null });
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

module.exports = { compareOne, runBenchmark, findProtojs, findQjs, main };
