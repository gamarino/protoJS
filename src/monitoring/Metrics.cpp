#include "Metrics.h"
#include <sstream>
#include <algorithm>

namespace protojs {

// Static member definitions
std::map<std::string, double> Metrics::counters;
std::map<std::string, double> Metrics::gauges;
std::map<std::string, HistogramStats> Metrics::histograms;
std::mutex Metrics::metricsMutex;

// Default histogram buckets
static const std::vector<double> DEFAULT_BUCKETS = {0.1, 0.5, 1.0, 5.0, 10.0, 50.0, 100.0, 500.0, 1000.0};

void Metrics::incrementCounter(const std::string& name, double value, const std::map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    std::string key = makeKey(name, labels);
    counters[key] += value;
}

double Metrics::getCounter(const std::string& name, const std::map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    std::string key = makeKey(name, labels);
    auto it = counters.find(key);
    return (it != counters.end()) ? it->second : 0.0;
}

void Metrics::setGauge(const std::string& name, double value, const std::map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    std::string key = makeKey(name, labels);
    gauges[key] = value;
}

void Metrics::addGauge(const std::string& name, double value, const std::map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    std::string key = makeKey(name, labels);
    gauges[key] += value;
}

double Metrics::getGauge(const std::string& name, const std::map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    std::string key = makeKey(name, labels);
    auto it = gauges.find(key);
    return (it != gauges.end()) ? it->second : 0.0;
}

void Metrics::recordHistogram(const std::string& name, double value, const std::map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    std::string key = makeKey(name, labels);
    
    HistogramStats& stats = histograms[key];
    if (stats.buckets.empty()) {
        stats.buckets = DEFAULT_BUCKETS;
        stats.counts.resize(DEFAULT_BUCKETS.size(), 0);
    }
    
    stats.sum += value;
    stats.count++;
    
    // Find appropriate bucket
    for (size_t i = 0; i < stats.buckets.size(); i++) {
        if (value <= stats.buckets[i]) {
            stats.counts[i]++;
            break;
        }
    }
}

HistogramStats Metrics::getHistogram(const std::string& name, const std::map<std::string, std::string>& labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    std::string key = makeKey(name, labels);
    auto it = histograms.find(key);
    return (it != histograms.end()) ? it->second : HistogramStats();
}

Metrics::MetricsSnapshot Metrics::getSnapshot() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    MetricsSnapshot snapshot;
    snapshot.counters = counters;
    snapshot.gauges = gauges;
    snapshot.histograms = histograms;
    return snapshot;
}

std::string Metrics::exportJSON() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    std::ostringstream oss;
    
    oss << "{\n"
        << "  \"counters\": {\n";
    bool first = true;
    for (const auto& [key, value] : counters) {
        if (!first) oss << ",\n";
        oss << "    \"" << key << "\": " << value;
        first = false;
    }
    oss << "\n  },\n"
        << "  \"gauges\": {\n";
    first = true;
    for (const auto& [key, value] : gauges) {
        if (!first) oss << ",\n";
        oss << "    \"" << key << "\": " << value;
        first = false;
    }
    oss << "\n  },\n"
        << "  \"histograms\": {\n";
    first = true;
    for (const auto& [key, stats] : histograms) {
        if (!first) oss << ",\n";
        oss << "    \"" << key << "\": {\n"
            << "      \"sum\": " << stats.sum << ",\n"
            << "      \"count\": " << stats.count << "\n"
            << "    }";
        first = false;
    }
    oss << "\n  }\n"
        << "}";
    
    return oss.str();
}

std::string Metrics::exportPrometheus() {
    std::lock_guard<std::mutex> lock(metricsMutex);
    std::ostringstream oss;
    
    // Export counters
    for (const auto& [key, value] : counters) {
        oss << "# TYPE " << key << " counter\n";
        oss << key << " " << value << "\n";
    }
    
    // Export gauges
    for (const auto& [key, value] : gauges) {
        oss << "# TYPE " << key << " gauge\n";
        oss << key << " " << value << "\n";
    }
    
    // Export histograms
    for (const auto& [key, stats] : histograms) {
        oss << "# TYPE " << key << " histogram\n";
        for (size_t i = 0; i < stats.buckets.size(); i++) {
            oss << key << "_bucket{le=\"" << stats.buckets[i] << "\"} " << stats.counts[i] << "\n";
        }
        oss << key << "_sum " << stats.sum << "\n";
        oss << key << "_count " << stats.count << "\n";
    }
    
    return oss.str();
}

std::string Metrics::makeKey(const std::string& name, const std::map<std::string, std::string>& labels) {
    if (labels.empty()) {
        return name;
    }
    
    std::ostringstream oss;
    oss << name << "{";
    bool first = true;
    for (const auto& [key, value] : labels) {
        if (!first) oss << ",";
        oss << key << "=\"" << value << "\"";
        first = false;
    }
    oss << "}";
    return oss.str();
}

} // namespace protojs
