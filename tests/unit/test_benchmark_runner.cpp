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
