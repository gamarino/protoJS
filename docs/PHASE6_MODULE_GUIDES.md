# Phase 6 Module Guides: npm, Benchmarking, Node.js Test Compatibility

**Version:** 1.0.0  
**Date:** January 2026  
**Audience:** Developers integrating or extending Phase 6 (C++ APIs)

---

## 1. Overview

Phase 6 adds three main areas:

- **Extended npm support**: Registry client, Semver resolution, package installation (C++).
- **Performance benchmarking**: Run scripts with protoJS/Node, compare time and memory, export reports (C++).
- **Node.js test compatibility**: Run tests with both runtimes, compare output, generate compatibility reports (C++).

These are implemented as C++ libraries used by the runtime/tooling. This guide covers API usage and examples.

---

## 2. Semver (Version Resolution)

**Headers:** `src/npm/Semver.h`  
**Implementation:** `src/npm/Semver.cpp`

### API Summary

| Method | Purpose |
|--------|--------|
| `Semver::parse(version, major, minor, patch, prerelease, build)` | Parse a version string; returns `bool`. |
| `Semver::compare(v1, v2)` | Compare two versions; returns `-1`, `0`, or `1`. |
| `Semver::satisfies(version, range)` | Check if version satisfies range; returns `bool`. |
| `Semver::findHighest(versions, range)` | Return highest version in list satisfying range. |
| `Semver::normalize(version)` | Normalize version string (e.g. drop leading `v`). |

### Usage Example (C++)

```cpp
#include "npm/Semver.h"
#include <vector>
#include <string>

using namespace protojs;

// Parse
int major, minor, patch;
std::string prerelease, build;
bool ok = Semver::parse("1.2.3-alpha.1", major, minor, patch, prerelease, build);
// ok == true, major==1, minor==2, patch==3, prerelease=="alpha.1"

// Compare
int cmp = Semver::compare("2.0.0", "1.9.9");  // 1 (greater)

// Range satisfaction
bool sat = Semver::satisfies("1.2.3", "^1.0.0");  // true
bool tilde = Semver::satisfies("1.2.9", "~1.2.0"); // true

// Resolve best version
std::vector<std::string> versions = {"1.0.0", "1.2.0", "1.2.3", "2.0.0"};
std::string best = Semver::findHighest(versions, "^1.0.0");  // "1.2.3"
```

### Supported range operators

- Exact: `1.2.3`, `=1.2.3`
- Comparison: `>=`, `<=`, `>`, `<`
- Tilde: `~1.2.3` (>=1.2.3 <1.3.0)
- Caret: `^1.2.3` (>=1.2.3 <2.0.0)
- Wildcard: `*`, `latest`

---

## 3. NPM Registry Client

**Headers:** `src/npm/NPMRegistry.h`  
**Implementation:** `src/npm/NPMRegistry.cpp`

### API Summary

| Method | Purpose |
|--------|--------|
| `NPMRegistry::fetchPackage(name, registry)` | Fetch package metadata from registry (uses cache when TTL &gt; 0). |
| `NPMRegistry::resolveVersion(name, range, registry)` | Resolve a version range to a concrete version. |
| `NPMRegistry::downloadPackage(name, version, targetDir, registry, progress)` | Download package tarball; optional `progress(bytesReceived, totalBytes)`. |
| `NPMRegistry::downloadPackages(specs, registry, progress, maxConcurrency)` | Download multiple packages in parallel; returns one bool per spec. Default `maxConcurrency` 4. |
| `NPMRegistry::searchPackages(query, limit, registry)` | Search packages by name. |
| `NPMRegistry::setCacheTTL(seconds)` | Set cache TTL for fetchPackage (0 = disable). Default 300. |
| `NPMRegistry::clearCache()` | Clear in-memory package metadata cache. |
| `NPMRegistry::setProgressCallback(cb)` | Set global progress callback for downloads. |

### Data Structures

- **PackageMetadata**: `name`, `description`, `versions` (map of version → PackageVersion), `latest`, `dist_tags`.
- **PackageVersion**: `version`, `dist_tarball`, `dist_shasum`, `dependencies`, `main`, `module`, `type`.

### Usage Example (C++)

```cpp
#include "npm/NPMRegistry.h"
#include <string>

using namespace protojs;

// Fetch metadata
PackageMetadata meta = NPMRegistry::fetchPackage("lodash");
std::string latest = meta.latest;

// Resolve version
std::string resolved = NPMRegistry::resolveVersion("lodash", "^4.0.0");

// Download (e.g. for install)
bool ok = NPMRegistry::downloadPackage("lodash", "4.17.21", "/tmp/pkgs");

// Download with progress
NPMRegistry::downloadPackage("lodash", "4.17.21", "/tmp/pkgs", NPMRegistry::DEFAULT_REGISTRY,
    [](size_t bytes, size_t total) { printf("%zu / %zu\n", bytes, total); });

// Cache: default TTL 300s; disable or clear
NPMRegistry::setCacheTTL(std::chrono::seconds(600));
NPMRegistry::clearCache();

// Parallel downloads (up to 4 concurrent)
std::vector<DownloadSpec> specs = { {"lodash","4.17.21","/tmp/p1"}, {"chalk","2.4.2","/tmp/p2"} };
std::vector<bool> ok = NPMRegistry::downloadPackages(specs, NPMRegistry::DEFAULT_REGISTRY, nullptr, 4);

// Search
auto names = NPMRegistry::searchPackages("lodash", 10);
```

**Implemented enhancements:** Full JSON parser (`src/npm/JsonParser.h`), HTTPS/TLS (OpenSSL), in-memory cache with TTL, progress reporting for downloads, and parallel downloads (`downloadPackages`, `InstallOptions::parallelDownloads`).

---

## 4. Benchmark Runner

**Headers:** `src/benchmarking/BenchmarkRunner.h`  
**Implementation:** `src/benchmarking/BenchmarkRunner.cpp`

### API Summary

| Method | Purpose |
|--------|--------|
| `BenchmarkRunner::runBenchmark(file, options)` | Run a single benchmark script with protoJS; returns `BenchmarkResult`. |
| `BenchmarkRunner::runBenchmarkRepeated(file, iterations, options)` | Run benchmark N times; returns result with stats (mean, median, stddev, min, max). |
| `BenchmarkRunner::runSuite(suite)` | Run multiple benchmark files; returns `vector<BenchmarkResult>`. |
| `BenchmarkRunner::compareWithNodeJS(file, options)` | Run script with protoJS and Node.js; returns comparison result. |
| `BenchmarkRunner::generateReport(results, format)` | Generate text report (`"text"`, `"json"`, `"html"`). |
| `BenchmarkRunner::exportToJSON(results)` | Export results as JSON. |
| `BenchmarkRunner::exportToHTML(results)` | Export results as HTML. |
| `BenchmarkRunner::computeStats(samples)` | Compute mean, median, stddev, min, max from run times. |
| `BenchmarkRunner::detectRegressions(current, baseline, thresholdPercent)` | Return names of benchmarks that regressed (current time &gt; baseline × (1 + threshold/100)). |
| `BenchmarkRunner::saveBaseline(results, path)` | Save results to CSV for later regression comparison. |
| `BenchmarkRunner::loadBaseline(path)` | Load baseline results from CSV. |
| `BenchmarkRunner::runSuiteFromFile(configPath)` | **Automated execution:** run suite from config file (first line = name, rest = paths). |
| `BenchmarkRunner::runForCI(suiteConfig, baselinePath, thresholdPercent, reportPath)` | **CI/CD:** run suite, compare to baseline, write report; returns `CIRunResult` (success, regressed list, report). |

### Data Structures

- **BenchmarkResult**: `name`, `protojs_time_ms`, `nodejs_time_ms`, `speedup`, `memory_usage_bytes`, `success`, `error_message`; optional stats: `has_stats`, `median_ms`, `stddev_ms`, `min_ms`, `max_ms`, `iterations_run`.
- **BenchmarkStats**: `mean_ms`, `median_ms`, `stddev_ms`, `min_ms`, `max_ms`, `iterations`.
- **BenchmarkSuite**: `name`, `test_files` (vector of paths), `options` (map).
- **CIRunResult**: `success` (no regression), `regressed` (names), `report` (text).

### Usage Example (C++)

```cpp
#include "benchmarking/BenchmarkRunner.h"
#include <vector>
#include <map>

using namespace protojs;

// Single benchmark
std::map<std::string, std::string> options;
BenchmarkResult r = BenchmarkRunner::runBenchmark("/path/to/benchmark.js", options);
if (r.success)
    printf("protoJS: %.2f ms\n", r.protojs_time_ms);

// Suite
BenchmarkSuite suite;
suite.name = "Phase 6 suite";
suite.test_files = { "/path/to/phase6_benchmark_suite.js", "/path/to/minimal_test.js" };
auto results = BenchmarkRunner::runSuite(suite);
std::string report = BenchmarkRunner::generateReport(results, "text");

// Compare with Node.js
BenchmarkResult cmp = BenchmarkRunner::compareWithNodeJS("/path/to/benchmark.js", options);
// cmp.protojs_time_ms, cmp.nodejs_time_ms, cmp.speedup

// Statistical analysis: run N times, get mean/median/stddev
BenchmarkResult r2 = BenchmarkRunner::runBenchmarkRepeated("/path/to/benchmark.js", 5, options);
if (r2.has_stats) printf("mean=%.2f median=%.2f stddev=%.2f\n", r2.protojs_time_ms, r2.median_ms, r2.stddev_ms);

// Regression detection and CI
auto current = BenchmarkRunner::runSuiteFromFile("/path/to/suite_config.txt");
BenchmarkRunner::saveBaseline(current, "/path/to/baseline.csv");
auto baseline = BenchmarkRunner::loadBaseline("/path/to/baseline.csv");
auto regressed = BenchmarkRunner::detectRegressions(current, baseline, 10.0);  // 10% threshold
CIRunResult ci = BenchmarkRunner::runForCI("/path/to/suite_config.txt", "/path/to/baseline.csv", 10.0, "report.txt");
if (!ci.success) for (const auto& name : ci.regressed) printf("Regression: %s\n", name.c_str());
```

**Note:** Execution uses `runtime + " " + scriptPath` (e.g. `protojs script.js`, `node script.js`). Ensure `protojs` and `node` are on `PATH` when using comparison. **Config file format** for `runSuiteFromFile`: first non-empty non-comment line = suite name; following lines = benchmark paths (one per line). **CI/CD:** Call `runForCI(suiteConfig, baselinePath, thresholdPercent, reportPath)` from your build; exit with non-zero if `!ci.success`.

---

## 5. Node.js Test Runner (Compatibility)

**Headers:** `src/testing/NodeJSTestRunner.h`  
**Implementation:** `src/testing/NodeJSTestRunner.cpp`

### API Summary

| Method | Purpose |
|--------|--------|
| `NodeJSTestRunner::runTest(file, options)` | Run test with Node.js and protoJS; compare output; return `TestResult`. |
| `NodeJSTestRunner::runTestSuite(files, options)` | Run multiple tests; return `CompatibilityReport`. |
| `NodeJSTestRunner::runTestSuiteParallel(files, options, maxConcurrency)` | Run tests in parallel (default 4 concurrent). |
| `NodeJSTestRunner::checkModuleCompatibility(moduleName, files)` | Run suite and add module-specific recommendations. |
| `NodeJSTestRunner::generateReport(report, format)` | Text/JSON/HTML report. |
| `NodeJSTestRunner::exportToJSON(report)` | Export report as JSON. |
| `NodeJSTestRunner::exportToHTML(report)` | Export report as HTML. |
| `NodeJSTestRunner::identifyGaps(report)` | List failed test names/messages. |
| `NodeJSTestRunner::setTestCacheEnabled(bool)` | Enable/disable cache of Node.js expected output. |
| `NodeJSTestRunner::clearTestCache()` | Clear test cache. |
| `NodeJSTestRunner::getCoverageSummary(report)` | Return `CoverageSummary` (passed/failed lists, pass_rate). |
| `NodeJSTestRunner::exportCoverageReport(report, path, format)` | Write coverage summary (text or json). |
| `NodeJSTestRunner::runTestSuiteFromFile(configPath)` | **Automated:** run suite from config (first line = name, rest = paths). |
| `NodeJSTestRunner::runTestsForCI(configPath, minPassRate, reportPath)` | **CI:** run suite from config; success if pass_rate &gt;= minPassRate. |

### Data Structures

- **TestResult**: `test_name`, `passed`, `error_message`, `execution_time_ms`, `expected_output`, `actual_output`.
- **CompatibilityReport**: `total_tests`, `passed_tests`, `failed_tests`, `pass_rate`, `results`, `compatibility_issues`, `recommendations`.
- **CoverageSummary**: `total`, `passed`, `failed`, `pass_rate`, `passed_tests`, `failed_tests`.
- **TestCIRunResult**: `success` (pass_rate >= minPassRate), `report` (text).

### Usage Example (C++)

```cpp
#include "testing/NodeJSTestRunner.h"
#include <vector>
#include <map>

using namespace protojs;

// Single test (Node output = expected, protoJS output = actual, then compare)
std::map<std::string, std::string> options;
TestResult tr = NodeJSTestRunner::runTest("/path/to/hello_world.js", options);
bool ok = tr.passed;

// Suite
std::vector<std::string> files = { "/path/to/a.js", "/path/to/b.js" };
CompatibilityReport report = NodeJSTestRunner::runTestSuite(files, options);
double passRate = report.pass_rate;
std::string textReport = NodeJSTestRunner::generateReport(report, "text");

// Gaps
auto gaps = NodeJSTestRunner::identifyGaps(report);

// Test cache (avoid re-running Node for same file)
NodeJSTestRunner::setTestCacheEnabled(true);
report = NodeJSTestRunner::runTestSuite(files, options);
NodeJSTestRunner::clearTestCache();

// Parallel execution
report = NodeJSTestRunner::runTestSuiteParallel(files, options, 4);

// Coverage analysis
CoverageSummary cov = NodeJSTestRunner::getCoverageSummary(report);
NodeJSTestRunner::exportCoverageReport(report, "/path/to/coverage.txt", "text");

// Automated / CI
report = NodeJSTestRunner::runTestSuiteFromFile("/path/to/test_suite_config.txt");
TestCIRunResult ci = NodeJSTestRunner::runTestsForCI("/path/to/test_suite_config.txt", 80.0, "report.txt");
if (!ci.success) { /* pass rate below 80% */ }
```

**Note:** Commands are hardcoded as `./protojs` and `node`; for out-of-tree runs set up `PATH` or adjust the implementation to use configurable binaries.

---

## 6. Running Phase 6 Tests and Benchmarks

### Unit tests (C++)

Phase 6 has dedicated unit tests under `tests/unit/`:

- `test_semver.cpp` – Semver parse, compare, satisfies, findHighest, normalize.
- `test_npm_registry.cpp` – Registry constant and Semver integration.
- `test_benchmark_runner.cpp` – Report generation and optional integration run.
- `test_nodejs_test_runner.cpp` – Report generation, identifyGaps, optional integration run.

Build and run (from build dir):

```bash
cd build
cmake ..
make protojs_tests
ctest -R Phase6
# Or run integration tests (protojs and node on PATH):
# PROTOJS_TEST_PROJECT_ROOT=/path/to/protoJS ctest -R "BenchmarkRunner|NodeJSTestRunner"
```

### Performance benchmark suite (JS)

A small Phase 6 benchmark script is provided:

- **Script:** `tests/benchmarks/phase6_benchmark_suite.js`
- **Run with protoJS:** `./protojs tests/benchmarks/phase6_benchmark_suite.js`
- **Run with Node:** `node tests/benchmarks/phase6_benchmark_suite.js`

Use it with the BenchmarkRunner (C++) or your own runner to track Phase 6–related performance over time.

---

## 7. References

- **Phase 6 completion:** [PHASE6_COMPLETION.md](PHASE6_COMPLETION.md)
- **Technical audit:** [TECHNICAL_AUDIT_PHASE6.md](../TECHNICAL_AUDIT_PHASE6.md)
- **Performance testing:** [PERFORMANCE_TESTING.md](PERFORMANCE_TESTING.md)
- **Testing strategy:** [TESTING_STRATEGY.md](../TESTING_STRATEGY.md)
