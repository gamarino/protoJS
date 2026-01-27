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

class NodeJSTestRunner {
public:
    // Run a single Node.js test file
    static TestResult runTest(const std::string& testFile, const std::map<std::string, std::string>& options = {});
    
    // Run Node.js test suite (subset)
    static CompatibilityReport runTestSuite(const std::vector<std::string>& testFiles, const std::map<std::string, std::string>& options = {});
    
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

private:
    // Execute test with protoJS
    static TestResult executeWithProtoJS(const std::string& testFile, const std::map<std::string, std::string>& options);
    
    // Execute test with Node.js
    static TestResult executeWithNodeJS(const std::string& testFile, const std::map<std::string, std::string>& options);
    
    // Compare outputs
    static bool compareOutputs(const std::string& expected, const std::string& actual);
};

} // namespace protojs

#endif // PROTOJS_NODEJSTESTRUNNER_H
