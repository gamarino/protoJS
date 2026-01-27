#include "NodeJSTestRunner.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <algorithm>

namespace protojs {

TestResult NodeJSTestRunner::runTest(const std::string& testFile, const std::map<std::string, std::string>& options) {
    TestResult result;
    result.test_name = testFile;
    result.passed = false;
    
    // Run with Node.js to get expected output
    TestResult nodeResult = executeWithNodeJS(testFile, options);
    result.expected_output = nodeResult.actual_output;
    
    // Run with protoJS
    TestResult protojsResult = executeWithProtoJS(testFile, options);
    result.actual_output = protojsResult.actual_output;
    result.execution_time_ms = protojsResult.execution_time_ms;
    
    // Compare outputs
    result.passed = compareOutputs(result.expected_output, result.actual_output);
    
    if (!result.passed) {
        result.error_message = "Output mismatch between Node.js and protoJS";
    }
    
    return result;
}

CompatibilityReport NodeJSTestRunner::runTestSuite(const std::vector<std::string>& testFiles, const std::map<std::string, std::string>& options) {
    CompatibilityReport report;
    report.total_tests = testFiles.size();
    report.passed_tests = 0;
    report.failed_tests = 0;
    
    for (const auto& testFile : testFiles) {
        TestResult result = runTest(testFile, options);
        report.results.push_back(result);
        
        if (result.passed) {
            report.passed_tests++;
        } else {
            report.failed_tests++;
            report.compatibility_issues.push_back(testFile + ": " + result.error_message);
        }
    }
    
    report.pass_rate = report.total_tests > 0 ? 
        (static_cast<double>(report.passed_tests) / report.total_tests) * 100.0 : 0.0;
    
    // Generate recommendations
    if (report.pass_rate < 80.0) {
        report.recommendations["priority"] = "High - Significant compatibility issues detected";
    } else if (report.pass_rate < 95.0) {
        report.recommendations["priority"] = "Medium - Some compatibility issues need attention";
    } else {
        report.recommendations["priority"] = "Low - Good compatibility, minor issues remain";
    }
    
    return report;
}

CompatibilityReport NodeJSTestRunner::checkModuleCompatibility(const std::string& moduleName, const std::vector<std::string>& testFiles) {
    CompatibilityReport report = runTestSuite(testFiles);
    
    // Add module-specific recommendations
    if (report.pass_rate < 100.0) {
        report.recommendations[moduleName] = "Module compatibility: " + std::to_string(report.pass_rate) + "%";
    }
    
    return report;
}

std::string NodeJSTestRunner::generateReport(const CompatibilityReport& report, const std::string& format) {
    if (format == "json") {
        return exportToJSON(report);
    } else if (format == "html") {
        return exportToHTML(report);
    }
    
    // Text format
    std::ostringstream oss;
    oss << "=== Node.js Compatibility Report ===\n\n";
    oss << "Total Tests: " << report.total_tests << "\n";
    oss << "Passed: " << report.passed_tests << "\n";
    oss << "Failed: " << report.failed_tests << "\n";
    oss << "Pass Rate: " << report.pass_rate << "%\n\n";
    
    if (!report.compatibility_issues.empty()) {
        oss << "Compatibility Issues:\n";
        for (const auto& issue : report.compatibility_issues) {
            oss << "  - " << issue << "\n";
        }
        oss << "\n";
    }
    
    if (!report.recommendations.empty()) {
        oss << "Recommendations:\n";
        for (const auto& pair : report.recommendations) {
            oss << "  - " << pair.first << ": " << pair.second << "\n";
        }
    }
    
    return oss.str();
}

std::string NodeJSTestRunner::exportToJSON(const CompatibilityReport& report) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"total_tests\": " << report.total_tests << ",\n";
    oss << "  \"passed_tests\": " << report.passed_tests << ",\n";
    oss << "  \"failed_tests\": " << report.failed_tests << ",\n";
    oss << "  \"pass_rate\": " << report.pass_rate << ",\n";
    oss << "  \"results\": [\n";
    
    for (size_t i = 0; i < report.results.size(); ++i) {
        const auto& result = report.results[i];
        oss << "    {\n";
        oss << "      \"test_name\": \"" << result.test_name << "\",\n";
        oss << "      \"passed\": " << (result.passed ? "true" : "false") << ",\n";
        oss << "      \"execution_time_ms\": " << result.execution_time_ms << ",\n";
        if (!result.error_message.empty()) {
            oss << "      \"error_message\": \"" << result.error_message << "\",\n";
        }
        if (i < report.results.size() - 1) {
            oss << "    },\n";
        } else {
            oss << "    }\n";
        }
    }
    
    oss << "  ]\n";
    oss << "}\n";
    
    return oss.str();
}

std::string NodeJSTestRunner::exportToHTML(const CompatibilityReport& report) {
    std::ostringstream oss;
    oss << "<!DOCTYPE html>\n";
    oss << "<html><head><title>Node.js Compatibility Report</title>\n";
    oss << "<style>table{border-collapse:collapse;width:100%}th,td{border:1px solid #ddd;padding:8px;text-align:left}th{background-color:#4CAF50;color:white}.pass{color:green}.fail{color:red}</style>\n";
    oss << "</head><body><h1>Node.js Compatibility Report</h1>\n";
    oss << "<p>Total Tests: " << report.total_tests << " | Passed: " << report.passed_tests << " | Failed: " << report.failed_tests << " | Pass Rate: " << report.pass_rate << "%</p>\n";
    oss << "<table><tr><th>Test</th><th>Status</th><th>Time (ms)</th><th>Error</th></tr>\n";
    
    for (const auto& result : report.results) {
        oss << "<tr>";
        oss << "<td>" << result.test_name << "</td>";
        oss << "<td class=\"" << (result.passed ? "pass" : "fail") << "\">" << (result.passed ? "PASS" : "FAIL") << "</td>";
        oss << "<td>" << result.execution_time_ms << "</td>";
        oss << "<td>" << (result.error_message.empty() ? "-" : result.error_message) << "</td>";
        oss << "</tr>\n";
    }
    
    oss << "</table></body></html>\n";
    return oss.str();
}

std::vector<std::string> NodeJSTestRunner::identifyGaps(const CompatibilityReport& report) {
    std::vector<std::string> gaps;
    
    for (const auto& result : report.results) {
        if (!result.passed) {
            gaps.push_back(result.test_name + ": " + result.error_message);
        }
    }
    
    return gaps;
}

TestResult NodeJSTestRunner::executeWithProtoJS(const std::string& testFile, const std::map<std::string, std::string>& options) {
    TestResult result;
    result.test_name = testFile;
    
    std::string command = "./protojs " + testFile;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.passed = false;
        result.error_message = "Failed to execute test with protoJS";
        return result;
    }
    
    char buffer[4096];
    std::ostringstream output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output << buffer;
    }
    
    int status = pclose(pipe);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    result.actual_output = output.str();
    result.execution_time_ms = duration.count();
    result.passed = (status == 0);
    
    return result;
}

TestResult NodeJSTestRunner::executeWithNodeJS(const std::string& testFile, const std::map<std::string, std::string>& options) {
    TestResult result;
    result.test_name = testFile;
    
    std::string command = "node " + testFile;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.passed = false;
        result.error_message = "Failed to execute test with Node.js";
        return result;
    }
    
    char buffer[4096];
    std::ostringstream output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output << buffer;
    }
    
    int status = pclose(pipe);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    result.actual_output = output.str();
    result.execution_time_ms = duration.count();
    result.passed = (status == 0);
    
    return result;
}

bool NodeJSTestRunner::compareOutputs(const std::string& expected, const std::string& actual) {
    // Normalize whitespace for comparison
    std::string normalizedExpected = expected;
    std::string normalizedActual = actual;
    
    // Remove trailing whitespace
    normalizedExpected.erase(normalizedExpected.find_last_not_of(" \n\r\t") + 1);
    normalizedActual.erase(normalizedActual.find_last_not_of(" \n\r\t") + 1);
    
    return normalizedExpected == normalizedActual;
}

} // namespace protojs
