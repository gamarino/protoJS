#!/usr/bin/env node
// Node.js Comparison Benchmark Runner
// Runs benchmarks with both protoJS and Node.js and compares results

const { spawn, exec } = require('child_process');
const fs = require('fs');
const path = require('path');

// Benchmark files to run
const benchmarks = [
    'array_operations.js',
    'basic_types.js',
    'collections.js',
    'concurrent_operations.js',
    'overall_performance.js'
];

// Results storage
const results = [];

// Run a single benchmark with a runtime
function runBenchmark(benchmarkFile, runtime) {
    return new Promise((resolve, reject) => {
        const startTime = Date.now();
        const benchmarkPath = path.join(__dirname, benchmarkFile);
        
        let command;
        if (runtime === 'protojs') {
            // Try to find protojs in build directory or current directory
            const possiblePaths = [
                path.join(__dirname, '../../build/protojs'),
                path.join(__dirname, '../build/protojs'),
                './build/protojs',
                '../build/protojs',
                'protojs'
            ];
            
            let protojsPath = 'protojs';
            for (const possiblePath of possiblePaths) {
                const fullPath = path.isAbsolute(possiblePath) ? possiblePath : 
                                path.resolve(__dirname, possiblePath);
                if (fs.existsSync(fullPath)) {
                    protojsPath = fullPath;
                    break;
                }
            }
            
            command = `${protojsPath} ${benchmarkPath}`;
        } else {
            command = `node ${benchmarkPath}`;
        }
        
        exec(command, { cwd: __dirname }, (error, stdout, stderr) => {
            const endTime = Date.now();
            const duration = endTime - startTime;
            
            if (error) {
                reject({ runtime, benchmarkFile, error: error.message, duration: -1 });
                return;
            }
            
            resolve({
                runtime,
                benchmarkFile,
                duration,
                stdout: stdout.trim(),
                stderr: stderr.trim()
            });
        });
    });
}

// Run comparison for a single benchmark
async function compareBenchmark(benchmarkFile) {
    console.log(`\n📊 Running: ${benchmarkFile}`);
    console.log('─'.repeat(60));
    
    try {
        // Run with protoJS
        console.log('  Running with protoJS...');
        const protojsResult = await runBenchmark(benchmarkFile, 'protojs');
        
        // Run with Node.js
        console.log('  Running with Node.js...');
        const nodejsResult = await runBenchmark(benchmarkFile, 'node');
        
        // Calculate speedup
        const speedup = nodejsResult.duration / protojsResult.duration;
        const faster = speedup > 1 ? 'protoJS' : 'Node.js';
        const speedupFactor = speedup > 1 ? speedup : 1 / speedup;
        
        const result = {
            benchmark: benchmarkFile,
            protojs_time_ms: protojsResult.duration,
            nodejs_time_ms: nodejsResult.duration,
            speedup: speedupFactor,
            faster: faster,
            protojs_output: protojsResult.stdout,
            nodejs_output: nodejsResult.stdout
        };
        
        results.push(result);
        
        // Print results
        console.log(`  ✅ protoJS:  ${protojsResult.duration} ms`);
        console.log(`  ✅ Node.js:  ${nodejsResult.duration} ms`);
        console.log(`  📈 ${faster} is ${speedupFactor.toFixed(2)}x faster`);
        
        return result;
    } catch (error) {
        console.error(`  ❌ Error: ${error.error || error.message}`);
        results.push({
            benchmark: benchmarkFile,
            error: error.error || error.message,
            protojs_time_ms: -1,
            nodejs_time_ms: -1
        });
        return null;
    }
}

// Generate report
function generateReport() {
    console.log('\n' + '='.repeat(60));
    console.log('📊 BENCHMARK COMPARISON REPORT');
    console.log('='.repeat(60));
    
    const successful = results.filter(r => !r.error && r.protojs_time_ms > 0 && r.nodejs_time_ms > 0);
    const failed = results.filter(r => r.error || r.protojs_time_ms < 0 || r.nodejs_time_ms < 0);
    
    if (successful.length > 0) {
        console.log('\n✅ Successful Benchmarks:');
        console.log('─'.repeat(60));
        
        let protojsWins = 0;
        let nodejsWins = 0;
        let totalProtojsTime = 0;
        let totalNodejsTime = 0;
        
        successful.forEach(result => {
            const protojsTime = result.protojs_time_ms;
            const nodejsTime = result.nodejs_time_ms;
            const speedup = result.speedup;
            const faster = result.faster;
            
            totalProtojsTime += protojsTime;
            totalNodejsTime += nodejsTime;
            
            if (faster === 'protoJS') {
                protojsWins++;
            } else {
                nodejsWins++;
            }
            
            console.log(`\n${result.benchmark}:`);
            console.log(`  protoJS:  ${protojsTime.toFixed(2)} ms`);
            console.log(`  Node.js:  ${nodejsTime.toFixed(2)} ms`);
            console.log(`  Speedup:  ${speedup.toFixed(2)}x (${faster} faster)`);
        });
        
        console.log('\n' + '─'.repeat(60));
        console.log('📈 Summary:');
        console.log(`  Total Benchmarks: ${results.length}`);
        console.log(`  Successful: ${successful.length}`);
        console.log(`  Failed: ${failed.length}`);
        console.log(`  protoJS Wins: ${protojsWins}`);
        console.log(`  Node.js Wins: ${nodejsWins}`);
        console.log(`  Average protoJS Time: ${(totalProtojsTime / successful.length).toFixed(2)} ms`);
        console.log(`  Average Node.js Time: ${(totalNodejsTime / successful.length).toFixed(2)} ms`);
        console.log(`  Overall Speedup: ${(totalNodejsTime / totalProtojsTime).toFixed(2)}x`);
    }
    
    if (failed.length > 0) {
        console.log('\n❌ Failed Benchmarks:');
        console.log('─'.repeat(60));
        failed.forEach(result => {
            console.log(`  ${result.benchmark}: ${result.error || 'Execution failed'}`);
        });
    }
    
    // Generate JSON report
    const reportPath = path.join(__dirname, 'results', 'nodejs_comparison.json');
    const reportDir = path.dirname(reportPath);
    if (!fs.existsSync(reportDir)) {
        fs.mkdirSync(reportDir, { recursive: true });
    }
    
    const jsonReport = {
        timestamp: new Date().toISOString(),
        total_benchmarks: results.length,
        successful: successful.length,
        failed: failed.length,
        results: results
    };
    
    fs.writeFileSync(reportPath, JSON.stringify(jsonReport, null, 2));
    console.log(`\n💾 JSON report saved to: ${reportPath}`);
    
    // Generate HTML report
    generateHTMLReport(reportPath.replace('.json', '.html'));
}

// Generate HTML report
function generateHTMLReport(outputPath) {
    const successful = results.filter(r => !r.error && r.protojs_time_ms > 0 && r.nodejs_time_ms > 0);
    
    let html = `<!DOCTYPE html>
<html>
<head>
    <title>Node.js Comparison Benchmark Report</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }
        .container { max-width: 1200px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        h1 { color: #333; border-bottom: 3px solid #4CAF50; padding-bottom: 10px; }
        table { width: 100%; border-collapse: collapse; margin: 20px 0; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }
        th { background-color: #4CAF50; color: white; font-weight: bold; }
        tr:hover { background-color: #f5f5f5; }
        .protojs-win { color: #4CAF50; font-weight: bold; }
        .nodejs-win { color: #2196F3; font-weight: bold; }
        .summary { background: #e8f5e9; padding: 15px; border-radius: 5px; margin: 20px 0; }
        .error { color: #f44336; }
    </style>
</head>
<body>
    <div class="container">
        <h1>📊 Node.js Comparison Benchmark Report</h1>
        <p><strong>Generated:</strong> ${new Date().toLocaleString()}</p>
        
        <div class="summary">
            <h2>Summary</h2>
            <p><strong>Total Benchmarks:</strong> ${results.length}</p>
            <p><strong>Successful:</strong> ${successful.length}</p>
            <p><strong>Failed:</strong> ${results.length - successful.length}</p>
        </div>
        
        <h2>Benchmark Results</h2>
        <table>
            <thead>
                <tr>
                    <th>Benchmark</th>
                    <th>protoJS (ms)</th>
                    <th>Node.js (ms)</th>
                    <th>Speedup</th>
                    <th>Winner</th>
                    <th>Status</th>
                </tr>
            </thead>
            <tbody>`;
    
    results.forEach(result => {
        if (result.error || result.protojs_time_ms < 0) {
            html += `
                <tr class="error">
                    <td>${result.benchmark}</td>
                    <td colspan="5">Error: ${result.error || 'Execution failed'}</td>
                </tr>`;
        } else {
            const speedup = result.speedup;
            const faster = result.faster;
            const winnerClass = faster === 'protoJS' ? 'protojs-win' : 'nodejs-win';
            
            html += `
                <tr>
                    <td><strong>${result.benchmark}</strong></td>
                    <td>${result.protojs_time_ms.toFixed(2)}</td>
                    <td>${result.nodejs_time_ms.toFixed(2)}</td>
                    <td>${speedup.toFixed(2)}x</td>
                    <td class="${winnerClass}">${faster}</td>
                    <td>✅</td>
                </tr>`;
        }
    });
    
    html += `
            </tbody>
        </table>
    </div>
</body>
</html>`;
    
    fs.writeFileSync(outputPath, html);
    console.log(`💾 HTML report saved to: ${outputPath}`);
}

// Main execution
async function main() {
    console.log('🚀 Starting Node.js Comparison Benchmarks');
    console.log('='.repeat(60));
    
    // Check if protojs exists
    const possiblePaths = [
        path.join(__dirname, '../../build/protojs'),
        path.join(__dirname, '../build/protojs'),
        './build/protojs',
        '../build/protojs',
        'protojs'
    ];
    
    let protojsPath = null;
    for (const possiblePath of possiblePaths) {
        const fullPath = path.isAbsolute(possiblePath) ? possiblePath : 
                        path.resolve(__dirname, possiblePath);
        if (fs.existsSync(fullPath)) {
            protojsPath = fullPath;
            break;
        }
    }
    
    if (!protojsPath) {
        console.error('❌ Error: protojs executable not found. Please build the project first.');
        console.error('   Searched in:', possiblePaths);
        process.exit(1);
    }
    
    console.log(`✅ Found protojs at: ${protojsPath}`);
    
    // Run all benchmarks
    for (const benchmark of benchmarks) {
        const benchmarkPath = path.join(__dirname, benchmark);
        if (fs.existsSync(benchmarkPath)) {
            await compareBenchmark(benchmark);
        } else {
            console.log(`⚠️  Skipping ${benchmark} (file not found)`);
        }
    }
    
    // Generate report
    generateReport();
    
    console.log('\n✅ Benchmark comparison complete!');
}

// Run if executed directly
if (require.main === module) {
    main().catch(error => {
        console.error('❌ Fatal error:', error);
        process.exit(1);
    });
}

module.exports = { compareBenchmark, generateReport };
