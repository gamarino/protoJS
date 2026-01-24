// Quick Performance Test (reduced iterations for faster execution)
// For documentation and quick validation

console.log('=== ProtoJS Quick Performance Test ===\n');

// Override BenchmarkRunner to use fewer iterations
class QuickBenchmarkRunner extends BenchmarkRunner {
    constructor(name, iterations = 10, warmup = 2) {
        super(name, iterations, warmup);
    }
}

// Temporarily replace BenchmarkRunner
const OriginalBenchmarkRunner = BenchmarkRunner;
BenchmarkRunner = QuickBenchmarkRunner;

const allResults = [];

try {
    console.log('Running Quick Basic Types benchmarks (10 iterations)...');
    const basicTypesResults = runBasicTypesBenchmarks();
    allResults.push(basicTypesResults);
    console.log(`  Completed: ${basicTypesResults.tests.length} tests\n`);
    
    console.log('Running Quick Collections benchmarks (10 iterations)...');
    const collectionsResults = runCollectionsBenchmarks();
    allResults.push(collectionsResults);
    console.log(`  Completed: ${collectionsResults.tests.length} tests\n`);
    
    console.log('Running Quick Overall Performance benchmarks (10 iterations)...');
    const overallResults = runOverallPerformanceBenchmarks();
    allResults.push(overallResults);
    console.log(`  Completed: ${overallResults.tests.length} tests\n`);
    
    console.log('Generating HTML report...');
    const htmlReport = generateHTMLReport(allResults, null);
    
    // Store for extraction
    if (typeof globalThis !== 'undefined') {
        globalThis.performanceReport = htmlReport;
        globalThis.performanceResults = allResults;
    }
    
    console.log('\n=== Quick Test Complete ===');
    console.log(`Total tests: ${allResults.reduce((sum, cat) => sum + (cat.tests ? cat.tests.length : 0), 0)}`);
    console.log('Report generated and stored in globalThis.performanceReport');
    
} catch (error) {
    console.error('Error:', error);
    console.error(error.stack);
}
