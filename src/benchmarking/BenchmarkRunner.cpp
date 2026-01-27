#include "BenchmarkRunner.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <sys/resource.h>
#include <unistd.h>

namespace protojs {

BenchmarkResult BenchmarkRunner::runBenchmark(const std::string& benchmarkFile, const std::map<std::string, std::string>& options) {
    BenchmarkResult result;
    result.name = benchmarkFile;
    result.success = false;
    
    if (!std::ifstream(benchmarkFile).good()) {
        result.error_message = "Benchmark file not found: " + benchmarkFile;
        return result;
    }
    
    // Measure protoJS execution time
    auto start = std::chrono::high_resolution_clock::now();
    size_t memoryBefore = getMemoryUsage();
    
    double protojsTime = executeBenchmark(benchmarkFile, "protojs", options);
    
    auto end = std::chrono::high_resolution_clock::now();
    size_t memoryAfter = getMemoryUsage();
    
    result.protojs_time_ms = protojsTime;
    result.memory_usage_bytes = memoryAfter > memoryBefore ? memoryAfter - memoryBefore : 0;
    result.success = protojsTime > 0;
    
    if (!result.success) {
        result.error_message = "Failed to execute benchmark with protoJS";
    }
    
    return result;
}

std::vector<BenchmarkResult> BenchmarkRunner::runSuite(const BenchmarkSuite& suite) {
    std::vector<BenchmarkResult> results;
    
    for (const auto& testFile : suite.test_files) {
        BenchmarkResult result = runBenchmark(testFile, suite.options);
        results.push_back(result);
    }
    
    return results;
}

BenchmarkResult BenchmarkRunner::compareWithNodeJS(const std::string& benchmarkFile, const std::map<std::string, std::string>& options) {
    BenchmarkResult result;
    result.name = benchmarkFile;
    
    // Run with protoJS
    result.protojs_time_ms = executeBenchmark(benchmarkFile, "protojs", options);
    
    // Run with Node.js
    result.nodejs_time_ms = executeBenchmark(benchmarkFile, "node", options);
    
    if (result.protojs_time_ms > 0 && result.nodejs_time_ms > 0) {
        result.speedup = result.nodejs_time_ms / result.protojs_time_ms;
        result.success = true;
    } else {
        result.success = false;
        result.error_message = "Failed to execute benchmark with one or both runtimes";
    }
    
    return result;
}

std::string BenchmarkRunner::generateReport(const std::vector<BenchmarkResult>& results, const std::string& format) {
    if (format == "json") {
        return exportToJSON(results);
    } else if (format == "html") {
        return exportToHTML(results);
    }
    
    // Text format
    std::ostringstream oss;
    oss << "=== Benchmark Report ===\n\n";
    
    for (const auto& result : results) {
        oss << "Benchmark: " << result.name << "\n";
        oss << "  protoJS Time: " << result.protojs_time_ms << " ms\n";
        if (result.nodejs_time_ms > 0) {
            oss << "  Node.js Time: " << result.nodejs_time_ms << " ms\n";
            oss << "  Speedup: " << (result.speedup > 1 ? result.speedup : 1.0 / result.speedup) << "x ";
            oss << (result.speedup > 1 ? "faster" : "slower") << "\n";
        }
        oss << "  Memory: " << (result.memory_usage_bytes / 1024.0) << " KB\n";
        oss << "  Status: " << (result.success ? "PASS" : "FAIL") << "\n";
        if (!result.error_message.empty()) {
            oss << "  Error: " << result.error_message << "\n";
        }
        oss << "\n";
    }
    
    return oss.str();
}

std::string BenchmarkRunner::exportToJSON(const std::vector<BenchmarkResult>& results) {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"benchmarks\": [\n";
    
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        oss << "    {\n";
        oss << "      \"name\": \"" << result.name << "\",\n";
        oss << "      \"protojs_time_ms\": " << result.protojs_time_ms << ",\n";
        oss << "      \"nodejs_time_ms\": " << result.nodejs_time_ms << ",\n";
        oss << "      \"speedup\": " << result.speedup << ",\n";
        oss << "      \"memory_usage_bytes\": " << result.memory_usage_bytes << ",\n";
        oss << "      \"success\": " << (result.success ? "true" : "false") << "\n";
        if (i < results.size() - 1) {
            oss << "    },\n";
        } else {
            oss << "    }\n";
        }
    }
    
    oss << "  ]\n";
    oss << "}\n";
    
    return oss.str();
}

std::string BenchmarkRunner::exportToHTML(const std::vector<BenchmarkResult>& results) {
    std::ostringstream oss;
    oss << "<!DOCTYPE html>\n";
    oss << "<html><head><title>Benchmark Report</title>\n";
    oss << "<style>table{border-collapse:collapse;width:100%}th,td{border:1px solid #ddd;padding:8px;text-align:left}th{background-color:#4CAF50;color:white}</style>\n";
    oss << "</head><body><h1>Benchmark Report</h1><table>\n";
    oss << "<tr><th>Benchmark</th><th>protoJS (ms)</th><th>Node.js (ms)</th><th>Speedup</th><th>Memory (KB)</th><th>Status</th></tr>\n";
    
    for (const auto& result : results) {
        oss << "<tr>";
        oss << "<td>" << result.name << "</td>";
        oss << "<td>" << result.protojs_time_ms << "</td>";
        oss << "<td>" << result.nodejs_time_ms << "</td>";
        oss << "<td>" << (result.speedup > 1 ? result.speedup : 1.0 / result.speedup) << "x</td>";
        oss << "<td>" << (result.memory_usage_bytes / 1024.0) << "</td>";
        oss << "<td>" << (result.success ? "PASS" : "FAIL") << "</td>";
        oss << "</tr>\n";
    }
    
    oss << "</table></body></html>\n";
    return oss.str();
}

double BenchmarkRunner::executeBenchmark(const std::string& scriptPath, const std::string& runtime, const std::map<std::string, std::string>& options) {
    std::string command = runtime + " " + scriptPath;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    int result = std::system((command + " > /dev/null 2>&1").c_str());
    
    auto end = std::chrono::high_resolution_clock::now();
    
    if (result != 0) {
        return -1; // Error
    }
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    return duration.count();
}

size_t BenchmarkRunner::getMemoryUsage() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return usage.ru_maxrss * 1024; // Convert KB to bytes
    }
    return 0;
}

} // namespace protojs
