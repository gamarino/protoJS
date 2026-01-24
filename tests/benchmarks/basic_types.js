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
