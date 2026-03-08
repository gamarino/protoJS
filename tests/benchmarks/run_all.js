// Main Benchmark Entry Point
// Runs all benchmark categories and generates HTML report

// Load benchmark runner first
// Note: In protoJS, we'll load files sequentially since module system may not be fully available

console.log('=== ProtoJS Performance Test Suite ===\n');
console.log('Loading benchmark framework...\n');

// We assume BenchmarkRunner, runBasicTypesBenchmarks, runCollectionsBenchmarks,
// runOverallPerformanceBenchmarks, and generateHTMLReport are available
// In a full implementation, these would be loaded via require() or import

const fs = typeof require !== 'undefined' ? require('fs') : null;

function getTimestamp() {
    const now = new Date();
    const year = now.getFullYear();
    const month = String(now.getMonth() + 1).padStart(2, '0');
    const day = String(now.getDate()).padStart(2, '0');
    const hours = String(now.getHours()).padStart(2, '0');
    const minutes = String(now.getMinutes()).padStart(2, '0');
    const seconds = String(now.getSeconds()).padStart(2, '0');
    return `${year}-${month}-${day}_${hours}-${minutes}-${seconds}`;
}

// Shared logic: run all categories and optionally write report. Used by both async and sync entry points.
function runAllBenchmarksCore(allResults, comparison, opts) {
    opts = opts || {};
    const timestamp = getTimestamp();
    const reportFilename = `results/report_${timestamp}.html`;
    const jsonFilename = `results/results_${timestamp}.json`;

    console.log('Generating HTML report...');
    const htmlReport = generateHTMLReport(allResults, comparison);

    if (fs) {
        fs.writeFileSync(reportFilename, htmlReport, 'utf8');
        console.log(`  Report saved: ${reportFilename}\n`);
    } else {
        console.log('\n=== Benchmark Summary ===');
        allResults.forEach(category => {
            console.log(`\n${category.category}:`);
            category.tests.forEach(test => {
                console.log(`  ${test.name}: ${test.mean.toFixed(3)}ms (mean), ${test.median.toFixed(3)}ms (median)`);
            });
        });
    }

    const jsonResults = {
        timestamp: new Date().toISOString(),
        results: allResults,
        comparison: comparison
    };
    if (fs) {
        fs.writeFileSync(jsonFilename, JSON.stringify(jsonResults, null, 2), 'utf8');
        console.log(`JSON results saved: ${jsonFilename}\n`);
    }

    console.log('=== Benchmark Suite Complete ===');
    console.log(`Total categories: ${allResults.length}`);
    console.log(`Total tests: ${allResults.reduce((sum, cat) => sum + (cat.tests ? cat.tests.length : 0), 0)}`);
    if (comparison) {
        console.log(`Compared tests: ${comparison.summary.compared}`);
    }
    return { reportFilename, jsonFilename };
}

// Synchronous entry point for environments (e.g. protoJS) where the event loop is not drained after eval.
function runAllBenchmarksSync() {
    const allResults = [];
    try {
        console.log('Running Basic Types benchmarks...');
        const basicTypesResults = runBasicTypesBenchmarks();
        allResults.push(basicTypesResults);
        console.log(`  Completed: ${basicTypesResults.tests.length} tests\n`);

        console.log('Running Collections benchmarks...');
        const collectionsResults = runCollectionsBenchmarks();
        allResults.push(collectionsResults);
        console.log(`  Completed: ${collectionsResults.tests.length} tests\n`);

        console.log('Running Overall Performance benchmarks...');
        const overallResults = runOverallPerformanceBenchmarks();
        allResults.push(overallResults);
        console.log(`  Completed: ${overallResults.tests.length} tests\n`);

        runAllBenchmarksCore(allResults, null);
        return { success: true };
    } catch (error) {
        console.error('Error running benchmarks:', error);
        return { success: false, error: error.message };
    }
}

async function runAllBenchmarks() {
    const allResults = [];
    let comparison = null;

    try {
        // Run Basic Types Benchmarks
        console.log('Running Basic Types benchmarks...');
        const basicTypesResults = runBasicTypesBenchmarks();
        allResults.push(basicTypesResults);
        console.log(`  Completed: ${basicTypesResults.tests.length} tests\n`);

        // Run Collections Benchmarks
        console.log('Running Collections benchmarks...');
        const collectionsResults = runCollectionsBenchmarks();
        allResults.push(collectionsResults);
        console.log(`  Completed: ${collectionsResults.tests.length} tests\n`);

        // Run Overall Performance Benchmarks
        console.log('Running Overall Performance benchmarks...');
        const overallResults = runOverallPerformanceBenchmarks();
        allResults.push(overallResults);
        console.log(`  Completed: ${overallResults.tests.length} tests\n`);

        // Optional: Run Node.js comparison
        if (typeof runNodeJSTests === 'function') {
            console.log('Running Node.js comparison...');
            const nodejsResults = await runNodeJSTests();
            if (nodejsResults) {
                comparison = calculateComparison(allResults, nodejsResults);
                console.log(`  Compared: ${comparison.summary.compared} tests\n`);
            }
        }

        const { reportFilename } = runAllBenchmarksCore(allResults, comparison);
        return {
            success: true,
            results: allResults,
            comparison: comparison,
            reportFile: reportFilename
        };
    } catch (error) {
        console.error('Error running benchmarks:', error);
        return {
            success: false,
            error: error.message
        };
    }
}

// Run benchmarks
if (typeof module !== 'undefined' && typeof module.exports !== 'undefined') {
    module.exports = runAllBenchmarks;
} else {
    // Direct execution: use sync path when Promise callbacks are not run after eval (e.g. protoJS)
    const useSync = (typeof __protojs__ !== 'undefined' && __protojs__) ||
        (typeof process === 'undefined' || typeof process.exit !== 'function');
    if (useSync) {
        const result = runAllBenchmarksSync();
        if (!result.success) {
            throw new Error(result.error || 'Benchmarks failed');
        }
    } else {
        runAllBenchmarks().then(result => {
            if (!result.success) {
                process.exit(1);
            }
        }).catch(error => {
            console.error('Fatal error:', error);
            process.exit(1);
        });
    }
}
