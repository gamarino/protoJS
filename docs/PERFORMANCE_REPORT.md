# ProtoJS Performance Test Report

**Generated:** January 24, 2026  
**Last updated:** 2026-03-06 (Node.js comparison run)  
**Test Suite Version:** 1.0  
**Status:** Sample Report Generated

## Overview

This document contains the performance test results for protoJS. The full interactive HTML report is available at [performance/sample_performance_report.html](performance/sample_performance_report.html).

### Latest Node.js comparison (2026-03-06)

The Node.js comparison suite (`run_nodejs_comparison.js`) was run on 2026-03-06: **5/5 benchmarks passed**. protoJS wins all 5. Overall speedup **10.81x** (protoJS avg 42.6 ms vs Node 460.4 ms). Array operations: **45x** faster. See [PERFORMANCE_RUN_2026-02-07.md](PERFORMANCE_RUN_2026-02-07.md) for the full run report and latest results table.

### Full performance analysis (2026-03-06)

The full combined suite (`combined_performance_suite.js`) was run with **Node.js** from `tests/benchmarks`: **41 tests** in **3 categories** (Basic Types 17, Collections 14, Overall Performance 10). Report and JSON were written to `tests/benchmarks/results/`:

- **HTML:** `tests/benchmarks/results/report_2026-03-06_00-07-34.html`
- **JSON:** `tests/benchmarks/results/results_2026-03-06_00-07-34.json`

Run from repo root: `cd tests/benchmarks && ./combine_suite.sh && node combined_performance_suite.js` (≈20–30 s). To get file output, the suite must be executed with `node` from the `tests/benchmarks` directory so that the `results/` path resolves correctly.

## Quick Access

- **[View Full HTML Report](performance/sample_performance_report.html)** - Sample interactive report
- **Latest full run:** `tests/benchmarks/results/report_2026-03-06_00-07-34.html` - Full analysis (41 tests, 2026-03-06)

## Results analysis (2026-03-06)

### Node.js comparison (protoJS vs Node)

| Benchmark | protoJS (ms) | Node.js (ms) | Speedup | Winner |
|-----------|--------------|--------------|---------|--------|
| array_operations.js | 46 | 2,070 | **45.00x** | protoJS |
| basic_types.js | 41 | 48 | 1.17x | protoJS |
| collections.js | 43 | 52 | 1.21x | protoJS |
| concurrent_operations.js | 41 | 61 | 1.49x | protoJS |
| overall_performance.js | 42 | 71 | 1.69x | protoJS |

**Summary:** 5/5 passed; protoJS wins 5; average protoJS 42.6 ms, Node 460.4 ms; **overall speedup 10.81x**.

**Findings:**
- **Array operations** dominate the gap: ~2 s in Node vs ~46 ms in protoJS. Immutable arrays and structural sharing in protoCore avoid full copies on large map/filter/reduce workloads.
- **Basic types, collections, concurrent, overall** show smaller but consistent wins (1.2–1.7x). protoJS stays in the 41–43 ms band; Node 48–71 ms.
- **Stability:** All benchmarks completed without failures; JSON/HTML reports written successfully.

### Full suite (41 tests, Node.js)

The full suite ran in ~30 s. Categories and sample metrics (mean time per iteration, in ms):

- **Basic Types (17):** Number ops ~0.15–4.3, String ~0.01–7.8, Boolean ~0.87–1.6, BigInt ~3.9–21.3. BigInt multiplication is the slowest (~21 ms/iter).
- **Collections (14):** Array creation ~10.7, Array map/filter/reduce ~0.96–1.0, Object property access ~172.9 (outlier), Object iteration ~0.19, JSON parse/stringify ~0.4–1.0.
- **Overall (10):** Startup ~0.004, Throughput ~1.2, Memory (object) ~83.6, Function call ~1.1, Closure ~0.04, Try-catch ~0.1, Type checking ~8.1.

**Notable:** Object property access (172.9 ms mean) and Memory: Object creation (83.6 ms) are the heaviest in the suite; the rest are &lt;25 ms per iteration. Use the generated HTML report for charts and sortable tables.

## Report Contents

The performance report includes:

1. **Executive Summary**
   - Total test categories
   - Total tests executed
   - Generation timestamp

2. **Basic Types Performance**
   - Number operations
   - String operations
   - Boolean operations
   - Null/Undefined checks
   - BigInt operations (if supported)

3. **Collections Performance**
   - Array operations
   - Object operations
   - ProtoCore collections (Set, Multiset, SparseList, Tuple)

4. **Overall Performance**
   - Startup time
   - Throughput metrics
   - Memory usage
   - Function call overhead

## Running Your Own Tests

To generate a new **full performance analysis** (HTML + JSON in `results/`):

```bash
cd tests/benchmarks
./combine_suite.sh
node combined_performance_suite.js
# Reports: results/report_*.html, results/results_*.json
```

The suite must run from `tests/benchmarks` with **Node.js** so that `results/` exists and file writes succeed. Execution takes about 20–30 seconds (41 tests, 100 iterations each).

To run with protoJS (benchmarks run synchronously via `runAllBenchmarksSync`; console and file output may not appear due to current host-call handling in the protoCore interpreter):

```bash
cd tests/benchmarks
./combine_suite.sh
../../build/protojs combined_performance_suite.js
```

For faster testing with reduced iterations:

```bash
# Quick test (10 iterations per benchmark)
../../build/protojs quick_suite.js
```

## Report Features

The HTML report includes:

- **Interactive Charts**: Bar charts comparing performance metrics
- **Sortable Tables**: Click column headers to sort
- **Statistical Details**: Mean, median, min, max, standard deviation
- **Color Coding**: Visual indicators for performance levels
- **Responsive Design**: Works on desktop and mobile

## Notes

- Sample report uses representative data
- Full reports are generated with 100 iterations per test (default)
- Results may vary based on system load and hardware
- For accurate comparisons, run tests on dedicated hardware

## See Also

- [Performance Testing Guide](PERFORMANCE_TESTING.md) - How to run and interpret tests
- [API Reference](API_REFERENCE.md) - ProtoJS API documentation

## Actual Test Results

### Quick Validation Test (Minimal Iterations)

**Test Date:** January 24, 2026  
**Test Type:** Minimal validation (3 iterations per test)

#### Results

| Test Name | Mean (ms) | Median (ms) | Min (ms) | Max (ms) |
|-----------|-----------|-------------|----------|----------|
| Number Addition | 0.00 | 0.00 | 0.00 | 0.00 |
| String Concatenation | 0.33 | 0.33 | 0.00 | 1.00 |
| Array Creation | 0.00 | 0.00 | 0.00 | 0.00 |

**Note:** These are minimal tests with reduced iterations for quick validation. Full performance reports use 100 iterations per test for statistical accuracy.

### Test Output

```
=== ProtoJS Minimal Performance Test ===

Running minimal benchmarks...

Results:
  Number Addition: 0.00ms (mean)
  String Concatenation: 0.33ms (mean)
  Array Creation: 0.00ms (mean)

=== Minimal Test Complete ===
```

## Full Test Suite

For comprehensive performance analysis, run the full test suite (see "Running Your Own Tests" above). The full suite includes:

- **41 benchmarks** in 3 categories (Basic Types, Collections, Overall Performance)
- 100 iterations per test (default)
- Statistical analysis (mean, median, stddev, memory delta)
- HTML and JSON report generation (when run with Node from `tests/benchmarks`)

**Expected duration:** ~20–30 seconds with Node.js
