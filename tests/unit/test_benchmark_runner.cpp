// Phase 6: Unit tests for BenchmarkRunner (report shape and suite execution)
#include <catch2/catch_all.hpp>
#include "../../src/benchmarking/BenchmarkRunner.h"
#include <cstdlib>
#include <fstream>
#include <filesystem>

using namespace protojs;

namespace {
    std::string getTestProjectRoot() {
        const char* root = std::getenv("PROTOJS_TEST_PROJECT_ROOT");
        if (root && root[0] != '\0') return root;
#ifdef PROTOJS_TEST_PROJECT_ROOT
        return PROTOJS_TEST_PROJECT_ROOT;
#else
        return "";
#endif
    }
    std::string getBenchmarkScript() {
        std::string root = getTestProjectRoot();
        if (root.empty()) return "";
        std::string path = root + "/tests/benchmarks/minimal_test.js";
        if (std::ifstream(path).good()) return path;
        return "";
    }
}

TEST_CASE("BenchmarkRunner::generateReport text format", "[BenchmarkRunner][Phase6]") {
    std::vector<BenchmarkResult> results;
    BenchmarkResult r;
    r.name = "test.js";
    r.protojs_time_ms = 10.0;
    r.nodejs_time_ms = 5.0;
    r.speedup = 0.5;
    r.memory_usage_bytes = 1024;
    r.success = true;
    results.push_back(r);

    std::string report = BenchmarkRunner::generateReport(results, "text");
    REQUIRE(report.find("Benchmark Report") != std::string::npos);
    REQUIRE(report.find("test.js") != std::string::npos);
    REQUIRE(report.find("10") != std::string::npos);
    REQUIRE(report.find("PASS") != std::string::npos);
}

TEST_CASE("BenchmarkRunner::exportToJSON", "[BenchmarkRunner][Phase6]") {
    std::vector<BenchmarkResult> results;
    BenchmarkResult r;
    r.name = "unit.js";
    r.protojs_time_ms = 1.0;
    r.success = true;
    results.push_back(r);

    std::string json = BenchmarkRunner::exportToJSON(results);
    REQUIRE(json.find("unit.js") != std::string::npos);
    REQUIRE(json.find("protojs_time_ms") != std::string::npos);
}

TEST_CASE("BenchmarkRunner::runSuite structure", "[BenchmarkRunner][Phase6]") {
    BenchmarkSuite suite;
    suite.name = "Phase6 suite";
    suite.test_files = {}; // empty
    auto results = BenchmarkRunner::runSuite(suite);
    REQUIRE(results.empty());
}

TEST_CASE("BenchmarkRunner::runBenchmark (integration)", "[BenchmarkRunner][Phase6][.integration]") {
    std::string script = getBenchmarkScript();
    if (script.empty()) {
        WARN("Skipping: PROTOJS_TEST_PROJECT_ROOT not set or minimal_test.js not found");
        return;
    }
    std::map<std::string, std::string> options;
    BenchmarkResult result = BenchmarkRunner::runBenchmark(script, options);
    REQUIRE(result.name == script);
    // May fail if protojs not in PATH
    if (result.success) {
        REQUIRE(result.protojs_time_ms >= 0);
    }
}

TEST_CASE("BenchmarkRunner::computeStats", "[BenchmarkRunner][Phase6]") {
    std::vector<double> samples = { 10.0, 20.0, 30.0, 40.0, 50.0 };
    auto stats = BenchmarkRunner::computeStats(samples);
    REQUIRE(stats.iterations == 5);
    REQUIRE(stats.mean_ms == 30.0);
    REQUIRE(stats.median_ms == 30.0);
    REQUIRE(stats.min_ms == 10.0);
    REQUIRE(stats.max_ms == 50.0);
    REQUIRE(stats.stddev_ms > 0);
}

TEST_CASE("BenchmarkRunner::detectRegressions", "[BenchmarkRunner][Phase6]") {
    std::vector<BenchmarkResult> baseline = { { "a.js", 100.0, 0, 0, 0, true, "", false, 0, 0, 0, 0, 0 } };
    std::vector<BenchmarkResult> current = { { "a.js", 120.0, 0, 0, 0, true, "", false, 0, 0, 0, 0, 0 } };
    auto regressed = BenchmarkRunner::detectRegressions(current, baseline, 10.0);
    REQUIRE(regressed.size() == 1);
    REQUIRE(regressed[0] == "a.js");
    current[0].protojs_time_ms = 109.0;
    regressed = BenchmarkRunner::detectRegressions(current, baseline, 10.0);
    REQUIRE(regressed.empty());
}

TEST_CASE("BenchmarkRunner::saveBaseline and loadBaseline", "[BenchmarkRunner][Phase6]") {
    std::vector<BenchmarkResult> results = { { "x.js", 5.0, 4.0, 0.8, 1024, true, "", false, 0, 0, 0, 0, 0 } };
    std::string path = "/tmp/protojs_baseline_test.csv";
    REQUIRE(BenchmarkRunner::saveBaseline(results, path));
    auto loaded = BenchmarkRunner::loadBaseline(path);
    REQUIRE(loaded.size() == 1);
    REQUIRE(loaded[0].name == "x.js");
    REQUIRE(loaded[0].protojs_time_ms == 5.0);
    REQUIRE(loaded[0].success);
    std::remove(path.c_str());
}

TEST_CASE("BenchmarkRunner::runSuiteFromFile empty or no file", "[BenchmarkRunner][Phase6]") {
    auto results = BenchmarkRunner::runSuiteFromFile("/nonexistent/suite_config.txt");
    REQUIRE(results.empty());
}

TEST_CASE("BenchmarkRunner::runForCI no baseline", "[BenchmarkRunner][Phase6]") {
    std::string root = getTestProjectRoot();
    std::string configPath = root + "/tests/benchmarks/suite_config.txt";
    std::string baselinePath = "/tmp/protojs_ci_baseline_nonexistent.csv";
    if (root.empty() || !std::ifstream(configPath).good()) {
        WARN("Skipping: suite_config.txt not found");
        return;
    }
    auto ci = BenchmarkRunner::runForCI(configPath, baselinePath, 10.0, "");
    REQUIRE(ci.success);
    bool hasReport = (ci.report.find("No baseline") != std::string::npos) || (ci.report.find("Benchmark") != std::string::npos);
    REQUIRE(hasReport);
}
