#include "Metrics.h"
#include <mutex>

namespace protojs {

// Static member definitions
const proto::ProtoSparseList* Metrics::countersStorage = nullptr;
const proto::ProtoSparseList* Metrics::gaugesStorage = nullptr;
const proto::ProtoSparseList* Metrics::histogramsStorage = nullptr;
std::mutex Metrics::metricsMutex;

// Default histogram buckets as ProtoList
static const proto::ProtoList* getDefaultBuckets(proto::ProtoContext* pContext) {
    static const proto::ProtoList* defaultBuckets = nullptr;
    if (!defaultBuckets) {
        defaultBuckets = pContext->newList();
        defaultBuckets = defaultBuckets->appendLast(pContext, pContext->fromDouble(0.1));
        defaultBuckets = defaultBuckets->appendLast(pContext, pContext->fromDouble(0.5));
        defaultBuckets = defaultBuckets->appendLast(pContext, pContext->fromDouble(1.0));
        defaultBuckets = defaultBuckets->appendLast(pContext, pContext->fromDouble(5.0));
        defaultBuckets = defaultBuckets->appendLast(pContext, pContext->fromDouble(10.0));
        defaultBuckets = defaultBuckets->appendLast(pContext, pContext->fromDouble(50.0));
        defaultBuckets = defaultBuckets->appendLast(pContext, pContext->fromDouble(100.0));
        defaultBuckets = defaultBuckets->appendLast(pContext, pContext->fromDouble(500.0));
        defaultBuckets = defaultBuckets->appendLast(pContext, pContext->fromDouble(1000.0));
    }
    return defaultBuckets;
}

void Metrics::incrementCounter(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoObject* value, const proto::ProtoSparseList* labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    const proto::ProtoSparseList* storage = getStorage(pContext, &countersStorage);
    const proto::ProtoString* key = makeKey(pContext, name, labels);
    unsigned long keyHash = key->getHash(pContext);
    
    const proto::ProtoObject* currentValue = storage->has(pContext, keyHash) 
        ? storage->getAt(pContext, keyHash) 
        : pContext->fromDouble(0.0);
    
    double current = currentValue->asDouble(pContext);
    double increment = value ? value->asDouble(pContext) : 1.0;
    const proto::ProtoObject* newValue = pContext->fromDouble(current + increment);
    
    storage = storage->setAt(pContext, keyHash, newValue);
    setStorage(pContext, &countersStorage, storage);
}

const proto::ProtoObject* Metrics::getCounter(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoSparseList* labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    const proto::ProtoSparseList* storage = getStorage(pContext, &countersStorage);
    const proto::ProtoString* key = makeKey(pContext, name, labels);
    unsigned long keyHash = key->getHash(pContext);
    
    if (storage->has(pContext, keyHash)) {
        return storage->getAt(pContext, keyHash);
    }
    
    return pContext->fromDouble(0.0);
}

void Metrics::setGauge(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoObject* value, const proto::ProtoSparseList* labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    const proto::ProtoSparseList* storage = getStorage(pContext, &gaugesStorage);
    const proto::ProtoString* key = makeKey(pContext, name, labels);
    unsigned long keyHash = key->getHash(pContext);
    
    storage = storage->setAt(pContext, keyHash, value);
    setStorage(pContext, &gaugesStorage, storage);
}

void Metrics::addGauge(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoObject* value, const proto::ProtoSparseList* labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    const proto::ProtoSparseList* storage = getStorage(pContext, &gaugesStorage);
    const proto::ProtoString* key = makeKey(pContext, name, labels);
    unsigned long keyHash = key->getHash(pContext);
    
    const proto::ProtoObject* currentValue = storage->has(pContext, keyHash)
        ? storage->getAt(pContext, keyHash)
        : pContext->fromDouble(0.0);
    
    double current = currentValue->asDouble(pContext);
    double add = value->asDouble(pContext);
    const proto::ProtoObject* newValue = pContext->fromDouble(current + add);
    
    storage = storage->setAt(pContext, keyHash, newValue);
    setStorage(pContext, &gaugesStorage, storage);
}

const proto::ProtoObject* Metrics::getGauge(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoSparseList* labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    const proto::ProtoSparseList* storage = getStorage(pContext, &gaugesStorage);
    const proto::ProtoString* key = makeKey(pContext, name, labels);
    unsigned long keyHash = key->getHash(pContext);
    
    if (storage->has(pContext, keyHash)) {
        return storage->getAt(pContext, keyHash);
    }
    
    return pContext->fromDouble(0.0);
}

void Metrics::recordHistogram(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoObject* value, const proto::ProtoSparseList* labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    const proto::ProtoSparseList* storage = getStorage(pContext, &histogramsStorage);
    const proto::ProtoString* key = makeKey(pContext, name, labels);
    unsigned long keyHash = key->getHash(pContext);
    
    // Get or create histogram stats object
    const proto::ProtoObject* statsObj = storage->has(pContext, keyHash)
        ? storage->getAt(pContext, keyHash)
        : nullptr;
    
    if (!statsObj) {
        // Create new HistogramStats object
        const proto::ProtoObject* newStats = pContext->newObject(true);
        const proto::ProtoString* bucketsKey = pContext->fromUTF8String("buckets")->asString(pContext);
        const proto::ProtoString* countsKey = pContext->fromUTF8String("counts")->asString(pContext);
        const proto::ProtoString* sumKey = pContext->fromUTF8String("sum")->asString(pContext);
        const proto::ProtoString* countKey = pContext->fromUTF8String("count")->asString(pContext);
        
        const proto::ProtoList* defaultBuckets = getDefaultBuckets(pContext);
        const proto::ProtoList* emptyCounts = pContext->newList();
        // Initialize counts list with zeros
        for (unsigned long i = 0; i < defaultBuckets->getSize(pContext); i++) {
            emptyCounts = emptyCounts->appendLast(pContext, pContext->fromInteger(0));
        }
        
        newStats = newStats->setAttribute(pContext, bucketsKey, defaultBuckets->asObject(pContext));
        newStats = newStats->setAttribute(pContext, countsKey, emptyCounts->asObject(pContext));
        newStats = newStats->setAttribute(pContext, sumKey, pContext->fromDouble(0.0));
        newStats = newStats->setAttribute(pContext, countKey, pContext->fromInteger(0));
        
        statsObj = newStats;
        storage = storage->setAt(pContext, keyHash, statsObj);
        setStorage(pContext, &histogramsStorage, storage);
    }
    
    // Update histogram stats
    double val = value->asDouble(pContext);
    const proto::ProtoString* sumKey = pContext->fromUTF8String("sum")->asString(pContext);
    const proto::ProtoString* countKey = pContext->fromUTF8String("count")->asString(pContext);
    const proto::ProtoString* countsKey = pContext->fromUTF8String("counts")->asString(pContext);
    const proto::ProtoString* bucketsKey = pContext->fromUTF8String("buckets")->asString(pContext);
    
    // Update sum
    const proto::ProtoObject* currentSumObj = statsObj->getAttribute(pContext, sumKey);
    double currentSum = currentSumObj->asDouble(pContext);
    statsObj = statsObj->setAttribute(pContext, sumKey, pContext->fromDouble(currentSum + val));
    
    // Update count
    const proto::ProtoObject* currentCountObj = statsObj->getAttribute(pContext, countKey);
    long long currentCount = currentCountObj->asLong(pContext);
    statsObj = statsObj->setAttribute(pContext, countKey, pContext->fromInteger(currentCount + 1));
    
    // Update bucket counts
    const proto::ProtoObject* bucketsObj = statsObj->getAttribute(pContext, bucketsKey);
    const proto::ProtoList* buckets = bucketsObj->asList(pContext);
    const proto::ProtoObject* countsObj = statsObj->getAttribute(pContext, countsKey);
    const proto::ProtoList* counts = countsObj->asList(pContext);
    
    // Find appropriate bucket and increment
    const proto::ProtoList* newCounts = counts;
    for (unsigned long i = 0; i < buckets->getSize(pContext); i++) {
        const proto::ProtoObject* bucketObj = buckets->getAt(pContext, i);
        double bucket = bucketObj->asDouble(pContext);
        if (val <= bucket) {
            const proto::ProtoObject* countObj = counts->getAt(pContext, i);
            long long count = countObj->asLong(pContext);
            // Replace count at index i
            newCounts = newCounts->setAt(pContext, i, pContext->fromInteger(count + 1));
            break;
        }
    }
    
    statsObj = statsObj->setAttribute(pContext, countsKey, newCounts->asObject(pContext));
    storage = storage->setAt(pContext, keyHash, statsObj);
    setStorage(pContext, &histogramsStorage, storage);
}

HistogramStats Metrics::getHistogram(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoSparseList* labels) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    HistogramStats stats;
    
    const proto::ProtoSparseList* storage = getStorage(pContext, &histogramsStorage);
    const proto::ProtoString* key = makeKey(pContext, name, labels);
    unsigned long keyHash = key->getHash(pContext);
    
    if (storage->has(pContext, keyHash)) {
        const proto::ProtoObject* statsObj = storage->getAt(pContext, keyHash);
        const proto::ProtoString* bucketsKey = pContext->fromUTF8String("buckets")->asString(pContext);
        const proto::ProtoString* countsKey = pContext->fromUTF8String("counts")->asString(pContext);
        const proto::ProtoString* sumKey = pContext->fromUTF8String("sum")->asString(pContext);
        const proto::ProtoString* countKey = pContext->fromUTF8String("count")->asString(pContext);
        
        stats.buckets = statsObj->getAttribute(pContext, bucketsKey)->asList(pContext);
        stats.counts = statsObj->getAttribute(pContext, countsKey)->asList(pContext);
        stats.sum = statsObj->getAttribute(pContext, sumKey);
        stats.count = statsObj->getAttribute(pContext, countKey);
    } else {
        // Return empty stats
        stats.buckets = pContext->newList();
        stats.counts = pContext->newList();
        stats.sum = pContext->fromDouble(0.0);
        stats.count = pContext->fromInteger(0);
    }
    
    return stats;
}

Metrics::MetricsSnapshot Metrics::getSnapshot(proto::ProtoContext* pContext) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    MetricsSnapshot snapshot;
    snapshot.counters = getStorage(pContext, &countersStorage);
    snapshot.gauges = getStorage(pContext, &gaugesStorage);
    snapshot.histograms = getStorage(pContext, &histogramsStorage);
    return snapshot;
}

const proto::ProtoString* Metrics::exportJSON(proto::ProtoContext* pContext) {
    std::lock_guard<std::mutex> lock(metricsMutex);
    MetricsSnapshot snapshot = getSnapshot(pContext);
    
    // Build JSON string using protoCore string operations
    const proto::ProtoString* json = pContext->fromUTF8String("{\"counters\":{")->asString(pContext);
    
    // Iterate over counters
    const proto::ProtoSparseListIterator* iter = snapshot.counters->getIterator(pContext);
    bool first = true;
    while (iter && iter->hasNext(pContext)) {
        if (!first) {
            json = json->appendLast(pContext, pContext->fromUTF8String(",")->asString(pContext));
        }
        // Format as "key":value (simplified - would need key lookup)
        const proto::ProtoObject* value = iter->nextValue(pContext);
        if (value->isDouble(pContext)) {
            // Convert double to string and append
            double d = value->asDouble(pContext);
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", d);
            json = json->appendLast(pContext, pContext->fromUTF8String(buf)->asString(pContext));
        }
        iter = const_cast<proto::ProtoSparseListIterator*>(iter)->advance(pContext);
        first = false;
    }
    
    json = json->appendLast(pContext, pContext->fromUTF8String("}}")->asString(pContext));
    return json;
}

const proto::ProtoString* Metrics::exportPrometheus(proto::ProtoContext* pContext) {
    // Similar to exportJSON but in Prometheus format
    // Build using protoCore string operations
    return pContext->fromUTF8String("# Prometheus format export")->asString(pContext);
}

const proto::ProtoString* Metrics::makeKey(proto::ProtoContext* pContext, const proto::ProtoString* name, const proto::ProtoSparseList* labels) {
    if (!labels || labels->getSize(pContext) == 0) {
        return name;
    }
    
    // Build key as: name{label1="value1",label2="value2"}
    const proto::ProtoString* key = name->appendLast(pContext, pContext->fromUTF8String("{")->asString(pContext));
    
    const proto::ProtoSparseListIterator* iter = labels->getIterator(pContext);
    bool first = true;
    while (iter && iter->hasNext(pContext)) {
        if (!first) {
            key = key->appendLast(pContext, pContext->fromUTF8String(",")->asString(pContext));
        }
        // Format label (simplified - would need key lookup)
        const proto::ProtoObject* value = iter->nextValue(pContext);
        if (value->isString(pContext)) {
            key = key->appendLast(pContext, value->asString(pContext));
        }
        iter = const_cast<proto::ProtoSparseListIterator*>(iter)->advance(pContext);
        first = false;
    }
    
    key = key->appendLast(pContext, pContext->fromUTF8String("}")->asString(pContext));
    return key;
}

const proto::ProtoSparseList* Metrics::getStorage(proto::ProtoContext* pContext, const proto::ProtoSparseList** storage) {
    if (!*storage) {
        *storage = pContext->newSparseList();
    }
    return *storage;
}

void Metrics::setStorage(proto::ProtoContext* pContext, const proto::ProtoSparseList** storage, const proto::ProtoSparseList* newStorage) {
    *storage = newStorage;
}

} // namespace protojs
