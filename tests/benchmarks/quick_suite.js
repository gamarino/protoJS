// Quick Performance Test Suite (reduced iterations)
// Benchmark Runner Framework
// Provides reusable benchmarking infrastructure with statistical analysis

class BenchmarkRunner {
    constructor(name, iterations = 100, warmup = 10) {
        this.name = name;
        this.iterations = iterations;
        this.warmup = warmup;
        this.times = [];
        this.memoryBefore = null;
        this.memoryAfter = null;
    }

    // Get high-resolution timer
    static getTimer() {
        if (typeof performance !== 'undefined' && typeof performance.now === 'function') {
            return () => performance.now();
        } else if (typeof process !== 'undefined' && typeof process.hrtime === 'function') {
            // Node.js hrtime
            return () => {
                const [sec, nanosec] = process.hrtime();
                return sec * 1000 + nanosec / 1000000;
            };
        } else {
            // Fallback to Date.now() (millisecond precision)
            return () => Date.now();
        }
    }

    // Get memory usage if available
    static getMemoryUsage() {
        if (typeof process !== 'undefined' && typeof process.memoryUsage === 'function') {
            const usage = process.memoryUsage();
            return {
                heapUsed: usage.heapUsed,
                heapTotal: usage.heapTotal,
                external: usage.external || 0,
                rss: usage.rss || 0
            };
        }
        return null;
    }

    // Run benchmark with warmup and multiple iterations
    run(fn) {
        this.times = [];
        const timer = BenchmarkRunner.getTimer();
        
        // Warmup phase
        for (let i = 0; i < this.warmup; i++) {
            fn();
        }

        // Memory measurement before
        this.memoryBefore = BenchmarkRunner.getMemoryUsage();

        // Actual measurements
        for (let i = 0; i < this.iterations; i++) {
            const start = timer();
            fn();
            const end = timer();
            this.times.push(end - start);
        }

        // Memory measurement after
        this.memoryAfter = BenchmarkRunner.getMemoryUsage();

        return this.getStats();
    }

    // Calculate statistical measures
    getStats() {
        if (this.times.length === 0) {
            return {
                name: this.name,
                iterations: 0,
                min: 0,
                max: 0,
                mean: 0,
                median: 0,
                stddev: 0,
                total: 0,
                memory: null
            };
        }

        const sorted = [...this.times].sort((a, b) => a - b);
        const sum = this.times.reduce((a, b) => a + b, 0);
        const mean = sum / this.times.length;
        
        // Calculate standard deviation
        const variance = this.times.reduce((acc, time) => acc + Math.pow(time - mean, 2), 0) / this.times.length;
        const stddev = Math.sqrt(variance);
        
        // Calculate median
        const mid = Math.floor(sorted.length / 2);
        const median = sorted.length % 2 === 0
            ? (sorted[mid - 1] + sorted[mid]) / 2
            : sorted[mid];

        // Memory delta
        let memory = null;
        if (this.memoryBefore && this.memoryAfter) {
            memory = {
                before: this.memoryBefore,
                after: this.memoryAfter,
                delta: {
                    heapUsed: this.memoryAfter.heapUsed - this.memoryBefore.heapUsed,
                    heapTotal: this.memoryAfter.heapTotal - this.memoryBefore.heapTotal,
                    external: this.memoryAfter.external - this.memoryBefore.external,
                    rss: this.memoryAfter.rss - this.memoryBefore.rss
                }
            };
        }

        return {
            name: this.name,
            iterations: this.iterations,
            min: sorted[0],
            max: sorted[sorted.length - 1],
            mean: mean,
            median: median,
            stddev: stddev,
            total: sum,
            memory: memory,
            times: this.times
        };
    }

    // Run benchmark with custom work per iteration
    runWithWork(fn, workPerIteration = 1) {
        this.times = [];
        const timer = BenchmarkRunner.getTimer();
        
        // Warmup
        for (let i = 0; i < this.warmup; i++) {
            fn(workPerIteration);
        }

        this.memoryBefore = BenchmarkRunner.getMemoryUsage();

        // Measurements
        for (let i = 0; i < this.iterations; i++) {
            const start = timer();
            fn(workPerIteration);
            const end = timer();
            this.times.push(end - start);
        }

        this.memoryAfter = BenchmarkRunner.getMemoryUsage();

        const stats = this.getStats();
        // Calculate operations per second
        stats.opsPerSecond = (workPerIteration * this.iterations) / (stats.total / 1000);
        return stats;
    }
}

// Export for use in other benchmark files
if (typeof module !== 'undefined' && typeof module.exports !== 'undefined') {
    module.exports = BenchmarkRunner;
}
// Basic Types Performance Benchmarks
// Tests performance of primitive JavaScript types

function runBasicTypesBenchmarks() {
    const results = {
        category: 'Basic Types',
        tests: []
    };

    // Number Operations
    console.log('Running Number benchmarks...');
    
    // Number Addition
    const addBench = new BenchmarkRunner('Number Addition', 100, 10);
    results.tests.push(addBench.run(() => {
        let sum = 0;
        for (let i = 0; i < 1000000; i++) {
            sum += i;
        }
    }));

    // Number Multiplication
    const mulBench = new BenchmarkRunner('Number Multiplication', 100, 10);
    results.tests.push(mulBench.run(() => {
        let product = 1;
        for (let i = 1; i < 100000; i++) {
            product *= 1.0001;
        }
    }));

    // Number Division
    const divBench = new BenchmarkRunner('Number Division', 100, 10);
    results.tests.push(divBench.run(() => {
        let quotient = 1000000;
        for (let i = 1; i < 100000; i++) {
            quotient /= 1.0001;
        }
    }));

    // Number Type Coercion (Number to String)
    const numToStrBench = new BenchmarkRunner('Number to String Coercion', 100, 10);
    results.tests.push(numToStrBench.run(() => {
        for (let i = 0; i < 100000; i++) {
            const str = String(i);
        }
    }));

    // Number Type Coercion (String to Number)
    const strToNumBench = new BenchmarkRunner('String to Number Coercion', 100, 10);
    results.tests.push(strToNumBench.run(() => {
        for (let i = 0; i < 100000; i++) {
            const num = Number(String(i));
        }
    }));

    // String Operations
    console.log('Running String benchmarks...');

    // String Concatenation
    const concatBench = new BenchmarkRunner('String Concatenation', 50, 5);
    results.tests.push(concatBench.run(() => {
        let str = '';
        for (let i = 0; i < 10000; i++) {
            str += 'test' + i;
        }
    }));

    // String Substring
    const substrBench = new BenchmarkRunner('String Substring', 100, 10);
    const testString = 'a'.repeat(10000);
    results.tests.push(substrBench.run(() => {
        for (let i = 0; i < 1000; i++) {
            const sub = testString.substring(i, i + 100);
        }
    }));

    // String Length Access
    const lengthBench = new BenchmarkRunner('String Length Access', 100, 10);
    const lengthTestStr = 'test string for length access';
    results.tests.push(lengthBench.run(() => {
        for (let i = 0; i < 1000000; i++) {
            const len = lengthTestStr.length;
        }
    }));

    // String Index Access
    const indexBench = new BenchmarkRunner('String Index Access', 100, 10);
    const indexTestStr = 'abcdefghijklmnopqrstuvwxyz';
    results.tests.push(indexBench.run(() => {
        for (let i = 0; i < 1000000; i++) {
            const char = indexTestStr[i % indexTestStr.length];
        }
    }));

    // Boolean Operations
    console.log('Running Boolean benchmarks...');

    // Boolean AND
    const andBench = new BenchmarkRunner('Boolean AND', 100, 10);
    results.tests.push(andBench.run(() => {
        for (let i = 0; i < 1000000; i++) {
            const result = (i % 2 === 0) && (i % 3 === 0);
        }
    }));

    // Boolean OR
    const orBench = new BenchmarkRunner('Boolean OR', 100, 10);
    results.tests.push(orBench.run(() => {
        for (let i = 0; i < 1000000; i++) {
            const result = (i % 2 === 0) || (i % 3 === 0);
        }
    }));

    // Boolean NOT
    const notBench = new BenchmarkRunner('Boolean NOT', 100, 10);
    results.tests.push(notBench.run(() => {
        for (let i = 0; i < 1000000; i++) {
            const result = !(i % 2 === 0);
        }
    }));

    // Null/Undefined Operations
    console.log('Running Null/Undefined benchmarks...');

    // Null Check
    const nullCheckBench = new BenchmarkRunner('Null Check', 100, 10);
    results.tests.push(nullCheckBench.run(() => {
        for (let i = 0; i < 1000000; i++) {
            const isNull = (null === null);
        }
    }));

    // Undefined Check
    const undefinedCheckBench = new BenchmarkRunner('Undefined Check', 100, 10);
    results.tests.push(undefinedCheckBench.run(() => {
        for (let i = 0; i < 1000000; i++) {
            const isUndef = (undefined === undefined);
        }
    }));

    // BigInt Operations (if supported)
    console.log('Running BigInt benchmarks...');
    
    try {
        // BigInt Addition
        const bigIntAddBench = new BenchmarkRunner('BigInt Addition', 100, 10);
        results.tests.push(bigIntAddBench.run(() => {
            let sum = BigInt(0);
            for (let i = 0; i < 100000; i++) {
                sum += BigInt(i);
            }
        }));

        // BigInt Multiplication
        const bigIntMulBench = new BenchmarkRunner('BigInt Multiplication', 100, 10);
        results.tests.push(bigIntMulBench.run(() => {
            let product = BigInt(1);
            for (let i = 1; i < 10000; i++) {
                product *= BigInt(i);
            }
        }));

        // BigInt to Number Conversion
        const bigIntToNumBench = new BenchmarkRunner('BigInt to Number', 100, 10);
        results.tests.push(bigIntToNumBench.run(() => {
            for (let i = 0; i < 100000; i++) {
                const num = Number(BigInt(i));
            }
        }));
    } catch (e) {
        console.log('BigInt not supported, skipping BigInt benchmarks');
    }

    return results;
}

// Export for use in main benchmark runner
if (typeof module !== 'undefined' && typeof module.exports !== 'undefined') {
    module.exports = runBasicTypesBenchmarks;
}
// Collections Performance Benchmarks
// Tests performance of all collection types (Arrays, Objects, protoCore collections)

function runCollectionsBenchmarks() {
    const results = {
        category: 'Collections',
        tests: []
    };

    // Array Operations
    console.log('Running Array benchmarks...');

    // Array Creation
    const arrayCreateBench = new BenchmarkRunner('Array Creation', 50, 5);
    results.tests.push(arrayCreateBench.run(() => {
        const arr = Array.from({length: 100000}, (_, idx) => idx);
    }));

    // Array Push
    const arrayPushBench = new BenchmarkRunner('Array Push', 50, 5);
    results.tests.push(arrayPushBench.run(() => {
        const arr = [];
        for (let i = 0; i < 10000; i++) {
            arr.push(i);
        }
    }));

    // Array Pop
    const arrayPopBench = new BenchmarkRunner('Array Pop', 50, 5);
    results.tests.push(arrayPopBench.run(() => {
        const arr = Array.from({length: 10000}, (_, idx) => idx);
        for (let i = 0; i < 10000; i++) {
            arr.pop();
        }
    }));

    // Array Map
    const arrayMapBench = new BenchmarkRunner('Array Map', 20, 2);
    results.tests.push(arrayMapBench.run(() => {
        const arr = Array.from({length: 10000}, (_, idx) => idx);
        const mapped = arr.map(x => x * 2);
    }));

    // Array Filter
    const arrayFilterBench = new BenchmarkRunner('Array Filter', 20, 2);
    results.tests.push(arrayFilterBench.run(() => {
        const arr = Array.from({length: 10000}, (_, idx) => idx);
        const filtered = arr.filter(x => x % 2 === 0);
    }));

    // Array Reduce
    const arrayReduceBench = new BenchmarkRunner('Array Reduce', 20, 2);
    results.tests.push(arrayReduceBench.run(() => {
        const arr = Array.from({length: 10000}, (_, idx) => idx);
        const sum = arr.reduce((a, b) => a + b, 0);
    }));

    // Array Iteration (for...of)
    const arrayIterBench = new BenchmarkRunner('Array Iteration', 20, 2);
    results.tests.push(arrayIterBench.run(() => {
        const arr = Array.from({length: 10000}, (_, idx) => idx);
        let sum = 0;
        for (const item of arr) {
            sum += item;
        }
    }));

    // Array Index Access
    const arrayIndexBench = new BenchmarkRunner('Array Index Access', 100, 10);
    const indexTestArray = Array.from({length: 1000}, (_, idx) => idx);
    results.tests.push(arrayIndexBench.run(() => {
        for (let i = 0; i < 1000000; i++) {
            const val = indexTestArray[i % indexTestArray.length];
        }
    }));

    // Object Operations
    console.log('Running Object benchmarks...');

    // Object Creation
    const objCreateBench = new BenchmarkRunner('Object Creation', 50, 5);
    results.tests.push(objCreateBench.run(() => {
        const obj = {};
        for (let i = 0; i < 10000; i++) {
            obj['key' + i] = i;
        }
    }));

    // Object Property Access
    const objAccessBench = new BenchmarkRunner('Object Property Access', 100, 10);
    const testObj = {};
    for (let i = 0; i < 1000; i++) {
        testObj['key' + i] = i;
    }
    results.tests.push(objAccessBench.run(() => {
        for (let i = 0; i < 1000000; i++) {
            const val = testObj['key' + (i % 1000)];
        }
    }));

    // Object Property Setting
    const objSetBench = new BenchmarkRunner('Object Property Setting', 50, 5);
    results.tests.push(objSetBench.run(() => {
        const obj = {};
        for (let i = 0; i < 100000; i++) {
            obj['key' + (i % 1000)] = i;
        }
    }));

    // Object Iteration (for...in)
    const objIterBench = new BenchmarkRunner('Object Iteration', 20, 2);
    const iterTestObj = {};
    for (let i = 0; i < 1000; i++) {
        iterTestObj['key' + i] = i;
    }
    results.tests.push(objIterBench.run(() => {
        let sum = 0;
        for (const key in iterTestObj) {
            sum += iterTestObj[key];
        }
    }));

    // JSON Serialization
    const jsonStringifyBench = new BenchmarkRunner('JSON Stringify', 20, 2);
    const jsonTestObj = {};
    for (let i = 0; i < 1000; i++) {
        jsonTestObj['key' + i] = {value: i, nested: {data: 'test' + i}};
    }
    results.tests.push(jsonStringifyBench.run(() => {
        const json = JSON.stringify(jsonTestObj);
    }));

    // JSON Parsing
    const jsonParseBench = new BenchmarkRunner('JSON Parse', 20, 2);
    const jsonString = JSON.stringify(jsonTestObj);
    results.tests.push(jsonParseBench.run(() => {
        const obj = JSON.parse(jsonString);
    }));

    // protoCore Collections (if available)
    if (typeof protoCore !== 'undefined') {
        console.log('Running protoCore collection benchmarks...');

        // protoCore.Set
        if (protoCore.Set) {
            // Set Creation and Add
            const setCreateBench = new BenchmarkRunner('protoCore.Set Creation', 50, 5);
            results.tests.push(setCreateBench.run(() => {
                const set = new protoCore.Set();
                for (let i = 0; i < 10000; i++) {
                    set.add(i);
                }
            }));

            // Set Has
            const setHasBench = new BenchmarkRunner('protoCore.Set Has', 100, 10);
            const testSet = new protoCore.Set();
            for (let i = 0; i < 1000; i++) {
                testSet.add(i);
            }
            results.tests.push(setHasBench.run(() => {
                for (let i = 0; i < 100000; i++) {
                    const has = testSet.has(i % 1000);
                }
            }));

            // Set Remove
            const setRemoveBench = new BenchmarkRunner('protoCore.Set Remove', 50, 5);
            results.tests.push(setRemoveBench.run(() => {
                const set = new protoCore.Set();
                for (let i = 0; i < 10000; i++) {
                    set.add(i);
                }
                for (let i = 0; i < 5000; i++) {
                    set.remove(i);
                }
            }));
        }

        // protoCore.Multiset
        if (protoCore.Multiset) {
            // Multiset Creation and Add
            const multisetCreateBench = new BenchmarkRunner('protoCore.Multiset Creation', 50, 5);
            results.tests.push(multisetCreateBench.run(() => {
                const multiset = new protoCore.Multiset();
                for (let i = 0; i < 10000; i++) {
                    multiset.add(i % 100);
                }
            }));

            // Multiset Count
            const multisetCountBench = new BenchmarkRunner('protoCore.Multiset Count', 100, 10);
            const testMultiset = new protoCore.Multiset();
            for (let i = 0; i < 10000; i++) {
                testMultiset.add(i % 100);
            }
            results.tests.push(multisetCountBench.run(() => {
                for (let i = 0; i < 100000; i++) {
                    const count = testMultiset.count(i % 100);
                }
            }));
        }

        // protoCore.SparseList
        if (protoCore.SparseList) {
            // SparseList Set
            const sparseSetBench = new BenchmarkRunner('protoCore.SparseList Set', 50, 5);
            results.tests.push(sparseSetBench.run(() => {
                const sparse = new protoCore.SparseList();
                for (let i = 0; i < 10000; i++) {
                    sparse.set(i * 10, 'value' + i);
                }
            }));

            // SparseList Get
            const sparseGetBench = new BenchmarkRunner('protoCore.SparseList Get', 100, 10);
            const testSparse = new protoCore.SparseList();
            for (let i = 0; i < 1000; i++) {
                testSparse.set(i * 10, 'value' + i);
            }
            results.tests.push(sparseGetBench.run(() => {
                for (let i = 0; i < 100000; i++) {
                    const val = testSparse.get((i % 100) * 10);
                }
            }));

            // SparseList Has
            const sparseHasBench = new BenchmarkRunner('protoCore.SparseList Has', 100, 10);
            results.tests.push(sparseHasBench.run(() => {
                for (let i = 0; i < 100000; i++) {
                    const has = testSparse.has((i % 100) * 10);
                }
            }));
        }

        // protoCore.Tuple
        if (protoCore.Tuple) {
            // Tuple Creation
            const tupleCreateBench = new BenchmarkRunner('protoCore.Tuple Creation', 50, 5);
            results.tests.push(tupleCreateBench.run(() => {
                const arr = Array.from({length: 10000}, (_, idx) => idx);
                const tuple = protoCore.Tuple(arr);
            }));

            // Tuple Access
            const tupleAccessBench = new BenchmarkRunner('protoCore.Tuple Access', 100, 10);
            const testTuple = protoCore.Tuple(Array.from({length: 1000}, (_, idx) => idx));
            results.tests.push(tupleAccessBench.run(() => {
                for (let i = 0; i < 1000000; i++) {
                    const val = testTuple[i % testTuple.length];
                }
            }));
        }
    } else {
        console.log('protoCore module not available, skipping protoCore collection benchmarks');
    }

    return results;
}

// Export for use in main benchmark runner
if (typeof module !== 'undefined' && typeof module.exports !== 'undefined') {
    module.exports = runCollectionsBenchmarks;
}
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
// Engine Comparison Runner
// Runs benchmarks in protoJS and optionally in Node.js for comparison

function calculateComparison(protojsResults, nodejsResults) {
    const comparison = {
        summary: {
            protojsTests: protojsResults.length,
            nodejsTests: nodejsResults ? nodejsResults.length : 0,
            compared: 0
        },
        tests: []
    };

    if (!nodejsResults) {
        return comparison;
    }

    // Create a map of Node.js results by test name
    const nodejsMap = {};
    if (Array.isArray(nodejsResults)) {
        nodejsResults.forEach(category => {
            if (category.tests) {
                category.tests.forEach(test => {
                    nodejsMap[test.name] = test;
                });
            }
        });
    }

    // Compare protoJS results with Node.js
    protojsResults.forEach(category => {
        if (category.tests) {
            category.tests.forEach(test => {
                const nodejsTest = nodejsMap[test.name];
                if (nodejsTest) {
                    const protojsMean = test.mean;
                    const nodejsMean = nodejsTest.mean;
                    const ratio = protojsMean / nodejsMean;
                    const percentDiff = ((protojsMean - nodejsMean) / nodejsMean) * 100;
                    
                    comparison.tests.push({
                        name: test.name,
                        category: category.category,
                        protojs: {
                            mean: protojsMean,
                            median: test.median,
                            min: test.min,
                            max: test.max,
                            stddev: test.stddev
                        },
                        nodejs: {
                            mean: nodejsMean,
                            median: nodejsTest.median,
                            min: nodejsTest.min,
                            max: nodejsTest.max,
                            stddev: nodejsTest.stddev
                        },
                        comparison: {
                            ratio: ratio,
                            percentDiff: percentDiff,
                            faster: ratio < 1 ? 'protojs' : 'nodejs',
                            speedup: ratio < 1 ? (1 / ratio) : ratio
                        }
                    });
                    comparison.summary.compared++;
                } else {
                    // protoJS-only test
                    comparison.tests.push({
                        name: test.name,
                        category: category.category,
                        protojs: {
                            mean: test.mean,
                            median: test.median,
                            min: test.min,
                            max: test.max,
                            stddev: test.stddev
                        },
                        nodejs: null,
                        comparison: null
                    });
                }
            });
        }
    });

    return comparison;
}

// Run Node.js benchmarks (if Node.js is available)
async function runNodeJSTests() {
    // This would typically spawn a child process to run Node.js
    // For now, return null to indicate Node.js comparison is not available
    // In a full implementation, this would:
    // 1. Spawn: child_process.spawn('node', ['benchmark_runner.js'])
    // 2. Collect stdout/stderr
    // 3. Parse JSON results
    // 4. Return structured results
    
    console.log('Node.js comparison not available in this environment');
    return null;
}

// Export functions
if (typeof module !== 'undefined' && typeof module.exports !== 'undefined') {
    module.exports = {
        calculateComparison,
        runNodeJSTests
    };
}
// HTML Report Generator
// Generates user-friendly HTML reports with charts and comparison tables

function generateHTMLReport(allResults, comparison = null) {
    const timestamp = new Date().toISOString();
    const dateStr = new Date().toLocaleString();
    
    // Get engine versions
    let protojsVersion = 'Unknown';
    let nodejsVersion = null;
    
    try {
        if (typeof process !== 'undefined' && process.versions) {
            if (process.versions.protojs) {
                protojsVersion = process.versions.protojs;
            }
            if (process.versions.node) {
                nodejsVersion = process.versions.node;
            }
        }
    } catch (e) {
        // Ignore
    }

    const html = `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ProtoJS Performance Report</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
            line-height: 1.6;
            color: #333;
            background: #f5f5f5;
            padding: 20px;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            padding: 30px;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        
        h1 {
            color: #2c3e50;
            border-bottom: 3px solid #3498db;
            padding-bottom: 10px;
            margin-bottom: 30px;
        }
        
        h2 {
            color: #34495e;
            margin-top: 40px;
            margin-bottom: 20px;
            padding-bottom: 10px;
            border-bottom: 2px solid #ecf0f1;
        }
        
        h3 {
            color: #555;
            margin-top: 30px;
            margin-bottom: 15px;
        }
        
        .summary {
            background: #ecf0f1;
            padding: 20px;
            border-radius: 5px;
            margin-bottom: 30px;
        }
        
        .summary-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-top: 15px;
        }
        
        .summary-item {
            background: white;
            padding: 15px;
            border-radius: 5px;
            border-left: 4px solid #3498db;
        }
        
        .summary-item h4 {
            color: #7f8c8d;
            font-size: 0.9em;
            margin-bottom: 5px;
        }
        
        .summary-item .value {
            font-size: 1.5em;
            font-weight: bold;
            color: #2c3e50;
        }
        
        table {
            width: 100%;
            border-collapse: collapse;
            margin: 20px 0;
            background: white;
        }
        
        th, td {
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }
        
        th {
            background: #34495e;
            color: white;
            font-weight: 600;
            cursor: pointer;
            user-select: none;
        }
        
        th:hover {
            background: #2c3e50;
        }
        
        tr:hover {
            background: #f8f9fa;
        }
        
        .faster {
            color: #27ae60;
            font-weight: bold;
        }
        
        .slower {
            color: #e74c3c;
            font-weight: bold;
        }
        
        .similar {
            color: #f39c12;
            font-weight: bold;
        }
        
        .chart-container {
            margin: 30px 0;
            padding: 20px;
            background: #fafafa;
            border-radius: 5px;
        }
        
        .section {
            margin-bottom: 40px;
        }
        
        .footer {
            margin-top: 50px;
            padding-top: 20px;
            border-top: 2px solid #ecf0f1;
            color: #7f8c8d;
            font-size: 0.9em;
        }
        
        .badge {
            display: inline-block;
            padding: 4px 8px;
            border-radius: 3px;
            font-size: 0.85em;
            font-weight: bold;
        }
        
        .badge-success {
            background: #27ae60;
            color: white;
        }
        
        .badge-warning {
            background: #f39c12;
            color: white;
        }
        
        .badge-danger {
            background: #e74c3c;
            color: white;
        }
        
        .expandable {
            cursor: pointer;
        }
        
        .expandable::before {
            content: '▶ ';
            display: inline-block;
            transition: transform 0.2s;
        }
        
        .expandable.expanded::before {
            transform: rotate(90deg);
        }
        
        .hidden {
            display: none;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>ProtoJS Performance Report</h1>
        
        <div class="summary">
            <h2>Executive Summary</h2>
            <p><strong>Generated:</strong> ${dateStr}</p>
            <p><strong>ProtoJS Version:</strong> ${protojsVersion}</p>
            ${nodejsVersion ? `<p><strong>Node.js Version:</strong> ${nodejsVersion}</p>` : ''}
            
            <div class="summary-grid">
                ${generateSummaryStats(allResults, comparison)}
            </div>
        </div>
        
        ${generateCategorySections(allResults, comparison)}
        
        <div class="footer">
            <p><strong>Test Environment:</strong> ${getEnvironmentInfo()}</p>
            <p><strong>Methodology:</strong> All benchmarks run with 100 iterations (default) and 10 warmup iterations. Results show mean execution time in milliseconds unless otherwise specified.</p>
        </div>
    </div>
    
    <script>
        ${generateChartScripts(allResults, comparison)}
        
        // Table sorting functionality
        document.querySelectorAll('th').forEach(header => {
            header.addEventListener('click', () => {
                const table = header.closest('table');
                const tbody = table.querySelector('tbody');
                const rows = Array.from(tbody.querySelectorAll('tr'));
                const index = Array.from(header.parentElement.children).indexOf(header);
                const isAsc = header.classList.contains('asc');
                
                rows.sort((a, b) => {
                    const aVal = a.children[index].textContent.trim();
                    const bVal = b.children[index].textContent.trim();
                    const aNum = parseFloat(aVal);
                    const bNum = parseFloat(bVal);
                    
                    if (!isNaN(aNum) && !isNaN(bNum)) {
                        return isAsc ? bNum - aNum : aNum - bNum;
                    }
                    return isAsc ? bVal.localeCompare(aVal) : aVal.localeCompare(bVal);
                });
                
                rows.forEach(row => tbody.appendChild(row));
                header.classList.toggle('asc');
            });
        });
        
        // Expandable sections
        document.querySelectorAll('.expandable').forEach(el => {
            el.addEventListener('click', () => {
                el.classList.toggle('expanded');
                const content = el.nextElementSibling;
                if (content) {
                    content.classList.toggle('hidden');
                }
            });
        });
    </script>
</body>
</html>`;

    return html;
}

function generateSummaryStats(allResults, comparison) {
    let totalTests = 0;
    let totalCategories = 0;
    
    allResults.forEach(category => {
        totalCategories++;
        if (category.tests) {
            totalTests += category.tests.length;
        }
    });
    
    let comparedTests = 0;
    if (comparison && comparison.summary) {
        comparedTests = comparison.summary.compared || 0;
    }
    
    return `
        <div class="summary-item">
            <h4>Total Categories</h4>
            <div class="value">${totalCategories}</div>
        </div>
        <div class="summary-item">
            <h4>Total Tests</h4>
            <div class="value">${totalTests}</div>
        </div>
        ${comparedTests > 0 ? `
        <div class="summary-item">
            <h4>Compared Tests</h4>
            <div class="value">${comparedTests}</div>
        </div>
        ` : ''}
    `;
}

function generateCategorySections(allResults, comparison) {
    let html = '';
    
    allResults.forEach(category => {
        html += `
        <div class="section">
            <h2>${category.category}</h2>
            ${generateTestTable(category, comparison)}
            ${generateCategoryChart(category, comparison)}
        </div>
        `;
    });
    
    return html;
}

function generateTestTable(category, comparison) {
    if (!category.tests || category.tests.length === 0) {
        return '<p>No tests available for this category.</p>';
    }
    
    // Create comparison map
    const comparisonMap = {};
    if (comparison && comparison.tests) {
        comparison.tests.forEach(test => {
            if (test.category === category.category) {
                comparisonMap[test.name] = test;
            }
        });
    }
    
    let tableHtml = `
    <table>
        <thead>
            <tr>
                <th>Test Name</th>
                <th>Mean (ms)</th>
                <th>Median (ms)</th>
                <th>Min (ms)</th>
                <th>Max (ms)</th>
                <th>Std Dev (ms)</th>
                ${comparison ? '<th>Comparison</th>' : ''}
            </tr>
        </thead>
        <tbody>
    `;
    
    category.tests.forEach(test => {
        const comp = comparisonMap[test.name];
        let comparisonCell = '';
        
        if (comp && comp.comparison) {
            const { ratio, percentDiff, faster } = comp.comparison;
            const badgeClass = faster === 'protojs' ? 'badge-success' : 
                             Math.abs(percentDiff) < 10 ? 'badge-warning' : 'badge-danger';
            const badgeText = faster === 'protojs' ? 'Faster' : 
                            Math.abs(percentDiff) < 10 ? 'Similar' : 'Slower';
            comparisonCell = `
                <td>
                    <span class="badge ${badgeClass}">${badgeText}</span><br>
                    <small>${percentDiff > 0 ? '+' : ''}${percentDiff.toFixed(1)}%</small>
                </td>
            `;
        } else if (comparison) {
            comparisonCell = '<td>-</td>';
        }
        
        tableHtml += `
            <tr>
                <td>${test.name}</td>
                <td>${test.mean.toFixed(3)}</td>
                <td>${test.median.toFixed(3)}</td>
                <td>${test.min.toFixed(3)}</td>
                <td>${test.max.toFixed(3)}</td>
                <td>${test.stddev.toFixed(3)}</td>
                ${comparisonCell}
            </tr>
        `;
    });
    
    tableHtml += `
        </tbody>
    </table>
    `;
    
    return tableHtml;
}

function generateCategoryChart(category, comparison) {
    if (!category.tests || category.tests.length === 0) {
        return '';
    }
    
    const chartId = `chart-${category.category.replace(/\s+/g, '-').toLowerCase()}`;
    const labels = category.tests.map(t => t.name);
    const protojsData = category.tests.map(t => t.mean);
    
    let nodejsData = null;
    if (comparison && comparison.tests) {
        const comparisonMap = {};
        comparison.tests.forEach(test => {
            if (test.category === category.category && test.nodejs) {
                comparisonMap[test.name] = test.nodejs.mean;
            }
        });
        nodejsData = category.tests.map(t => comparisonMap[t.name] || null);
    }
    
    const datasets = [{
        label: 'ProtoJS',
        data: protojsData,
        backgroundColor: 'rgba(52, 152, 219, 0.6)',
        borderColor: 'rgba(52, 152, 219, 1)',
        borderWidth: 1
    }];
    
    if (nodejsData && nodejsData.some(v => v !== null)) {
        datasets.push({
            label: 'Node.js',
            data: nodejsData,
            backgroundColor: 'rgba(46, 204, 113, 0.6)',
            borderColor: 'rgba(46, 204, 113, 1)',
            borderWidth: 1
        });
    }
    
    return `
    <div class="chart-container">
        <h3>Performance Comparison Chart</h3>
        <canvas id="${chartId}" width="400" height="200"></canvas>
        <script>
            (function() {
                const ctx = document.getElementById('${chartId}').getContext('2d');
                new Chart(ctx, {
                    type: 'bar',
                    data: {
                        labels: ${JSON.stringify(labels)},
                        datasets: ${JSON.stringify(datasets)}
                    },
                    options: {
                        responsive: true,
                        scales: {
                            y: {
                                beginAtZero: true,
                                title: {
                                    display: true,
                                    text: 'Time (ms)'
                                }
                            }
                        }
                    }
                });
            })();
        </script>
    </div>
    `;
}

function generateChartScripts(allResults, comparison) {
    // Charts are generated inline in generateCategoryChart
    return '';
}

function getEnvironmentInfo() {
    let info = [];
    
    if (typeof process !== 'undefined') {
        if (process.platform) info.push(`Platform: ${process.platform}`);
        if (process.arch) info.push(`Architecture: ${process.arch}`);
    }
    
    return info.length > 0 ? info.join(', ') : 'Unknown';
}

// Export for use in main benchmark runner
if (typeof module !== 'undefined' && typeof module.exports !== 'undefined') {
    module.exports = {
        generateHTMLReport,
        generateSummaryStats,
        generateCategorySections,
        generateTestTable,
        generateCategoryChart
    };
}
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
