#ifndef PROTOJS_METRICS_H
#define PROTOJS_METRICS_H

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <chrono>

namespace protojs {

/**
 * @brief Histogram statistics
 */
struct HistogramStats {
    std::vector<double> buckets;
    std::vector<size_t> counts;
    double sum = 0.0;
    size_t count = 0;
};

/**
 * @brief Metrics provides performance metrics collection for protoJS
 */
class Metrics {
public:
    /**
     * @brief Increment a counter
     */
    static void incrementCounter(const std::string& name, double value = 1.0, const std::map<std::string, std::string>& labels = {});
    
    /**
     * @brief Get counter value
     */
    static double getCounter(const std::string& name, const std::map<std::string, std::string>& labels = {});
    
    /**
     * @brief Set a gauge value
     */
    static void setGauge(const std::string& name, double value, const std::map<std::string, std::string>& labels = {});
    
    /**
     * @brief Add to a gauge value
     */
    static void addGauge(const std::string& name, double value, const std::map<std::string, std::string>& labels = {});
    
    /**
     * @brief Get gauge value
     */
    static double getGauge(const std::string& name, const std::map<std::string, std::string>& labels = {});
    
    /**
     * @brief Record a histogram value
     */
    static void recordHistogram(const std::string& name, double value, const std::map<std::string, std::string>& labels = {});
    
    /**
     * @brief Get histogram statistics
     */
    static HistogramStats getHistogram(const std::string& name, const std::map<std::string, std::string>& labels = {});
    
    /**
     * @brief Metrics snapshot structure
     */
    struct MetricsSnapshot {
        std::map<std::string, double> counters;
        std::map<std::string, double> gauges;
        std::map<std::string, HistogramStats> histograms;
    };
    
    /**
     * @brief Get current metrics snapshot
     */
    static MetricsSnapshot getSnapshot();
    
    /**
     * @brief Export metrics as JSON
     */
    static std::string exportJSON();
    
    /**
     * @brief Export metrics as Prometheus format
     */
    static std::string exportPrometheus();

private:
    static std::string makeKey(const std::string& name, const std::map<std::string, std::string>& labels);
    
    static std::map<std::string, double> counters;
    static std::map<std::string, double> gauges;
    static std::map<std::string, HistogramStats> histograms;
    static std::mutex metricsMutex;
};

} // namespace protojs

#endif // PROTOJS_METRICS_H
