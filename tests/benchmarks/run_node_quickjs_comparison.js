#!/usr/bin/env node
// Three-way benchmark comparison: Node.js vs QuickJS vs protoJS.
// Runs self-contained benchmarks in tests/benchmarks/standard/ that output
// __BENCH_RESULT__<json> with in-process time_ms. Uses that for fair comparison.
//
// Usage: node run_node_quickjs_comparison.js
// Requires: Node.js, built protojs (../../build/protojs), built qjs (deps/quickjs/qjs)

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
        path.join(QUICKJS_SRC, 'qjs'),
        path.join(QUICKJS_SRC, 'qjs.exe'),
        path.resolve(QUICKJS_SRC, 'qjs'),
    ];
    for (const p of candidates) {
        if (fs.existsSync(p)) return p;
    }
    try {
        const r = require('child_process').execSync('which qjs 2>/dev/null', { encoding: 'utf8' });
        const p = (r || '').trim();
        if (p && fs.existsSync(p)) return p;
    } catch (_) {}
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
        } else if (runtime === 'node') {
            cmd = `node "${scriptPath}"`;
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
    const results = { node: null, quickjs: null, protojs: null };
    const names = { node: 'Node.js', quickjs: 'QuickJS', protojs: 'protoJS' };

    for (const rt of ['node', 'quickjs', 'protojs']) {
        try {
            const r = rt === 'node'
                ? await runBenchmark(benchmarkFile, 'node', protojsPath, qjsPath)
                : rt === 'quickjs'
                    ? await runBenchmark(benchmarkFile, 'quickjs', protojsPath, qjsPath)
                    : await runBenchmark(benchmarkFile, 'protojs', protojsPath, qjsPath);
            const time = r.parsed && typeof r.parsed.time_ms === 'number' ? r.parsed.time_ms : null;
            results[rt] = { time_ms: time, ok: time != null, stderr: (r.stderr || '').trim() };
        } catch (e) {
            results[rt] = { time_ms: null, ok: false, stderr: e.stderr || e.message || '' };
        }
    }

    const nodeTime = results.node && results.node.ok ? results.node.time_ms : null;
    const quickjsTime = results.quickjs && results.quickjs.ok ? results.quickjs.time_ms : null;
    const protojsTime = results.protojs && results.protojs.ok ? results.protojs.time_ms : null;

    let fastest = null;
    let fastestTime = Infinity;
    if (nodeTime != null && nodeTime < fastestTime) { fastest = 'Node.js'; fastestTime = nodeTime; }
    if (quickjsTime != null && quickjsTime < fastestTime) { fastest = 'QuickJS'; fastestTime = quickjsTime; }
    if (protojsTime != null && protojsTime < fastestTime) { fastest = 'protoJS'; fastestTime = protojsTime; }

    return {
        benchmark: benchmarkFile,
        name: (results.node?.ok && results.node.time_ms) ? benchmarkFile.replace('.js', '') : benchmarkFile.replace('.js', ''),
        node_time_ms: nodeTime,
        quickjs_time_ms: quickjsTime,
        protojs_time_ms: protojsTime,
        fastest,
        node_ok: results.node?.ok ?? false,
        quickjs_ok: results.quickjs?.ok ?? false,
        protojs_ok: results.protojs?.ok ?? false,
        node_stderr: results.node?.stderr || '',
        quickjs_stderr: results.quickjs?.stderr || '',
        protojs_stderr: results.protojs?.stderr || '',
    };
}

function generateReport(results) {
    const allThree = results.filter(r =>
        r.node_time_ms != null && r.quickjs_time_ms != null && r.protojs_time_ms != null);
    const nodeAndQuickjs = results.filter(r => r.node_time_ms != null && r.quickjs_time_ms != null);
    const failed = results.filter(r =>
        r.node_time_ms == null && r.quickjs_time_ms == null && r.protojs_time_ms == null);

    console.log('\n' + '='.repeat(72));
    console.log('Benchmark comparison: Node.js vs QuickJS vs protoJS (in-process time_ms)');
    console.log('='.repeat(72));

    const displaySet = allThree.length > 0 ? allThree : nodeAndQuickjs;
    if (displaySet.length > 0) {
        console.log('\nResults (lower is better):');
        console.log('-'.repeat(72));
        console.log(
            'Benchmark'.padEnd(24) +
            'Node.js (ms)'.padStart(14) +
            'QuickJS (ms)'.padStart(14) +
            'protoJS (ms)'.padStart(14) +
            '  Winner'
        );
        console.log('-'.repeat(72));

        let nodeWins = 0, quickjsWins = 0, protojsWins = 0;
        displaySet.forEach(r => {
            const n = (r.node_time_ms != null ? r.node_time_ms.toFixed(2) : '-').padStart(12);
            const q = (r.quickjs_time_ms != null ? r.quickjs_time_ms.toFixed(2) : '-').padStart(12);
            const p = (r.protojs_time_ms != null ? r.protojs_time_ms.toFixed(2) : '-').padStart(12);
            let winner = r.fastest;
            if (winner == null && r.node_time_ms != null && r.quickjs_time_ms != null) {
                winner = r.node_time_ms <= r.quickjs_time_ms ? 'Node.js' : 'QuickJS';
            }
            if (winner === 'Node.js') nodeWins++;
            else if (winner === 'QuickJS') quickjsWins++;
            else if (winner === 'protoJS') protojsWins++;
            console.log(`${(r.name || r.benchmark.replace('.js', ''))}`.padEnd(24) + n.padStart(14) + q.padStart(14) + p.padStart(14) + `  ${winner}`);
        });
        console.log('-'.repeat(72));
        console.log(`Wins: Node.js ${nodeWins}, QuickJS ${quickjsWins}${allThree.length > 0 ? ', protoJS ' + protojsWins : ''}`);

        if (nodeAndQuickjs.length > 1) {
            const geo = (arr) => {
                const prod = arr.reduce((a, b) => a * b, 1);
                return Math.pow(prod, 1 / arr.length);
            };
            const ratio = nodeAndQuickjs.map(r => r.quickjs_time_ms / Math.max(r.node_time_ms, 0.001));
            console.log(`Geometric mean QuickJS/Node: ${geo(ratio).toFixed(2)}x (QuickJS is ${geo(ratio).toFixed(2)}x slower than Node on this suite)`);
        }
        if (allThree.length > 0) {
            const geo = (arr) => Math.pow(arr.reduce((a, b) => a * b, 1), 1 / arr.length);
            const nodeOverProto = allThree.map(r => r.node_time_ms / Math.max(r.protojs_time_ms, 0.001));
            const quickjsOverProto = allThree.map(r => r.quickjs_time_ms / Math.max(r.protojs_time_ms, 0.001));
            console.log(`Geometric mean ratio (runtime/protoJS): Node ${geo(nodeOverProto).toFixed(2)}x, QuickJS ${geo(quickjsOverProto).toFixed(2)}x`);
        }
    }

    const missingProto = results.filter(r => r.protojs_time_ms == null && (r.node_time_ms != null || r.quickjs_time_ms != null));
    if (missingProto.length > 0) {
        console.log('\nNote: protoJS did not emit __BENCH_RESULT__ for some or all benchmarks (interpreter/host-call path). Node vs QuickJS comparison above is still valid.');
    }
    if (failed.length > 0) {
        console.log('\nFully failed (no result from any runtime):');
        failed.forEach(r => {
            console.log(`  ${r.benchmark}: node=${r.node_ok}, quickjs=${r.quickjs_ok}, protojs=${r.protojs_ok}`);
        });
    }
    const partialFail = results.filter(r => r.node_time_ms == null || r.quickjs_time_ms == null);
    if (partialFail.length > 0 && partialFail.some(r => r.node_time_ms == null || r.quickjs_time_ms == null)) {
        console.log('\nMissing Node or QuickJS result:');
        partialFail.forEach(r => {
            console.log(`  ${r.benchmark}: node=${r.node_ok}, quickjs=${r.quickjs_ok}, protojs=${r.protojs_ok}`);
            if (r.node_stderr) console.log(`    node stderr: ${r.node_stderr.slice(0, 150)}`);
            if (r.quickjs_stderr) console.log(`    quickjs stderr: ${r.quickjs_stderr.slice(0, 150)}`);
        });
    }

    const reportPath = path.join(RESULTS_DIR, 'node_quickjs_comparison.json');
    if (!fs.existsSync(RESULTS_DIR)) fs.mkdirSync(RESULTS_DIR, { recursive: true });
    const nodeVsQuickjs = results.filter(r => r.node_time_ms != null && r.quickjs_time_ms != null);
    const nodeWins = nodeVsQuickjs.filter(r => r.node_time_ms <= r.quickjs_time_ms).length;
    const quickjsWins = nodeVsQuickjs.filter(r => r.quickjs_time_ms < r.node_time_ms).length;
    fs.writeFileSync(reportPath, JSON.stringify({
        timestamp: new Date().toISOString(),
        results,
        summary: {
            total: results.length,
            with_all_three: allThree.length,
            node_and_quickjs: nodeAndQuickjs.length,
            node_wins: results.filter(r => r.fastest === 'Node.js').length,
            quickjs_wins: results.filter(r => r.fastest === 'QuickJS').length,
            protojs_wins: results.filter(r => r.fastest === 'protoJS').length,
            node_vs_quickjs_only: { node_wins: nodeWins, quickjs_wins: quickjsWins },
        },
    }, null, 2));
    console.log(`\nJSON report: ${reportPath}`);
}

async function main() {
    const protojsPath = findProtojs();
    const qjsPath = findQjs();

    if (!protojsPath) {
        console.error('protojs not found. Build with: cmake -B build -S . && cmake --build build');
        process.exit(1);
    }
    if (!qjsPath) {
        console.error('QuickJS qjs not found. Build: cd deps/quickjs && make qjs');
        process.exit(1);
    }

    console.log('Node.js vs QuickJS vs protoJS comparison');
    console.log('protojs:', protojsPath);
    console.log('qjs:', qjsPath);
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
            const parts = [];
            if (r.node_time_ms != null) parts.push(`Node ${r.node_time_ms.toFixed(2)} ms`);
            else parts.push('Node -');
            if (r.quickjs_time_ms != null) parts.push(`QJS ${r.quickjs_time_ms.toFixed(2)} ms`);
            else parts.push('QJS -');
            if (r.protojs_time_ms != null) parts.push(`protoJS ${r.protojs_time_ms.toFixed(2)} ms`);
            else parts.push('protoJS -');
            console.log(`  => ${parts.join(', ')}  Winner: ${r.fastest || 'n/a'}`);
        } catch (e) {
            console.error('  Error:', e.message);
            results.push({
                benchmark: f,
                node_time_ms: null,
                quickjs_time_ms: null,
                protojs_time_ms: null,
                fastest: null,
                node_ok: false,
                quickjs_ok: false,
                protojs_ok: false,
            });
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
