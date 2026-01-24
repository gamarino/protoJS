#include "GCBridge.h"
#include "JSContext.h"
#include <iostream>
#include <algorithm>
#include <cstring>

namespace protojs {

// Static member definitions
std::unordered_map<JSContext*, std::unordered_map<uint64_t, GCBridge::MappingEntry>> GCBridge::jsToProtoMap;
std::unordered_map<JSContext*, std::unordered_map<const proto::ProtoObject*, JSValue>> GCBridge::protoToJSMap;
std::unordered_map<JSContext*, std::vector<GCBridge::WeakReference>> GCBridge::weakRefs;
std::mutex GCBridge::mapMutex;

void GCBridge::initialize(JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);
    // Initialize empty maps for this context
    jsToProtoMap[ctx];
    protoToJSMap[ctx];
    weakRefs[ctx];
}

void GCBridge::registerMapping(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx) {
    if (!protoObj || JS_IsNull(jsVal) || JS_IsUndefined(jsVal)) {
        return;
    }

    std::lock_guard<std::mutex> lock(mapMutex);

    uint64_t tag = getJSValueTag(jsVal);
    MappingEntry entry;
    entry.jsValue = JS_DupValue(ctx, jsVal);
    entry.protoObj = protoObj;
    entry.isRoot = false;
    entry.isWeakRef = false;
    entry.created = std::chrono::steady_clock::now();

    jsToProtoMap[ctx][tag] = entry;
    protoToJSMap[ctx][protoObj] = jsVal;

    // Register as root if JSValue is active
    if (isActiveJSValue(jsVal, ctx)) {
        registerRoot(jsVal, protoObj, ctx);
    }
}

void GCBridge::unregisterMapping(JSValue jsVal, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);

    uint64_t tag = getJSValueTag(jsVal);
    auto& ctxMap = jsToProtoMap[ctx];
    auto it = ctxMap.find(tag);
    
    if (it != ctxMap.end()) {
        const proto::ProtoObject* protoObj = it->second.protoObj;
        
        // Unregister root if registered
        if (it->second.isRoot) {
            unregisterRoot(jsVal, ctx);
        }
        
        // Free JSValue
        JS_FreeValue(ctx, it->second.jsValue);
        
        // Remove from both maps
        ctxMap.erase(it);
        protoToJSMap[ctx].erase(protoObj);
    }
}

const proto::ProtoObject* GCBridge::getProtoObject(JSValue jsVal, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);

    uint64_t tag = getJSValueTag(jsVal);
    auto& ctxMap = jsToProtoMap[ctx];
    auto it = ctxMap.find(tag);
    
    if (it != ctxMap.end()) {
        return it->second.protoObj;
    }
    
    return nullptr;
}

JSValue GCBridge::getJSValue(const proto::ProtoObject* protoObj, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);

    auto& ctxMap = protoToJSMap[ctx];
    auto it = ctxMap.find(protoObj);
    
    if (it != ctxMap.end()) {
        return JS_DupValue(ctx, it->second);
    }
    
    return JS_NULL;
}

void GCBridge::registerRoot(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);

    uint64_t tag = getJSValueTag(jsVal);
    auto& ctxMap = jsToProtoMap[ctx];
    auto it = ctxMap.find(tag);
    
    if (it != ctxMap.end()) {
        it->second.isRoot = true;
        
        // Note: protoCore's GC scans contexts automatically during STW phase
        // We mark this as a root so it's included in root scanning
        // The actual root registration happens during GC scanRoots call
    } else {
        // Create new entry if not exists
        registerMapping(jsVal, protoObj, ctx);
        registerRoot(jsVal, protoObj, ctx);
    }
}

void GCBridge::unregisterRoot(JSValue jsVal, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);

    uint64_t tag = getJSValueTag(jsVal);
    auto& ctxMap = jsToProtoMap[ctx];
    auto it = ctxMap.find(tag);
    
    if (it != ctxMap.end()) {
        it->second.isRoot = false;
    }
}

void GCBridge::registerWeakRef(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);

    WeakReference weakRef;
    weakRef.jsVal = JS_DupValue(ctx, jsVal);
    weakRef.protoObj = protoObj;
    weakRef.isAlive = true;

    weakRefs[ctx].push_back(weakRef);

    // Also register as mapping but mark as weak
    uint64_t tag = getJSValueTag(jsVal);
    MappingEntry entry;
    entry.jsValue = JS_DupValue(ctx, jsVal);
    entry.protoObj = protoObj;
    entry.isRoot = false;
    entry.isWeakRef = true;
    entry.created = std::chrono::steady_clock::now();

    jsToProtoMap[ctx][tag] = entry;
}

void GCBridge::unregisterWeakRef(JSValue jsVal, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);

    uint64_t tag = getJSValueTag(jsVal);
    
    // Remove from weak refs vector
    auto& refs = weakRefs[ctx];
    refs.erase(
        std::remove_if(refs.begin(), refs.end(),
            [tag, ctx](const WeakReference& ref) {
                return getJSValueTag(ref.jsVal) == tag;
            }),
        refs.end()
    );
    
    // Remove from mapping
    auto& ctxMap = jsToProtoMap[ctx];
    auto it = ctxMap.find(tag);
    if (it != ctxMap.end() && it->second.isWeakRef) {
        JS_FreeValue(ctx, it->second.jsValue);
        ctxMap.erase(it);
    }
}

GCBridge::MemoryLeakReport GCBridge::detectLeaks(JSContext* ctx) {
    MemoryLeakReport report;
    
    std::lock_guard<std::mutex> lock(mapMutex);

    auto now = std::chrono::steady_clock::now();
    auto& ctxMap = jsToProtoMap[ctx];
    
    for (auto& [tag, entry] : ctxMap) {
        if (entry.isRoot && !isActiveJSValue(entry.jsValue, ctx)) {
            // Potential leak: JSValue registered as root but not active
            report.orphanedJSValues.push_back(tag);
            report.orphanedProtoObjects.push_back(entry.protoObj);
            report.totalLeaks++;
            
            auto age = now - entry.created;
            if (report.leakAge.count() == 0 || age > report.leakAge) {
                report.leakAge = age;
            }
        }
    }
    
    return report;
}

void GCBridge::reportLeaks(JSContext* ctx) {
    MemoryLeakReport report = detectLeaks(ctx);
    
    if (report.totalLeaks == 0) {
        std::cout << "GCBridge: No memory leaks detected." << std::endl;
        return;
    }
    
    std::cerr << "GCBridge: Memory leak detected! " << report.totalLeaks << " leaked objects." << std::endl;
    std::cerr << "  Orphaned JSValues: " << report.orphanedJSValues.size() << std::endl;
    std::cerr << "  Orphaned ProtoObjects: " << report.orphanedProtoObjects.size() << std::endl;
    std::cerr << "  Oldest leak age: " << std::chrono::duration<double>(report.leakAge).count() << " seconds" << std::endl;
}

GCBridge::MemoryStats GCBridge::getMemoryStats(JSContext* ctx) {
    MemoryStats stats;
    
    std::lock_guard<std::mutex> lock(mapMutex);

    auto& ctxMap = jsToProtoMap[ctx];
    stats.totalJSValues = ctxMap.size();
    stats.totalProtoObjects = protoToJSMap[ctx].size();
    stats.weakReferences = weakRefs[ctx].size();
    
    for (const auto& [tag, entry] : ctxMap) {
        if (entry.isRoot) {
            stats.registeredRoots++;
        }
    }
    
    // Get GC stats from protoCore if available
    proto::ProtoSpace* space = getProtoSpace(ctx);
    if (space) {
        // Note: protoCore doesn't expose these stats directly, so we estimate
        // In a full implementation, we'd query ProtoSpace for actual stats
        stats.gcCycles = 0;  // TODO: Get from ProtoSpace
        stats.memoryUsed = 0;  // TODO: Get from ProtoSpace
    }
    
    // Count leaked objects
    MemoryLeakReport leakReport = detectLeaks(ctx);
    stats.leakedObjects = leakReport.totalLeaks;
    
    return stats;
}

void GCBridge::cleanup(JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);

    // Free all JSValues
    auto& ctxMap = jsToProtoMap[ctx];
    for (auto& [tag, entry] : ctxMap) {
        JS_FreeValue(ctx, entry.jsValue);
    }
    
    // Free weak reference JSValues
    for (auto& weakRef : weakRefs[ctx]) {
        JS_FreeValue(ctx, weakRef.jsVal);
    }
    
    // Clear all maps
    jsToProtoMap.erase(ctx);
    protoToJSMap.erase(ctx);
    weakRefs.erase(ctx);
}

void GCBridge::scanRoots(proto::ProtoSpace* space, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);

    auto& ctxMap = jsToProtoMap[ctx];
    
    for (auto& [tag, entry] : ctxMap) {
        if (entry.isRoot && isActiveJSValue(entry.jsValue, ctx)) {
            // Mark ProtoObject as reachable during GC
            // Note: protoCore's GC will handle marking if the object is in a context
            // For now, we just ensure the mapping is maintained
            // In a full implementation, we'd call space->markObject() or similar
        }
    }
}

bool GCBridge::isActiveJSValue(JSValue jsVal, JSContext* ctx) {
    // Check if JSValue is reachable
    // For QuickJS, we can check if it's in the global scope or referenced
    // This is a simplified check - in full implementation, we'd traverse the object graph
    
    // For now, consider all non-null/undefined values as potentially active
    // A more sophisticated implementation would check:
    // - Is it in global scope?
    // - Is it referenced by other active objects?
    // - Is it in the call stack?
    
    return !JS_IsNull(jsVal) && !JS_IsUndefined(jsVal);
}

uint64_t GCBridge::getJSValueTag(JSValue jsVal) {
    // Use JSValue's tag as key
    // JSValue is a 64-bit value where the tag is in the lower bits
    // We use the entire value as a unique identifier
    return static_cast<uint64_t>(jsVal.u.int64);
}

proto::ProtoSpace* GCBridge::getProtoSpace(JSContext* ctx) {
    // Get ProtoSpace from JSContext via JSContextWrapper stored in opaque
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (wrapper) {
        return wrapper->getProtoSpace();
    }
    return nullptr;
}

} // namespace protojs
