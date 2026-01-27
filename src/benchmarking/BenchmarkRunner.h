#ifndef PROTOJS_BENCHMARKRUNNER_H
#define PROTOJS_BENCHMARKRUNNER_H

#include <string>
#include <map>
#include <vector>
#include <chrono>

namespace protojs {

struct BenchmarkResult {
    std::string name;
    double protojs_time_ms;
    double nodejs_time_ms;
    double speedup;
    size_t memory_usage_bytes;
    bool success;
    std::string error_message;
};

struct BenchmarkSuite {
    std::string name;
    std::vector<std::string> test_files;
    std::map<std::string, std::string> options;
};

class BenchmarkRunner {
public:
    // Run a single benchmark
    static BenchmarkResult runBenchmark(const std::string& benchmarkFile, const std::map<std::string, std::string>& options = {});
    
    // Run a benchmark suite
    static std::vector<BenchmarkResult> runSuite(const BenchmarkSuite& suite);
    
    // Compare with Node.js
    static BenchmarkResult compareWithNodeJS(const std::string& benchmarkFile, const std::map<std::string, std::string>& options = {});
    
    // Generate benchmark report
    static std::string generateReport(const std::vector<BenchmarkResult>& results, const std::string& format = "text");
    
    // Export to JSON
    static std::string exportToJSON(const std::vector<BenchmarkResult>& results);
    
    // Export to HTML
    static std::string exportToHTML(const std::vector<BenchmarkResult>& results);

private:
    // Execute benchmark script
    static double executeBenchmark(const std::string& scriptPath, const std::string& runtime, const std::map<std::string, std::string>& options);
    
    // Get memory usage
    static size_t getMemoryUsage();
};

} // namespace protojs

#endif // PROTOJS_BENCHMARKRUNNER_H
