# Benchmark CI/CD Integration

**Phase 6** — Use the BenchmarkRunner C++ API for statistical analysis, regression detection, automated execution, and CI integration.

---

## Overview

- **Statistical analysis**: Run benchmarks multiple times; get mean, median, stddev, min, max.
- **Regression detection**: Compare current results to a baseline; flag benchmarks that exceed a threshold (e.g. 10% slower).
- **Automated execution**: Run a suite from a config file (suite name + list of benchmark paths).
- **CI/CD**: Run suite, compare to baseline, write report; exit with failure if any benchmark regressed.

---

## Config file format

Used by `runSuiteFromFile` and `runForCI`:

- **First non-empty, non-comment line** = suite name.
- **Following lines** = paths to benchmark scripts (one per line).
- Lines starting with `#` and blank lines are ignored.

Example (`tests/benchmarks/suite_config.txt`):

```
Phase 6 automated suite
tests/benchmarks/minimal_test.js
tests/benchmarks/phase6_benchmark_suite.js
```

Paths are relative to the current working directory when the suite is run.

---

## Baseline format

- **Save**: `BenchmarkRunner::saveBaseline(results, path)` writes a CSV file (name, protojs_time_ms, nodejs_time_ms, speedup, memory_usage_bytes, success).
- **Load**: `BenchmarkRunner::loadBaseline(path)` reads that CSV for regression comparison.

First CI run: no baseline file → current run is saved as baseline and the run is considered successful. Later runs: baseline is loaded and compared.

---

## CI flow (C++)

1. Build your executable that links `BenchmarkRunner` (e.g. test harness or main app).
2. From C++, call:
   ```cpp
   CIRunResult ci = BenchmarkRunner::runForCI(
       "/path/to/suite_config.txt",
       "/path/to/baseline.csv",
       10.0,   // threshold: 10% slower = regression
       "/path/to/report.txt"
   );
   if (!ci.success) {
       for (const auto& name : ci.regressed) fprintf(stderr, "Regression: %s\n", name.c_str());
       return 1;
   }
   return 0;
   ```
3. In CI, run this executable; non-zero exit means regression.

---

## Example: unit test

The Phase 6 unit tests include a test that runs `runForCI` with no baseline (see `tests/unit/test_benchmark_runner.cpp`). When the baseline file does not exist, the current results are saved as baseline and the run succeeds.

---

## References

- [Phase 6 module guides](PHASE6_MODULE_GUIDES.md) — BenchmarkRunner API summary and examples.
- [Technical audit Phase 6](../TECHNICAL_AUDIT_PHASE6.md) — Benchmarking enhancements section.
