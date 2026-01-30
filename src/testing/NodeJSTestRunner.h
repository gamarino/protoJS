#ifndef PROTOJS_NODEJSTESTRUNNER_H
#define PROTOJS_NODEJSTESTRUNNER_H

#include <string>
#include <vector>
#include <map>

namespace protojs {

struct TestResult {
    std::string test_name;
    bool passed;
    std::string error_message;
    double execution_time_ms;
    std::string expected_output;
    std::string actual_output;
};

struct CompatibilityReport {
    int total_tests;
    int passed_tests;
    int failed_tests;
    double pass_rate;
    std::vector<TestResult> results;
    std::vector<std::string> compatibility_issues;
    std::map<std::string, std::string> recommendations;
};

// Coverage summary: which tests passed/failed (for coverage analysis).
struct CoverageSummary {
    int total = 0;
    int passed = 0;
    int failed = 0;
    double pass_rate = 0;
    std::vector<std::string> passed_tests;
    std::vector<std::string> failed_tests;
};

// CI result: success (pass rate >= min), report text.
struct TestCIRunResult {
    bool success = true;
    std::string report;
};

class NodeJSTestRunner {
public:
    // Run a single Node.js test file
    static TestResult runTest(const std::string& testFile, const std::map<std::string, std::string>& options = {});

    // Run Node.js test suite (subset)
    static CompatibilityReport runTestSuite(const std::vector<std::string>& testFiles, const std::map<std::string, std::string>& options = {});

    // Run test suite in parallel (up to maxConcurrency tests at a time)
    static CompatibilityReport runTestSuiteParallel(const std::vector<std::string>& testFiles, const std::map<std::string, std::string>& options = {}, size_t maxConcurrency = 4);

    // Check compatibility for a specific module
    static CompatibilityReport checkModuleCompatibility(const std::string& moduleName, const std::vector<std::string>& testFiles);

    // Generate compatibility report
    static std::string generateReport(const CompatibilityReport& report, const std::string& format = "text");

    // Export to JSON
    static std::string exportToJSON(const CompatibilityReport& report);

    // Export to HTML
    static std::string exportToHTML(const CompatibilityReport& report);

    // Identify compatibility gaps
    static std::vector<std::string> identifyGaps(const CompatibilityReport& report);

    // Test caching: cache Node.js expected output to avoid re-running Node for same file
    static void setTestCacheEnabled(bool enabled);
    static void clearTestCache();

    // Coverage analysis: summary of passed/failed tests
    static CoverageSummary getCoverageSummary(const CompatibilityReport& report);
    static bool exportCoverageReport(const CompatibilityReport& report, const std::string& path, const std::string& format = "text");

    // Automated testing: run suite from config file (first line = name, rest = test paths)
    static CompatibilityReport runTestSuiteFromFile(const std::string& configPath);

    // CI integration: run suite from config, write report; success if pass_rate >= minPassRate
    static TestCIRunResult runTestsForCI(const std::string& configPath, double minPassRate = 80.0, const std::string& reportPath = "");

private:
    // Execute test with protoJS
    static TestResult executeWithProtoJS(const std::string& testFile, const std::map<std::string, std::string>& options);

    // Execute test with Node.js
    static TestResult executeWithNodeJS(const std::string& testFile, const std::map<std::string, std::string>& options);

    // Compare outputs
    static bool compareOutputs(const std::string& expected, const std::string& actual);

    // Run single test (used by parallel; may use cache for expected output)
    static TestResult runTestInternal(const std::string& testFile, const std::map<std::string, std::string>& options);
};

} // namespace protojs

#endif // PROTOJS_NODEJSTESTRUNNER_H
