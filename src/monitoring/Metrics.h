#ifndef PROTOJS_METRICS_H
#define PROTOJS_METRICS_H

#include "headers/protoCore.h"
#include <mutex>

namespace protojs {

/**
 * @brief Histogram statistics (using protoCore objects)
 */
struct HistogramStats {
    const proto::ProtoList* buckets;  // List of bucket thresholds as doubles
    const proto::ProtoList* counts;  // List of counts per bucket as integers
    const proto::ProtoObject* sum;   // Sum as double
    const proto::ProtoObject* count; // Total count as integer
};

/**
 * @brief Metrics provides performance metrics collection for protoJS using only protoCore objects
 */
class Metrics {
public:
    /**
     * @brief Increment a counter
     */
    static void incrementCounter(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoObject* value, const proto::ProtoSparseList* labels = nullptr);
    
    /**
     * @brief Get counter value
     */
    static const proto::ProtoObject* getCounter(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoSparseList* labels = nullptr);
    
    /**
     * @brief Set a gauge value
     */
    static void setGauge(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoObject* value, const proto::ProtoSparseList* labels = nullptr);
    
    /**
     * @brief Add to a gauge value
     */
    static void addGauge(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoObject* value, const proto::ProtoSparseList* labels = nullptr);
    
    /**
     * @brief Get gauge value
     */
    static const proto::ProtoObject* getGauge(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoSparseList* labels = nullptr);
    
    /**
     * @brief Record a histogram value
     */
    static void recordHistogram(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoObject* value, const proto::ProtoSparseList* labels = nullptr);
    
    /**
     * @brief Get histogram statistics
     */
    static HistogramStats getHistogram(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoSparseList* labels = nullptr);
    
    /**
     * @brief Metrics snapshot structure (using protoCore objects)
     */
    struct MetricsSnapshot {
        const proto::ProtoSparseList* counters;  // Key: metric name, Value: counter value
        const proto::ProtoSparseList* gauges;   // Key: metric name, Value: gauge value
        const proto::ProtoSparseList* histograms; // Key: metric name, Value: HistogramStats object
    };
    
    /**
     * @brief Get current metrics snapshot
     */
    static MetricsSnapshot getSnapshot(proto::ProtoContext* pContext);
    
    /**
     * @brief Export metrics as JSON (returns ProtoString)
     */
    static const proto::ProtoString* exportJSON(proto::ProtoContext* pContext);
    
    /**
     * @brief Export metrics as Prometheus format (returns ProtoString)
     */
    static const proto::ProtoString* exportPrometheus(proto::ProtoContext* pContext);

private:
    /**
     * @brief Create a key for metric name + labels
     */
    static const proto::ProtoString* makeKey(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoSparseList* labels);
    
    // Store metrics in ProtoSparseList
    // Key: metric name + labels hash (as ProtoString hash)
    // Value: metric value (counter/gauge) or HistogramStats object
    static const proto::ProtoSparseList* countersStorage;
    static const proto::ProtoSparseList* gaugesStorage;
    static const proto::ProtoSparseList* histogramsStorage;
    static std::mutex metricsMutex;
    
    /**
     * @brief Get or create storage
     */
    static const proto::ProtoSparseList* getStorage(proto::ProtoContext* pContext, const proto::ProtoSparseList** storage);
    
    /**
     * @brief Set storage
     */
    static void setStorage(proto::ProtoContext* pContext, const proto::ProtoSparseList** storage, const proto::ProtoSparseList* newStorage);
};

} // namespace protojs

#endif // PROTOJS_METRICS_H
