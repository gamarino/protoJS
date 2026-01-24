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
        // This would spawn Node.js and run the same benchmarks
        // For now, we skip it as it requires Node.js to be available
        if (typeof runNodeJSTests === 'function') {
            console.log('Running Node.js comparison...');
            const nodejsResults = await runNodeJSTests();
            if (nodejsResults) {
                comparison = calculateComparison(allResults, nodejsResults);
                console.log(`  Compared: ${comparison.summary.compared} tests\n`);
            }
        }
        
        // Generate HTML Report
        console.log('Generating HTML report...');
        const htmlReport = generateHTMLReport(allResults, comparison);
        
        // Save report
        const timestamp = getTimestamp();
        const reportFilename = `results/report_${timestamp}.html`;
        
        if (fs) {
            // Node.js environment
            fs.writeFileSync(reportFilename, htmlReport, 'utf8');
            console.log(`  Report saved: ${reportFilename}\n`);
        } else {
            // protoJS environment - output to console or use a different method
            console.log('\n=== HTML Report ===');
            console.log('(In protoJS, report would be written to file system)');
            console.log(`Report length: ${htmlReport.length} characters\n`);
            
            // For protoJS, we might need to use a different approach
            // For now, output a summary
            console.log('=== Benchmark Summary ===');
            allResults.forEach(category => {
                console.log(`\n${category.category}:`);
                category.tests.forEach(test => {
                    console.log(`  ${test.name}: ${test.mean.toFixed(3)}ms (mean), ${test.median.toFixed(3)}ms (median)`);
                });
            });
        }
        
        // Also save JSON results
        const jsonResults = {
            timestamp: new Date().toISOString(),
            results: allResults,
            comparison: comparison
        };
        
        const jsonFilename = `results/results_${timestamp}.json`;
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
    // Direct execution
    runAllBenchmarks().then(result => {
        if (!result.success) {
            process.exit(1);
        }
    }).catch(error => {
        console.error('Fatal error:', error);
        process.exit(1);
    });
}
