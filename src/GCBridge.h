#ifndef PROTOJS_GCBRIDGE_H
#define PROTOJS_GCBRIDGE_H

#include "quickjs.h"
#include "headers/protoCore.h"
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <vector>
#include <string>

// Forward declaration
namespace protojs {
    class JSContextWrapper;
}

namespace protojs {

/**
 * @brief GCBridge integrates QuickJS JSValue lifecycle with protoCore garbage collection.
 * 
 * Maintains bidirectional mapping between JSValues and ProtoObjects, registers JSValues
 * as GC roots, and provides memory leak detection and profiling.
 */
class GCBridge {
public:
    /**
     * @brief Memory statistics structure
     */
    struct MemoryStats {
        size_t totalJSValues = 0;
        size_t totalProtoObjects = 0;
        size_t registeredRoots = 0;
        size_t weakReferences = 0;
        size_t leakedObjects = 0;
        size_t memoryUsed = 0;
        size_t gcCycles = 0;
    };

    /**
     * @brief Memory leak report structure
     */
    struct MemoryLeakReport {
        std::vector<uint64_t> orphanedJSValues;  // JSValue tags (pointers as uint64_t)
        std::vector<const proto::ProtoObject*> orphanedProtoObjects;
        size_t totalLeaks = 0;
        std::chrono::duration<double> leakAge;
    };

    /**
     * @brief Register a mapping between JSValue and ProtoObject
     */
    static void registerMapping(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx);

    /**
     * @brief Unregister a mapping
     */
    static void unregisterMapping(JSValue jsVal, JSContext* ctx);

    /**
     * @brief Get ProtoObject for a JSValue
     */
    static const proto::ProtoObject* getProtoObject(JSValue jsVal, JSContext* ctx);

    /**
     * @brief Get JSValue for a ProtoObject
     */
    static JSValue getJSValue(const proto::ProtoObject* protoObj, JSContext* ctx);

    /**
     * @brief Register a JSValue as a GC root
     */
    static void registerRoot(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx);

    /**
     * @brief Unregister a JSValue as a GC root
     */
    static void unregisterRoot(JSValue jsVal, JSContext* ctx);

    /**
     * @brief Register a weak reference
     */
    static void registerWeakRef(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx);

    /**
     * @brief Unregister a weak reference
     */
    static void unregisterWeakRef(JSValue jsVal, JSContext* ctx);

    /**
     * @brief Detect memory leaks
     */
    static MemoryLeakReport detectLeaks(JSContext* ctx);

    /**
     * @brief Report memory leaks to console
     */
    static void reportLeaks(JSContext* ctx);

    /**
     * @brief Get memory statistics
     */
    static MemoryStats getMemoryStats(JSContext* ctx);

    /**
     * @brief Cleanup all mappings for a context
     */
    static void cleanup(JSContext* ctx);

    /**
     * @brief Initialize GCBridge for a context
     */
    static void initialize(JSContext* ctx);

    /**
     * @brief Scan roots during GC (called by protoCore GC)
     */
    static void scanRoots(proto::ProtoSpace* space, JSContext* ctx);

private:
    struct MappingEntry {
        JSValue jsValue;
        const proto::ProtoObject* protoObj;
        bool isRoot;
        bool isWeakRef;
        std::chrono::time_point<std::chrono::steady_clock> created;
    };

    struct WeakReference {
        JSValue jsVal;
        const proto::ProtoObject* protoObj;
        bool isAlive;
    };

    // Per-context mappings
    static std::unordered_map<JSContext*, std::unordered_map<uint64_t, MappingEntry>> jsToProtoMap;
    static std::unordered_map<JSContext*, std::unordered_map<const proto::ProtoObject*, JSValue>> protoToJSMap;
    static std::unordered_map<JSContext*, std::vector<WeakReference>> weakRefs;
    static std::mutex mapMutex;

    /**
     * @brief Check if a JSValue is active (reachable)
     */
    static bool isActiveJSValue(JSValue jsVal, JSContext* ctx);

    /**
     * @brief Get JSValue tag for use as map key
     */
    static uint64_t getJSValueTag(JSValue jsVal);

    /**
     * @brief Get ProtoSpace from JSContext
     */
    static proto::ProtoSpace* getProtoSpace(JSContext* ctx);
};

} // namespace protojs

#endif // PROTOJS_GCBRIDGE_H
