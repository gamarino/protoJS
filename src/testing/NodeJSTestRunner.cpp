#include "NodeJSTestRunner.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <algorithm>
#include <mutex>
#include <future>

namespace protojs {

namespace {
    std::mutex g_cacheMutex;
    std::map<std::string, std::string> g_expectedOutputCache;
    bool g_testCacheEnabled = false;
}

void NodeJSTestRunner::setTestCacheEnabled(bool enabled) {
    g_testCacheEnabled = enabled;
}

void NodeJSTestRunner::clearTestCache() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_expectedOutputCache.clear();
}

TestResult NodeJSTestRunner::runTestInternal(const std::string& testFile, const std::map<std::string, std::string>& options) {
    TestResult result;
    result.test_name = testFile;
    result.passed = false;

    std::string expected;
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        if (g_testCacheEnabled) {
            auto it = g_expectedOutputCache.find(testFile);
            if (it != g_expectedOutputCache.end())
                expected = it->second;
        }
    }
    if (expected.empty()) {
        TestResult nodeResult = executeWithNodeJS(testFile, options);
        expected = nodeResult.actual_output;
        if (g_testCacheEnabled) {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            g_expectedOutputCache[testFile] = expected;
        }
    }
    result.expected_output = expected;

    TestResult protojsResult = executeWithProtoJS(testFile, options);
    result.actual_output = protojsResult.actual_output;
    result.execution_time_ms = protojsResult.execution_time_ms;
    result.passed = compareOutputs(result.expected_output, result.actual_output);
    if (!result.passed)
        result.error_message = "Output mismatch between Node.js and protoJS";
    return result;
}

TestResult NodeJSTestRunner::runTest(const std::string& testFile, const std::map<std::string, std::string>& options) {
    return runTestInternal(testFile, options);
}

CompatibilityReport NodeJSTestRunner::runTestSuite(const std::vector<std::string>& testFiles, const std::map<std::string, std::string>& options) {
    CompatibilityReport report;
    report.total_tests = static_cast<int>(testFiles.size());
    report.passed_tests = 0;
    report.failed_tests = 0;

    for (const auto& testFile : testFiles) {
        TestResult result = runTestInternal(testFile, options);
        report.results.push_back(result);
        if (result.passed)
            report.passed_tests++;
        else {
            report.failed_tests++;
            report.compatibility_issues.push_back(testFile + ": " + result.error_message);
        }
    }

    report.pass_rate = report.total_tests > 0 ?
        (static_cast<double>(report.passed_tests) / report.total_tests) * 100.0 : 0.0;

    if (report.pass_rate < 80.0)
        report.recommendations["priority"] = "High - Significant compatibility issues detected";
    else if (report.pass_rate < 95.0)
        report.recommendations["priority"] = "Medium - Some compatibility issues need attention";
    else
        report.recommendations["priority"] = "Low - Good compatibility, minor issues remain";

    return report;
}

CompatibilityReport NodeJSTestRunner::runTestSuiteParallel(const std::vector<std::string>& testFiles, const std::map<std::string, std::string>& options, size_t maxConcurrency) {
    CompatibilityReport report;
    report.total_tests = static_cast<int>(testFiles.size());
    report.passed_tests = 0;
    report.failed_tests = 0;
    report.results.resize(testFiles.size());
    if (maxConcurrency == 0) maxConcurrency = 1;

    size_t next = 0;
    while (next < testFiles.size()) {
        std::vector<std::future<TestResult>> batch;
        for (size_t i = 0; i < maxConcurrency && next < testFiles.size(); ++i) {
            size_t idx = next++;
            const std::string& testFile = testFiles[idx];
            batch.push_back(std::async(std::launch::async, [&testFile, &options, idx, &report]() {
                TestResult r = runTestInternal(testFile, options);
                report.results[idx] = r;
                return r;
            }));
        }
        for (auto& f : batch) (void)f.get();
    }

    for (const auto& result : report.results) {
        if (result.passed) report.passed_tests++;
        else {
            report.failed_tests++;
            report.compatibility_issues.push_back(result.test_name + ": " + result.error_message);
        }
    }
    report.pass_rate = report.total_tests > 0 ?
        (static_cast<double>(report.passed_tests) / report.total_tests) * 100.0 : 0.0;
    if (report.pass_rate < 80.0)
        report.recommendations["priority"] = "High - Significant compatibility issues detected";
    else if (report.pass_rate < 95.0)
        report.recommendations["priority"] = "Medium - Some compatibility issues need attention";
    else
        report.recommendations["priority"] = "Low - Good compatibility, minor issues remain";
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
    std::string normalizedExpected = expected;
    std::string normalizedActual = actual;
    auto trim = [](std::string& s) {
        size_t end = s.find_last_not_of(" \n\r\t");
        if (end == std::string::npos) s.clear();
        else s.erase(end + 1);
    };
    trim(normalizedExpected);
    trim(normalizedActual);
    return normalizedExpected == normalizedActual;
}

CoverageSummary NodeJSTestRunner::getCoverageSummary(const CompatibilityReport& report) {
    CoverageSummary s;
    s.total = report.total_tests;
    s.passed = report.passed_tests;
    s.failed = report.failed_tests;
    s.pass_rate = report.pass_rate;
    for (const auto& r : report.results) {
        if (r.passed) s.passed_tests.push_back(r.test_name);
        else s.failed_tests.push_back(r.test_name);
    }
    return s;
}

bool NodeJSTestRunner::exportCoverageReport(const CompatibilityReport& report, const std::string& path, const std::string& format) {
    std::ofstream f(path);
    if (!f) return false;
    CoverageSummary s = getCoverageSummary(report);
    if (format == "json") {
        f << "{\n  \"total\": " << s.total << ",\n  \"passed\": " << s.passed << ",\n  \"failed\": " << s.failed << ",\n  \"pass_rate\": " << s.pass_rate << ",\n";
        f << "  \"passed_tests\": [";
        for (size_t i = 0; i < s.passed_tests.size(); ++i) f << (i ? "," : "") << "\n    \"" << s.passed_tests[i] << "\"";
        f << "\n  ],\n  \"failed_tests\": [";
        for (size_t i = 0; i < s.failed_tests.size(); ++i) f << (i ? "," : "") << "\n    \"" << s.failed_tests[i] << "\"";
        f << "\n  ]\n}\n";
    } else {
        f << "=== Test Coverage Summary ===\n\n";
        f << "Total: " << s.total << " | Passed: " << s.passed << " | Failed: " << s.failed << " | Pass Rate: " << s.pass_rate << "%\n\n";
        f << "Passed tests:\n";
        for (const auto& n : s.passed_tests) f << "  - " << n << "\n";
        f << "Failed tests:\n";
        for (const auto& n : s.failed_tests) f << "  - " << n << "\n";
    }
    return true;
}

CompatibilityReport NodeJSTestRunner::runTestSuiteFromFile(const std::string& configPath) {
    std::ifstream f(configPath);
    if (!f) return CompatibilityReport();
    std::vector<std::string> testFiles;
    std::string line;
    bool haveName = false;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) start++;
        line = line.substr(start);
        if (line.empty() || (line.size() > 0 && line[0] == '#')) continue;
        if (!haveName) { haveName = true; continue; }
        testFiles.push_back(line);
    }
    if (testFiles.empty()) return CompatibilityReport();
    return runTestSuite(testFiles, {});
}

TestCIRunResult NodeJSTestRunner::runTestsForCI(const std::string& configPath, double minPassRate, const std::string& reportPath) {
    TestCIRunResult ci;
    CompatibilityReport report = runTestSuiteFromFile(configPath);
    if (report.total_tests == 0) {
        ci.success = false;
        ci.report = "No tests run (invalid config or empty suite)";
        return ci;
    }
    ci.report = generateReport(report, "text");
    ci.success = (report.pass_rate >= minPassRate);
    if (!reportPath.empty()) {
        std::ofstream out(reportPath);
        if (out) out << ci.report;
    }
    return ci;
}

} // namespace protojs
