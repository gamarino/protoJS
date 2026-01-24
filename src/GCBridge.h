#ifndef PROTOJS_GCBRIDGE_H
#define PROTOJS_GCBRIDGE_H

#include "quickjs.h"
#include "headers/protoCore.h"
#include <mutex>

// Forward declaration
namespace protojs {
    class JSContextWrapper;
}

namespace protojs {

/**
 * @brief GCBridge integrates QuickJS JSValue lifecycle with protoCore garbage collection.
 * 
 * Maintains bidirectional mapping between JSValues and ProtoObjects using only protoCore objects.
 * Uses ProtoSparseList for mappings, ProtoExternalPointer for C++ pointers, and ProtoString for keys.
 */
class GCBridge {
public:
    /**
     * @brief Memory statistics structure (using protoCore objects)
     */
    struct MemoryStats {
        const proto::ProtoObject* totalJSValues;
        const proto::ProtoObject* totalProtoObjects;
        const proto::ProtoObject* registeredRoots;
        const proto::ProtoObject* weakReferences;
        const proto::ProtoObject* leakedObjects;
        const proto::ProtoObject* memoryUsed;
        const proto::ProtoObject* gcCycles;
    };

    /**
     * @brief Memory leak report structure (using protoCore objects)
     */
    struct MemoryLeakReport {
        const proto::ProtoList* orphanedJSValues;  // List of JSValue tags as strings
        const proto::ProtoList* orphanedProtoObjects;  // List of ProtoObjects
        const proto::ProtoObject* totalLeaks;
        const proto::ProtoObject* leakAge;  // Age in seconds as double
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
    // Note: MappingData is no longer used as a C++ struct
    // Instead, mapping data is stored as attributes in ProtoObject
    // This structure is kept for reference but not used directly
    struct MappingData {
        JSValue jsValue;
        const proto::ProtoObject* protoObj;
        bool isRoot;
        bool isWeakRef;
        double createdTimestamp;
    };

    // Per-context mappings stored in protoCore
    // Key: JSContext* wrapped in ProtoExternalPointer, stored in global map
    // Value: ProtoSparseList containing mappings for that context
    //   - Key: JSValue tag as ProtoString hash
    //   - Value: ProtoObject containing MappingData (wrapped in ProtoExternalPointer)
    
    // For reverse mapping (ProtoObject -> JSValue):
    //   - Key: ProtoObject hash
    //   - Value: JSValue wrapped in ProtoExternalPointer
    
    // Global map: JSContext* -> ProtoSparseList (mappings for that context)
    // Stored as ProtoSparseList where key is JSContext* hash (via ProtoExternalPointer)
    static const proto::ProtoSparseList* contextMappings;
    static std::mutex mapMutex;

    /**
     * @brief Get or create mappings for a context
     */
    static const proto::ProtoSparseList* getContextMappings(JSContext* ctx, proto::ProtoContext* pContext);

    /**
     * @brief Store mappings for a context
     */
    static void setContextMappings(JSContext* ctx, const proto::ProtoSparseList* mappings, proto::ProtoContext* pContext);

    /**
     * @brief Create a key for JSValue (as ProtoString)
     */
    static const proto::ProtoString* createJSValueKey(JSValue jsVal, proto::ProtoContext* pContext);

    /**
     * @brief Create a key for ProtoObject (use its hash)
     */
    static unsigned long getProtoObjectKey(const proto::ProtoObject* protoObj, proto::ProtoContext* pContext);

    /**
     * @brief Get pointer from ProtoExternalPointer (helper)
     * Uses only public API from protoCore.h
     */
    static void* getPointerFromExternalPointer(const proto::ProtoObject* obj, proto::ProtoContext* pContext);

    /**
     * @brief Check if a JSValue is active (reachable)
     */
    static bool isActiveJSValue(JSValue jsVal, JSContext* ctx);

    /**
     * @brief Get JSValue tag for use as key
     */
    static uint64_t getJSValueTag(JSValue jsVal);

    /**
     * @brief Get ProtoSpace from JSContext
     */
    static proto::ProtoSpace* getProtoSpace(JSContext* ctx);

    /**
     * @brief Get ProtoContext from JSContext
     */
    static proto::ProtoContext* getProtoContext(JSContext* ctx);
};

} // namespace protojs

#endif // PROTOJS_GCBRIDGE_H
