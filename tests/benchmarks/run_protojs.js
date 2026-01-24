// ProtoJS-Compatible Benchmark Runner
// Runs benchmarks and outputs results (no file system dependencies)

console.log('=== ProtoJS Performance Test Suite ===\n');

// Run all benchmarks
const allResults = [];

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
    
    // Generate HTML Report
    console.log('Generating HTML report...');
    const htmlReport = generateHTMLReport(allResults, null);
    
    // Output summary
    console.log('\n=== Benchmark Summary ===');
    allResults.forEach(category => {
        console.log(`\n${category.category}:`);
        category.tests.forEach(test => {
            console.log(`  ${test.name}: ${test.mean.toFixed(3)}ms (mean), ${test.median.toFixed(3)}ms (median)`);
        });
    });
    
    // Store report in global for extraction
    if (typeof globalThis !== 'undefined') {
        globalThis.performanceReport = htmlReport;
        globalThis.performanceResults = allResults;
    }
    
    console.log('\n=== Performance Test Suite Complete ===');
    console.log(`Total categories: ${allResults.length}`);
    console.log(`Total tests: ${allResults.reduce((sum, cat) => sum + (cat.tests ? cat.tests.length : 0), 0)}`);
    console.log('\nHTML report generated (stored in globalThis.performanceReport)');
    
} catch (error) {
    console.error('Error running benchmarks:', error);
    console.error(error.stack);
}
