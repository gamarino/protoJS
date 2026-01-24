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
