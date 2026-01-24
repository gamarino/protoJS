// Combined Performance Test Suite Loader
// Loads all benchmark files in correct order and runs the suite

// This script combines all benchmark files for execution in protoJS
// Since protoJS may not have full module support, we'll concatenate files

console.log('=== ProtoJS Performance Test Suite ===\n');
console.log('Loading all benchmark modules...\n');

// The actual implementation would load files in this order:
// 1. benchmark_runner.js (BenchmarkRunner class)
// 2. basic_types.js (runBasicTypesBenchmarks function)
// 3. collections.js (runCollectionsBenchmarks function)
// 4. overall_performance.js (runOverallPerformanceBenchmarks function)
// 5. compare_engines.js (calculateComparison, runNodeJSTests functions)
// 6. report_generator.js (generateHTMLReport and related functions)
// 7. run_all.js (main entry point)

// For protoJS execution, we need to load these files in sequence
// This is a placeholder - in practice, you would:
// - Use a file concatenation tool
// - Or implement a simple module loader
// - Or use the protoJS module system when available

console.log('Note: This script requires all benchmark files to be loaded.');
console.log('In a full implementation, files would be loaded via:');
console.log('  - File system reads (if available)');
console.log('  - Module system (when implemented)');
console.log('  - Or concatenated into a single file\n');

// For now, we'll assume all functions are available
// In practice, you would concatenate all files or use a loader

try {
    // Check if required functions/classes are available
    if (typeof BenchmarkRunner === 'undefined') {
        throw new Error('BenchmarkRunner class not found. Please load benchmark_runner.js first.');
    }
    if (typeof runBasicTypesBenchmarks === 'undefined') {
        throw new Error('runBasicTypesBenchmarks function not found. Please load basic_types.js.');
    }
    if (typeof runCollectionsBenchmarks === 'undefined') {
        throw new Error('runCollectionsBenchmarks function not found. Please load collections.js.');
    }
    if (typeof runOverallPerformanceBenchmarks === 'undefined') {
        throw new Error('runOverallPerformanceBenchmarks function not found. Please load overall_performance.js.');
    }
    if (typeof generateHTMLReport === 'undefined') {
        throw new Error('generateHTMLReport function not found. Please load report_generator.js.');
    }
    
    console.log('All required modules loaded successfully.\n');
    
    // Now run the benchmarks
    if (typeof runAllBenchmarks === 'function') {
        runAllBenchmarks();
    } else {
        // Fallback: run benchmarks directly
        console.log('Running benchmarks directly...\n');
        
        const allResults = [];
        
        // Run Basic Types
        console.log('Running Basic Types benchmarks...');
        const basicTypesResults = runBasicTypesBenchmarks();
        allResults.push(basicTypesResults);
        console.log(`  Completed: ${basicTypesResults.tests.length} tests\n`);
        
        // Run Collections
        console.log('Running Collections benchmarks...');
        const collectionsResults = runCollectionsBenchmarks();
        allResults.push(collectionsResults);
        console.log(`  Completed: ${collectionsResults.tests.length} tests\n`);
        
        // Run Overall Performance
        console.log('Running Overall Performance benchmarks...');
        const overallResults = runOverallPerformanceBenchmarks();
        allResults.push(overallResults);
        console.log(`  Completed: ${overallResults.tests.length} tests\n`);
        
        // Generate report
        console.log('Generating HTML report...');
        const htmlReport = generateHTMLReport(allResults, null);
        console.log(`  Report generated: ${htmlReport.length} characters\n`);
        
        // Output summary
        console.log('=== Benchmark Summary ===');
        allResults.forEach(category => {
            console.log(`\n${category.category}:`);
            category.tests.forEach(test => {
                console.log(`  ${test.name}: ${test.mean.toFixed(3)}ms (mean)`);
            });
        });
        
        console.log('\n=== Performance Test Suite Complete ===');
    }
    
} catch (error) {
    console.error('Error:', error.message);
    console.error('\nPlease ensure all benchmark files are loaded in the correct order:');
    console.error('  1. benchmark_runner.js');
    console.error('  2. basic_types.js');
    console.error('  3. collections.js');
    console.error('  4. overall_performance.js');
    console.error('  5. compare_engines.js');
    console.error('  6. report_generator.js');
    console.error('  7. run_all.js (or use this loader)');
}
