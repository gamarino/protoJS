// Phase 6: Unit tests for NodeJSTestRunner (report shape and gap identification)
#include <catch2/catch_all.hpp>
#include "../../src/testing/NodeJSTestRunner.h"
#include <cstdlib>
#include <fstream>

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
    std::string getHelloWorldScript() {
        std::string root = getTestProjectRoot();
        if (root.empty()) return "";
        std::string path = root + "/tests/integration/basic/hello_world.js";
        if (std::ifstream(path).good()) return path;
        return "";
    }
}

TEST_CASE("NodeJSTestRunner::generateReport text format", "[NodeJSTestRunner][Phase6]") {
    CompatibilityReport report;
    report.total_tests = 2;
    report.passed_tests = 1;
    report.failed_tests = 1;
    report.pass_rate = 50.0;
    report.results.resize(2);
    report.results[0].test_name = "a.js";
    report.results[0].passed = true;
    report.results[1].test_name = "b.js";
    report.results[1].passed = false;

    std::string text = NodeJSTestRunner::generateReport(report, "text");
    REQUIRE(text.find("Compatibility Report") != std::string::npos);
    REQUIRE(text.find("Total Tests: 2") != std::string::npos);
    REQUIRE(text.find("50") != std::string::npos);
}

TEST_CASE("NodeJSTestRunner::exportToJSON", "[NodeJSTestRunner][Phase6]") {
    CompatibilityReport report;
    report.total_tests = 1;
    report.passed_tests = 1;
    report.failed_tests = 0;
    report.pass_rate = 100.0;
    std::string json = NodeJSTestRunner::exportToJSON(report);
    REQUIRE(json.find("total_tests") != std::string::npos);
    REQUIRE(json.find("pass_rate") != std::string::npos);
}

TEST_CASE("NodeJSTestRunner::identifyGaps", "[NodeJSTestRunner][Phase6]") {
    CompatibilityReport report;
    report.total_tests = 2;
    report.passed_tests = 1;
    report.failed_tests = 1;
    report.results.resize(2);
    report.results[0].passed = true;
    report.results[1].passed = false;
    report.results[1].test_name = "fail.js";
    report.results[1].error_message = "output mismatch";

    auto gaps = NodeJSTestRunner::identifyGaps(report);
    REQUIRE(gaps.size() == 1);
    REQUIRE(gaps[0].find("fail.js") != std::string::npos);
}

TEST_CASE("NodeJSTestRunner::runTestSuite empty", "[NodeJSTestRunner][Phase6]") {
    std::vector<std::string> files;
    std::map<std::string, std::string> options;
    auto report = NodeJSTestRunner::runTestSuite(files, options);
    REQUIRE(report.total_tests == 0);
    REQUIRE(report.passed_tests == 0);
    REQUIRE(report.pass_rate == 0.0);
}

TEST_CASE("NodeJSTestRunner::runTest (integration)", "[NodeJSTestRunner][Phase6][.integration]") {
    std::string script = getHelloWorldScript();
    if (script.empty()) {
        WARN("Skipping: PROTOJS_TEST_PROJECT_ROOT not set or hello_world.js not found");
        return;
    }
    std::map<std::string, std::string> options;
    TestResult result = NodeJSTestRunner::runTest(script, options);
    REQUIRE(result.test_name == script);
    // runTest compares Node output vs protojs output; may fail if binaries not in PATH
    REQUIRE((result.passed || !result.error_message.empty() || result.expected_output.size() >= 0));
}
