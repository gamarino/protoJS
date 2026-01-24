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
