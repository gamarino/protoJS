// Minimal Performance Test - Very fast execution
console.log('=== ProtoJS Minimal Performance Test ===\n');

// Create minimal benchmark runner with very few iterations
class MinimalBenchmarkRunner {
    constructor(name) {
        this.name = name;
    }
    
    static getTimer() {
        return () => Date.now();
    }
    
    run(fn) {
        const timer = MinimalBenchmarkRunner.getTimer();
        const times = [];
        
        // Just 3 iterations for speed
        for (let i = 0; i < 3; i++) {
            const start = timer();
            fn();
            const end = timer();
            times.push(end - start);
        }
        
        const sum = times.reduce((a, b) => a + b, 0);
        const mean = sum / times.length;
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
    }
}

// Run a few quick tests
const results = {
    category: 'Quick Validation',
    tests: []
};

console.log('Running minimal benchmarks...\n');

// Simple addition
const addTest = new MinimalBenchmarkRunner('Number Addition');
results.tests.push(addTest.run(() => {
    let sum = 0;
    for (let i = 0; i < 10000; i++) {
        sum += i;
    }
}));

// String concat
const strTest = new MinimalBenchmarkRunner('String Concatenation');
results.tests.push(strTest.run(() => {
    let str = '';
    for (let i = 0; i < 1000; i++) {
        str += 'test';
    }
}));

// Array creation
const arrTest = new MinimalBenchmarkRunner('Array Creation');
results.tests.push(arrTest.run(() => {
    const arr = Array.from({length: 1000}, (_, idx) => idx);
}));

console.log('Results:');
results.tests.forEach(test => {
    console.log(`  ${test.name}: ${test.mean.toFixed(2)}ms (mean)`);
});

console.log('\n=== Minimal Test Complete ===');
