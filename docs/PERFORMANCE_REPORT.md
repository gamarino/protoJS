# ProtoJS Performance Test Report

**Generated:** January 24, 2026  
**Test Suite Version:** 1.0  
**Status:** Sample Report Generated

## Overview

This document contains the performance test results for protoJS. The full interactive HTML report is available at [performance/sample_performance_report.html](performance/sample_performance_report.html).

## Quick Access

- **[View Full HTML Report](performance/sample_performance_report.html)** - Interactive report with charts and detailed metrics

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

To generate a new performance report:

```bash
# Combine all benchmark files
cd tests/benchmarks
./combine_suite.sh

# Run the suite (may take several minutes)
../../build/protojs combined_performance_suite.js

# Reports will be saved to tests/benchmarks/results/
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

For comprehensive performance analysis, run the full test suite:

```bash
cd tests/benchmarks
./combine_suite.sh
../../build/protojs combined_performance_suite.js
```

The full suite includes:
- 45+ individual benchmarks
- 100 iterations per test (default)
- Statistical analysis (mean, median, stddev)
- HTML report generation
- Node.js comparison (when available)

**Expected Duration:** 5-10 minutes for full suite execution
