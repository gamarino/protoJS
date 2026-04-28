/**
 * Minimal Benchmark Runner for testing ProtoJS performance
 */

// Create minimal benchmark runner with very few iterations
function MinimalBenchmarkRunner(name) {
    this.name = name;
}
    
MinimalBenchmarkRunner.getTimer = function() {
    return () => Date.now();
};

MinimalBenchmarkRunner.prototype.run = function(fn) {
    const times = [];
    
    // Just 3 iterations for speed
    for (let i = 0; i < 3; i++) {
        const start = Date.now();
        fn();
        const end = Date.now();
        times.push(end - start);
    }
    
    let sum = 0;
    const tlen = times.length;
    for (let j = 0; j < tlen; j++) {
        sum += times[j];
    }
    const mean = sum / tlen;
    const sorted = [...times].sort((a, b) => a - b);
    const median = sorted[Math.floor(sorted.length / 2)];
    
    return {
        name: this.name,
        mean: mean,
        median: median,
        min: sorted[0],
        max: sorted[sorted.length - 1],
        iterations: 3
    };
};

// Run a few quick tests
function runMinimalBenchmarks() {
    const results = {
        category: 'Minimal Benchmarks',
        tests: []
    };

    console.log('Running minimal benchmarks...\n');

    // Number Addition
    const addTest = new MinimalBenchmarkRunner('Number Addition');
    results.tests.push(addTest.run(() => {
        let sum = 0;
        for (let i = 0; i < 100000; i++) {
            sum += i;
        }
    }));

    // String Concatenation
    const concatTest = new MinimalBenchmarkRunner('String Concatenation');
    results.tests.push(concatTest.run(() => {
        let str = '';
        for (let i = 0; i < 1000; i++) {
            str += 'test' + i;
        }
    }));

    // Array Creation
    const arrayTest = new MinimalBenchmarkRunner('Array Creation');
    results.tests.push(arrayTest.run(() => {
        const arr = [];
        for (let i = 0; i < 1000; i++) {
            arr.push(i);
        }
    }));

    return results;
}

console.log('=== ProtoJS Minimal Performance Test ===\n');
const results = runMinimalBenchmarks();

console.log('Results:');
results.tests.forEach(test => {
    console.log(`  ${test.name}: ${test.mean.toFixed(2)}ms (mean)`);
});

console.log('\n=== Minimal Test Complete ===');
