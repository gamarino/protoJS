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
    // Statistical analysis (when runBenchmarkRepeated used)
    bool has_stats = false;
    double median_ms = 0;
    double stddev_ms = 0;
    double min_ms = 0;
    double max_ms = 0;
    size_t iterations_run = 0;
};

struct BenchmarkStats {
    double mean_ms = 0;
    double median_ms = 0;
    double stddev_ms = 0;
    double min_ms = 0;
    double max_ms = 0;
    size_t iterations = 0;
};

struct BenchmarkSuite {
    std::string name;
    std::vector<std::string> test_files;
    std::map<std::string, std::string> options;
};

// CI result: success (no regression), regressed benchmark names, report text.
struct CIRunResult {
    bool success = true;
    std::vector<std::string> regressed;
    std::string report;
};

class BenchmarkRunner {
public:
    // Run a single benchmark
    static BenchmarkResult runBenchmark(const std::string& benchmarkFile, const std::map<std::string, std::string>& options = {});

    // Run benchmark multiple times and compute statistics (mean, median, stddev, min, max)
    static BenchmarkResult runBenchmarkRepeated(const std::string& benchmarkFile, size_t iterations, const std::map<std::string, std::string>& options = {});

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

    // Statistical analysis: compute stats from a list of run times
    static BenchmarkStats computeStats(const std::vector<double>& samples);

    // Regression detection: compare current results to baseline; return names of benchmarks that regressed (current time > baseline * (1 + threshold_percent/100))
    static std::vector<std::string> detectRegressions(const std::vector<BenchmarkResult>& current, const std::vector<BenchmarkResult>& baseline, double thresholdPercent = 10.0);

    // Baseline: save/load results to file (for regression comparison)
    static bool saveBaseline(const std::vector<BenchmarkResult>& results, const std::string& path);
    static std::vector<BenchmarkResult> loadBaseline(const std::string& path);

    // Automated execution: run suite from config file (text: first line = suite name, rest = benchmark paths, one per line)
    static std::vector<BenchmarkResult> runSuiteFromFile(const std::string& configPath);

    // CI/CD integration: run suite from config, compare to baseline, write report; returns success (no regression)
    static CIRunResult runForCI(const std::string& suiteConfigPath, const std::string& baselinePath, double thresholdPercent = 10.0, const std::string& reportPath = "");

private:
    // Execute benchmark script
    static double executeBenchmark(const std::string& scriptPath, const std::string& runtime, const std::map<std::string, std::string>& options);

    // Get memory usage
    static size_t getMemoryUsage();
};

} // namespace protojs

#endif // PROTOJS_BENCHMARKRUNNER_H
