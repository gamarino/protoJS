// Overall Performance Benchmarks
// Measures startup time, memory usage, throughput, and GC performance

function runOverallPerformanceBenchmarks() {
    const results = {
        category: 'Overall Performance',
        tests: []
    };

    // Startup Time (approximate - time to execute first meaningful operation)
    console.log('Running Overall Performance benchmarks...');
    
    const startupBench = new BenchmarkRunner('Startup Time', 10, 1);
    results.tests.push(startupBench.run(() => {
        // Simulate startup by creating context and running simple operation
        const test = 1 + 1;
        const arr = [1, 2, 3];
        const obj = {test: 'value'};
    }));

    // Throughput - Simple Operations Per Second
    const throughputBench = new BenchmarkRunner('Throughput (Simple Ops)', 10, 1);
    const throughputStats = throughputBench.runWithWork((iterations) => {
        let sum = 0;
        for (let i = 0; i < iterations; i++) {
            sum += i;
        }
    }, 1000000);
    results.tests.push(throughputStats);

    // Memory - Array Creation
    const memoryArrayBench = new BenchmarkRunner('Memory: Array Creation', 10, 1);
    results.tests.push(memoryArrayBench.run(() => {
        const arr = Array.from({length: 100000}, (_, idx) => idx);
    }));

    // Memory - Object Creation
    const memoryObjBench = new BenchmarkRunner('Memory: Object Creation', 10, 1);
    results.tests.push(memoryObjBench.run(() => {
        const obj = {};
        for (let i = 0; i < 100000; i++) {
            obj['key' + i] = i;
        }
    }));

    // Memory - String Creation
    const memoryStringBench = new BenchmarkRunner('Memory: String Creation', 10, 1);
    results.tests.push(memoryStringBench.run(() => {
        let str = '';
        for (let i = 0; i < 10000; i++) {
            str += 'test' + i;
        }
    }));

    // Function Call Overhead
    const funcCallBench = new BenchmarkRunner('Function Call Overhead', 100, 10);
    function testFunc(x) {
        return x + 1;
    }
    results.tests.push(funcCallBench.run(() => {
        for (let i = 0; i < 1000000; i++) {
            testFunc(i);
        }
    }));

    // Closure Creation
    const closureBench = new BenchmarkRunner('Closure Creation', 50, 5);
    results.tests.push(closureBench.run(() => {
        for (let i = 0; i < 10000; i++) {
            const closure = (function(x) {
                return function(y) {
                    return x + y;
                };
            })(i);
        }
    }));

    // Property Access Chain
    const propChainBench = new BenchmarkRunner('Property Access Chain', 100, 10);
    const testObj = {
        level1: {
            level2: {
                level3: {
                    value: 42
                }
            }
        }
    };
    results.tests.push(propChainBench.run(() => {
        for (let i = 0; i < 1000000; i++) {
            const val = testObj.level1.level2.level3.value;
        }
    }));

    // Try-Catch Overhead
    const tryCatchBench = new BenchmarkRunner('Try-Catch Overhead', 50, 5);
    results.tests.push(tryCatchBench.run(() => {
        for (let i = 0; i < 100000; i++) {
            try {
                const val = i * 2;
            } catch (e) {
                // Never executed
            }
        }
    }));

    // Type Checking
    const typeCheckBench = new BenchmarkRunner('Type Checking', 100, 10);
    const testValues = [1, 'string', true, null, undefined, {}, []];
    results.tests.push(typeCheckBench.run(() => {
        for (let i = 0; i < 1000000; i++) {
            const val = testValues[i % testValues.length];
            const isNumber = typeof val === 'number';
            const isString = typeof val === 'string';
            const isObject = typeof val === 'object';
        }
    }));

    return results;
}

// Export for use in main benchmark runner
if (typeof module !== 'undefined' && typeof module.exports !== 'undefined') {
    module.exports = runOverallPerformanceBenchmarks;
}
