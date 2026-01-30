#include "BenchmarkRunner.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <algorithm>
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

BenchmarkStats BenchmarkRunner::computeStats(const std::vector<double>& samples) {
    BenchmarkStats stats;
    if (samples.empty()) return stats;
    stats.iterations = samples.size();
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    stats.min_ms = sorted.front();
    stats.max_ms = sorted.back();
    double sum = 0;
    for (double v : samples) sum += v;
    stats.mean_ms = sum / static_cast<double>(samples.size());
    size_t mid = samples.size() / 2;
    stats.median_ms = (samples.size() % 2 == 1) ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2.0;
    double sqSum = 0;
    for (double v : samples) { double d = v - stats.mean_ms; sqSum += d * d; }
    stats.stddev_ms = (samples.size() > 1) ? std::sqrt(sqSum / static_cast<double>(samples.size() - 1)) : 0;
    return stats;
}

BenchmarkResult BenchmarkRunner::runBenchmarkRepeated(const std::string& benchmarkFile, size_t iterations, const std::map<std::string, std::string>& options) {
    BenchmarkResult result;
    result.name = benchmarkFile;
    result.success = false;
    if (iterations == 0) iterations = 1;
    if (!std::ifstream(benchmarkFile).good()) {
        result.error_message = "Benchmark file not found: " + benchmarkFile;
        return result;
    }
    std::vector<double> samples;
    for (size_t i = 0; i < iterations; ++i) {
        double t = executeBenchmark(benchmarkFile, "protojs", options);
        if (t > 0) samples.push_back(t);
    }
    if (samples.empty()) {
        result.error_message = "All runs failed";
        return result;
    }
    BenchmarkStats stats = computeStats(samples);
    result.protojs_time_ms = stats.mean_ms;
    result.has_stats = true;
    result.median_ms = stats.median_ms;
    result.stddev_ms = stats.stddev_ms;
    result.min_ms = stats.min_ms;
    result.max_ms = stats.max_ms;
    result.iterations_run = samples.size();
    result.success = true;
    result.memory_usage_bytes = getMemoryUsage();
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
        oss << "  protoJS Time: " << result.protojs_time_ms << " ms";
        if (result.has_stats) {
            oss << " (mean; n=" << result.iterations_run << " median=" << result.median_ms << " stddev=" << result.stddev_ms << " min=" << result.min_ms << " max=" << result.max_ms << ")";
        }
        oss << "\n";
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

std::vector<std::string> BenchmarkRunner::detectRegressions(const std::vector<BenchmarkResult>& current, const std::vector<BenchmarkResult>& baseline, double thresholdPercent) {
    std::map<std::string, double> baselineTime;
    for (const auto& r : baseline)
        if (r.success && r.protojs_time_ms > 0)
            baselineTime[r.name] = r.protojs_time_ms;
    std::vector<std::string> regressed;
    double factor = 1.0 + (thresholdPercent / 100.0);
    for (const auto& r : current) {
        if (!r.success || r.protojs_time_ms <= 0) continue;
        auto it = baselineTime.find(r.name);
        if (it == baselineTime.end()) continue;
        if (r.protojs_time_ms > it->second * factor)
            regressed.push_back(r.name);
    }
    return regressed;
}

static std::string escapeBaselineField(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\n' || c == '\r' || c == ',' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

static std::string unescapeBaselineField(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) { out += s[i + 1]; ++i; }
        else out += s[i];
    }
    return out;
}

bool BenchmarkRunner::saveBaseline(const std::vector<BenchmarkResult>& results, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << "name,protojs_time_ms,nodejs_time_ms,speedup,memory_usage_bytes,success\n";
    for (const auto& r : results) {
        f << escapeBaselineField(r.name) << ","
          << r.protojs_time_ms << ","
          << r.nodejs_time_ms << ","
          << r.speedup << ","
          << r.memory_usage_bytes << ","
          << (r.success ? "1" : "0") << "\n";
    }
    return true;
}

std::vector<BenchmarkResult> BenchmarkRunner::loadBaseline(const std::string& path) {
    std::vector<BenchmarkResult> results;
    std::ifstream f(path);
    if (!f) return results;
    std::string line;
    if (!std::getline(f, line) || line != "name,protojs_time_ms,nodejs_time_ms,speedup,memory_usage_bytes,success")
        return results;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        BenchmarkResult r;
        size_t pos = 0;
        auto next = [&](char delim) {
            std::string field;
            while (pos < line.size()) {
                if (line[pos] == '\\' && pos + 1 < line.size()) { field += line[pos + 1]; pos += 2; continue; }
                if (line[pos] == delim) { pos++; return field; }
                field += line[pos++];
            }
            return field;
        };
        r.name = unescapeBaselineField(next(','));
        std::string a = next(','), b = next(','), c = next(','), d = next(',');
        std::string e = (pos <= line.size()) ? line.substr(pos) : "";
        try {
            r.protojs_time_ms = std::stod(a);
            r.nodejs_time_ms = std::stod(b);
            r.speedup = std::stod(c);
            r.memory_usage_bytes = static_cast<size_t>(std::stoull(d));
            r.success = (e == "1" || e == "1\n" || e == "1\r");
        } catch (...) { continue; }
        results.push_back(r);
    }
    return results;
}

std::vector<BenchmarkResult> BenchmarkRunner::runSuiteFromFile(const std::string& configPath) {
    std::ifstream f(configPath);
    if (!f) return {};
    BenchmarkSuite suite;
    std::string line;
    bool haveName = false;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) start++;
        line = line.substr(start);
        if (line.empty() || (line.size() > 0 && line[0] == '#')) continue;
        if (!haveName) { suite.name = line; haveName = true; continue; }
        suite.test_files.push_back(line);
    }
    if (suite.test_files.empty()) return {};
    return runSuite(suite);
}

CIRunResult BenchmarkRunner::runForCI(const std::string& suiteConfigPath, const std::string& baselinePath, double thresholdPercent, const std::string& reportPath) {
    CIRunResult ci;
    std::vector<BenchmarkResult> current = runSuiteFromFile(suiteConfigPath);
    if (current.empty()) {
        ci.success = false;
        ci.report = "No benchmark results (invalid config or empty suite)";
        return ci;
    }
    std::vector<BenchmarkResult> baseline = loadBaseline(baselinePath);
    if (baseline.empty()) {
        ci.success = true;
        ci.report = generateReport(current, "text");
        ci.report = "No baseline found; saving current as baseline.\n" + ci.report;
        saveBaseline(current, baselinePath);
        if (!reportPath.empty()) {
            std::ofstream out(reportPath);
            if (out) out << ci.report;
        }
        return ci;
    }
    ci.regressed = detectRegressions(current, baseline, thresholdPercent);
    ci.success = ci.regressed.empty();
    ci.report = generateReport(current, "text");
    if (!ci.regressed.empty()) {
        ci.report += "\n=== REGRESSIONS (threshold " + std::to_string(thresholdPercent) + "%)\n";
        for (const auto& name : ci.regressed) ci.report += "  - " + name + "\n";
    }
    if (!reportPath.empty()) {
        std::ofstream out(reportPath);
        if (out) out << ci.report;
    }
    return ci;
}

} // namespace protojs
