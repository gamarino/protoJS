# Performance Testing Guide

## Overview

The ProtoJS Performance Test Suite provides comprehensive benchmarking capabilities for measuring and comparing the performance of protoJS against other JavaScript engines, particularly Node.js (V8).

## Test Suite Structure

The performance test suite consists of the following components:

### Core Framework

- **`benchmark_runner.js`**: Core benchmarking framework with statistical analysis
  - `BenchmarkRunner` class for running benchmarks
  - Statistical calculations (mean, median, min, max, standard deviation)
  - Memory measurement support
  - High-resolution timing

### Test Categories

1. **`basic_types.js`**: Basic JavaScript type performance
   - Number operations (addition, multiplication, division, coercion)
   - String operations (concatenation, substring, length access)
   - Boolean operations (AND, OR, NOT)
   - Null/Undefined checks
   - BigInt operations (if supported)

2. **`collections.js`**: Collection performance benchmarks
   - Standard JavaScript: Arrays, Objects
   - ProtoCore collections: Set, Multiset, SparseList, Tuple
   - Operations: creation, access, modification, iteration

3. **`overall_performance.js`**: Overall runtime performance
   - Startup time
   - Memory usage
   - Throughput (operations per second)
   - Function call overhead
   - Closure creation
   - Type checking

### Utilities

- **`compare_engines.js`**: Engine comparison functionality
  - Compares protoJS results with Node.js
  - Calculates performance ratios and differences
  - Generates comparison statistics

- **`report_generator.js`**: HTML report generation
  - Creates user-friendly HTML reports
  - Interactive charts using Chart.js
  - Sortable tables
  - Color-coded performance indicators

### Entry Points

- **`run_all.js`**: Main benchmark runner
  - Orchestrates all benchmark categories
  - Generates reports
  - Saves results to files

- **`run_performance_suite.js`**: Combined loader for protoJS
  - Loads all benchmark files in correct order
  - Handles module loading for protoJS environment

## Running the Performance Test Suite

### Prerequisites

- ProtoJS binary compiled and available
- Node.js (optional, for comparison)

### Basic Usage

#### Using protoJS

The suite detects protoJS via the global `__protojs__` and runs **synchronously** (`runAllBenchmarksSync`) so that benchmarks execute without relying on the Promise job queue. Use the combined file:

```bash
cd tests/benchmarks
./combine_suite.sh
../../build/protojs combined_performance_suite.js
```

**Current limitation:** Under the protoCore interpreter, host calls (e.g. `console.log`) may not produce visible output; the benchmark logic still runs and completes. For full console output and HTML/JSON reports, run the suite with **Node.js** from `tests/benchmarks` (see below).

#### Using Node.js (for comparison)

```bash
node tests/benchmarks/run_all.js
```

#### Node vs QuickJS vs protoJS (standard suite)

To compare **Node.js**, **QuickJS** (vanilla `qjs`), and **protoJS** on the same in-process benchmarks:

```bash
# From repo root; requires built protojs and deps/quickjs/qjs
node tests/benchmarks/run_node_quickjs_comparison.js
```

- Uses benchmarks in `tests/benchmarks/standard/` that output `__BENCH_RESULT__` with `time_ms`.
- Build QuickJS CLI if needed: `cd deps/quickjs && make qjs`.
- Report: `tests/benchmarks/results/node_quickjs_comparison.json`.
- See [PERFORMANCE_REPORT.md](PERFORMANCE_REPORT.md) for the "Node vs QuickJS" analysis section.

#### protoJS vs Node.js (legacy wall-clock suite)

```bash
node tests/benchmarks/run_nodejs_comparison.js
```

Compares protoJS and Node on the legacy 5 benchmarks (array_operations, basic_types, collections, concurrent_operations, overall_performance) using wall-clock time. Reports: `results/nodejs_comparison.json`, `results/nodejs_comparison.html`.

### ProtoJS runtime notes

- When `__protojs__` is set (or `process`/`process.exit` is absent), the suite uses **`runAllBenchmarksSync()`** so execution completes within a single eval (no Promise job queue).
- **Host calls** (e.g. `console.log`, `console.error`) from the script may not produce visible output when running under the protoCore interpreter; this is a known limitation of the current host-call bridge. The benchmark code still runs to completion. For visible output and file reports, use Node.js as above.

### Output

The test suite generates:

1. **HTML Report**: `tests/benchmarks/results/report_YYYY-MM-DD_HH-MM-SS.html`
   - Interactive charts and tables
   - Performance comparisons
   - Statistical summaries

2. **JSON Results**: `tests/benchmarks/results/results_YYYY-MM-DD_HH-MM-SS.json`
   - Raw benchmark data
   - Machine-readable format
   - Suitable for automated analysis

## Understanding the Reports

### Executive Summary

The report begins with an executive summary showing:
- Total test categories
- Total tests executed
- Number of tests compared (if Node.js comparison was run)
- Generation timestamp and version information

### Test Categories

Each category section includes:

1. **Performance Table**
   - Test name
   - Mean execution time (ms)
   - Median execution time (ms)
   - Minimum execution time (ms)
   - Maximum execution time (ms)
   - Standard deviation (ms)
   - Comparison indicators (if Node.js comparison available)

2. **Performance Chart**
   - Bar chart comparing protoJS vs Node.js
   - Visual representation of performance differences
   - Interactive (hover for details)

### Performance Indicators

- **Green Badge (Faster)**: ProtoJS is faster than Node.js
- **Yellow Badge (Similar)**: Performance is within 10% (similar)
- **Red Badge (Slower)**: ProtoJS is slower than Node.js

### Statistical Measures

- **Mean**: Average execution time across all iterations
- **Median**: Middle value when sorted (less affected by outliers)
- **Min/Max**: Best and worst case execution times
- **Std Dev**: Measure of variance in execution times

## Interpreting Results

### What to Look For

1. **Consistency**: Low standard deviation indicates consistent performance
2. **Outliers**: High max values may indicate GC pauses or system interference
3. **Trends**: Compare similar operations to identify patterns
4. **Memory**: Check memory deltas for memory-intensive operations

### Performance Expectations

- **Basic Types**: Should be very fast (< 1ms for most operations)
- **Collections**: Performance depends on size and operation type
- **Overall Performance**: Startup time and throughput are key metrics

### Comparison with Node.js

When comparing with Node.js:
- **Ratio < 1.0**: ProtoJS is faster (lower is better)
- **Ratio > 1.0**: ProtoJS is slower
- **Percent Difference**: Shows relative performance difference

## Adding New Benchmarks

### Creating a New Benchmark

1. **Choose the appropriate category file**:
   - Basic types → `basic_types.js`
   - Collections → `collections.js`
   - Overall performance → `overall_performance.js`

2. **Create a BenchmarkRunner instance**:
   ```javascript
   const bench = new BenchmarkRunner('My Test Name', 100, 10);
   ```

3. **Run the benchmark**:
   ```javascript
   const result = bench.run(() => {
       // Your test code here
   });
   ```

4. **Add to results**:
   ```javascript
   results.tests.push(result);
   ```

### Example

```javascript
// In basic_types.js
const myBench = new BenchmarkRunner('Custom Operation', 100, 10);
results.tests.push(myBench.run(() => {
    // Perform operation 1M times
    for (let i = 0; i < 1000000; i++) {
        // Your operation
    }
}));
```

## Comparison Methodology

### Test Execution

1. **Warmup Phase**: Runs 10 iterations (default) to warm up JIT and caches
2. **Measurement Phase**: Runs 100 iterations (default) for statistical accuracy
3. **Statistical Analysis**: Calculates mean, median, min, max, stddev

### Fair Comparison

- Same test code for both engines
- Same number of iterations
- Same warmup iterations
- Same measurement iterations
- Run on same hardware when possible

### Limitations

- System load can affect results
- GC pauses may cause outliers
- First run may be slower (cold start)
- Results may vary between runs

## Best Practices

1. **Run Multiple Times**: Average results from multiple runs
2. **Minimize System Load**: Close other applications
3. **Use Consistent Hardware**: Same machine for comparisons
4. **Check for Outliers**: Review min/max values
5. **Consider Memory**: Some operations trade speed for memory

## Troubleshooting

### "BenchmarkRunner class not found"

**Solution**: Ensure `benchmark_runner.js` is loaded first.

### "Function not found" errors

**Solution**: Load all benchmark files in the correct order:
1. benchmark_runner.js
2. basic_types.js
3. collections.js
4. overall_performance.js
5. compare_engines.js
6. report_generator.js
7. run_all.js

### Reports not generating

**Solution**: 
- Check that `results/` directory exists
- Verify file system write permissions
- Check console for error messages

### Memory measurements unavailable

**Solution**: Memory measurement requires `process.memoryUsage()` which may not be available in protoJS. This is expected and benchmarks will still run without memory data.

## Future Enhancements

- Automated CI/CD integration
- Historical performance tracking
- Performance regression detection
- More detailed memory profiling
- CPU profiling integration
- Network performance benchmarks

## See Also

- [API Reference](API_REFERENCE.md)
- [Testing Strategy](TESTING_STRATEGY.md)
- [Performance Optimization](PHASE3_PERFORMANCE_OPTIMIZATION.md)
