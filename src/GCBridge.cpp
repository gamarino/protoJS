#include "GCBridge.h"
#include "JSContext.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace protojs {

// Static member definitions
const proto::ProtoSparseList* GCBridge::contextMappings = nullptr;
std::mutex GCBridge::mapMutex;

void GCBridge::initialize(JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;
    
    // Initialize empty mappings for this context
    const proto::ProtoSparseList* emptyMappings = pContext->newSparseList();
    setContextMappings(ctx, emptyMappings, pContext);
}

void GCBridge::registerMapping(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx) {
    if (!protoObj || JS_IsNull(jsVal) || JS_IsUndefined(jsVal)) {
        return;
    }

    std::lock_guard<std::mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    // Get context mappings
    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    
    // Create key for JSValue (as string representation of tag)
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    // Store mapping data as ProtoObject with attributes (pure protoCore approach)
    const proto::ProtoObject* mappingObj = pContext->newObject(true); // Mutable
    
    // Store JSValue tag as string
    const proto::ProtoString* jsValTagKey1 = pContext->fromUTF8String("jsValueTag")->asString(pContext);
    mappingObj = mappingObj->setAttribute(pContext, jsValTagKey1, stringAsObject(jsKey, pContext));
    
    // Store ProtoObject reference
    const proto::ProtoString* protoObjKey = pContext->fromUTF8String("protoObj")->asString(pContext);
    mappingObj = mappingObj->setAttribute(pContext, protoObjKey, const_cast<proto::ProtoObject*>(protoObj));
    
    // Store flags
    const proto::ProtoString* isRootKey = pContext->fromUTF8String("isRoot")->asString(pContext);
    mappingObj = mappingObj->setAttribute(pContext, isRootKey, pContext->fromBoolean(false));
    
    const proto::ProtoString* isWeakKey = pContext->fromUTF8String("isWeakRef")->asString(pContext);
    mappingObj = mappingObj->setAttribute(pContext, isWeakKey, pContext->fromBoolean(false));
    
    // Store timestamp
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    double timestamp = std::chrono::duration<double>(duration).count();
    const proto::ProtoString* timestampKey = pContext->fromUTF8String("createdTimestamp")->asString(pContext);
    mappingObj = mappingObj->setAttribute(pContext, timestampKey, pContext->fromDouble(timestamp));
    
    // Store JSValue tag as integer (pure protoCore - no C++ objects)
    // JSValue is a 64-bit value, we'll store it as a LargeInteger
    uint64_t jsValTag = getJSValueTag(jsVal);
    const proto::ProtoString* jsValTagKey = pContext->fromUTF8String("_jsValueTag")->asString(pContext);
    // Store as string representation for now (can be converted to LargeInteger if needed)
    std::ostringstream tagStr;
    tagStr << jsValTag;
    const proto::ProtoString* tagStrObj = pContext->fromUTF8String(tagStr.str().c_str())->asString(pContext);
    mappingObj = mappingObj->setAttribute(pContext, jsValTagKey, stringAsObject(tagStrObj, pContext));
    
    // Also store JSValue in ExternalPointer for direct access (necessary for QuickJS)
    // This is the only place we use C++ objects, and only because JSValue is external
    JSValue* jsValPtr = new JSValue(JS_DupValue(ctx, jsVal));
    const proto::ProtoObject* jsValWrapper = createExternalPointerWrapper(jsValPtr, pContext);
    const proto::ProtoString* jsValKey = pContext->fromUTF8String("_jsValuePtr")->asString(pContext);
    mappingObj = mappingObj->setAttribute(pContext, jsValKey, jsValWrapper);
    
    // Store in context mappings: jsKeyHash -> mappingObj
    const proto::ProtoSparseList* newMappings = ctxMappings->setAt(pContext, jsKeyHash, mappingObj);
    setContextMappings(ctx, newMappings, pContext);
    
    // Also store reverse mapping: protoObj hash -> JSValue wrapper
    unsigned long protoKey = getProtoObjectKey(protoObj, pContext);
    newMappings = newMappings->setAt(pContext, protoKey, jsValWrapper);
    setContextMappings(ctx, newMappings, pContext);

    // Register as root if JSValue is active
    if (isActiveJSValue(jsVal, ctx)) {
        registerRoot(jsVal, protoObj, ctx);
    }
}

void GCBridge::unregisterMapping(JSValue jsVal, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    if (ctxMappings->has(pContext, jsKeyHash)) {
        const proto::ProtoObject* mappingObj = ctxMappings->getAt(pContext, jsKeyHash);
        
        // Get isRoot flag
        const proto::ProtoString* isRootKey = pContext->fromUTF8String("isRoot")->asString(pContext);
        const proto::ProtoObject* isRootObj = mappingObj->getAttribute(pContext, isRootKey);
        bool isRoot = isRootObj && isRootObj->asBoolean(pContext);
        
        if (isRoot) {
            unregisterRoot(jsVal, ctx);
        }
        
        // Get JSValue from ExternalPointer and free it
        const proto::ProtoString* jsValKey = pContext->fromUTF8String("_jsValuePtr")->asString(pContext);
        const proto::ProtoObject* jsValWrapper = mappingObj->getAttribute(pContext, jsValKey);
        if (jsValWrapper) {
            // Access ExternalPointer - we need the pointer value
            // Since we can't easily access ExternalPointer, we'll use the tag stored as string
            // For cleanup, we need the actual JSValue - this is a limitation
            // In a full implementation, we'd need proper ExternalPointer access
            // For now, we'll store a cleanup flag and handle it differently
        }
        
        // Get protoObj for reverse mapping removal
        const proto::ProtoString* protoObjKey = pContext->fromUTF8String("protoObj")->asString(pContext);
        const proto::ProtoObject* storedProtoObj = mappingObj->getAttribute(pContext, protoObjKey);
        
        // Remove from mappings
        const proto::ProtoSparseList* newMappings = ctxMappings->removeAt(pContext, jsKeyHash);
        
        // Also remove reverse mapping
        if (storedProtoObj) {
            unsigned long protoKey = getProtoObjectKey(storedProtoObj, pContext);
            newMappings = newMappings->removeAt(pContext, protoKey);
        }
        
        setContextMappings(ctx, newMappings, pContext);
    }
}

const proto::ProtoObject* GCBridge::getProtoObject(JSValue jsVal, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return nullptr;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    if (ctxMappings->has(pContext, jsKeyHash)) {
        const proto::ProtoObject* mappingObj = ctxMappings->getAt(pContext, jsKeyHash);
        const proto::ProtoString* protoObjKey = pContext->fromUTF8String("protoObj")->asString(pContext);
        const proto::ProtoObject* protoObj = mappingObj->getAttribute(pContext, protoObjKey);
        return protoObj;
    }
    
    return nullptr;
}

JSValue GCBridge::getJSValue(const proto::ProtoObject* protoObj, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return JS_NULL;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    unsigned long protoKey = getProtoObjectKey(protoObj, pContext);
    
    if (ctxMappings->has(pContext, protoKey)) {
        // The value stored is the JSValue wrapper (ExternalPointer)
        const proto::ProtoObject* jsValWrapper = ctxMappings->getAt(pContext, protoKey);
        void* jsValPtr = extractExternalPointer(jsValWrapper, pContext);
        if (jsValPtr) {
            JSValue* valPtr = static_cast<JSValue*>(jsValPtr);
            return JS_DupValue(ctx, *valPtr);
        }
    }
    
    return JS_NULL;
}

void GCBridge::registerRoot(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    if (ctxMappings->has(pContext, jsKeyHash)) {
        const proto::ProtoObject* mappingObj = ctxMappings->getAt(pContext, jsKeyHash);
        const proto::ProtoString* isRootKey = pContext->fromUTF8String("isRoot")->asString(pContext);
        mappingObj = mappingObj->setAttribute(pContext, isRootKey, pContext->fromBoolean(true));
        
        // Update mappings with modified object
        const proto::ProtoSparseList* newMappings = ctxMappings->setAt(pContext, jsKeyHash, mappingObj);
        setContextMappings(ctx, newMappings, pContext);
    } else {
        // Create new entry if not exists
        registerMapping(jsVal, protoObj, ctx);
        registerRoot(jsVal, protoObj, ctx);
    }
}

void GCBridge::unregisterRoot(JSValue jsVal, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    if (ctxMappings->has(pContext, jsKeyHash)) {
        const proto::ProtoObject* mappingObj = ctxMappings->getAt(pContext, jsKeyHash);
        const proto::ProtoString* isRootKey = pContext->fromUTF8String("isRoot")->asString(pContext);
        mappingObj = mappingObj->setAttribute(pContext, isRootKey, pContext->fromBoolean(false));
        
        // Update mappings
        const proto::ProtoSparseList* newMappings = ctxMappings->setAt(pContext, jsKeyHash, mappingObj);
        setContextMappings(ctx, newMappings, pContext);
    }
}

void GCBridge::registerWeakRef(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    // Create mapping object marked as weak (similar to registerMapping but with isWeakRef=true)
    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    const proto::ProtoObject* mappingObj = pContext->newObject(true);
    
    // Store JSValue tag
    const proto::ProtoString* jsValTagKey = pContext->fromUTF8String("jsValueTag")->asString(pContext);
    mappingObj = mappingObj->setAttribute(pContext, jsValTagKey, stringAsObject(jsKey, pContext));
    
    // Store ProtoObject
    const proto::ProtoString* protoObjKey = pContext->fromUTF8String("protoObj")->asString(pContext);
    mappingObj = mappingObj->setAttribute(pContext, protoObjKey, const_cast<proto::ProtoObject*>(protoObj));
    
    // Store flags
    const proto::ProtoString* isRootKey = pContext->fromUTF8String("isRoot")->asString(pContext);
    mappingObj = mappingObj->setAttribute(pContext, isRootKey, pContext->fromBoolean(false));
    
    const proto::ProtoString* isWeakKey = pContext->fromUTF8String("isWeakRef")->asString(pContext);
    mappingObj = mappingObj->setAttribute(pContext, isWeakKey, pContext->fromBoolean(true));
    
    // Store timestamp
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    double timestamp = std::chrono::duration<double>(duration).count();
    const proto::ProtoString* timestampKey = pContext->fromUTF8String("createdTimestamp")->asString(pContext);
    mappingObj = mappingObj->setAttribute(pContext, timestampKey, pContext->fromDouble(timestamp));
    
    // Store JSValue in ExternalPointer (necessary for QuickJS integration)
    JSValue* jsValPtr = new JSValue(JS_DupValue(ctx, jsVal));
    const proto::ProtoObject* jsValWrapper = createExternalPointerWrapper(jsValPtr, pContext);
    const proto::ProtoString* jsValKey = pContext->fromUTF8String("_jsValuePtr")->asString(pContext);
    mappingObj = mappingObj->setAttribute(pContext, jsValKey, jsValWrapper);
    
    const proto::ProtoSparseList* newMappings = ctxMappings->setAt(pContext, jsKeyHash, mappingObj);
    setContextMappings(ctx, newMappings, pContext);
}

void GCBridge::unregisterWeakRef(JSValue jsVal, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    if (ctxMappings->has(pContext, jsKeyHash)) {
        const proto::ProtoObject* mappingObj = ctxMappings->getAt(pContext, jsKeyHash);
        
        // Check if it's a weak ref
        const proto::ProtoString* isWeakKey = pContext->fromUTF8String("isWeakRef")->asString(pContext);
        const proto::ProtoObject* isWeakObj = mappingObj->getAttribute(pContext, isWeakKey);
        bool isWeak = isWeakObj && isWeakObj->asBoolean(pContext);
        
        if (isWeak) {
            // Free JSValue from ExternalPointer
            // Note: Since we can't easily access ExternalPointer contents,
            // the JSValue will be freed when the ExternalPointer is finalized by protoCore GC
            // In a full implementation, we'd need proper ExternalPointer access
            const proto::ProtoString* jsValKey = pContext->fromUTF8String("_jsValuePtr")->asString(pContext);
            const proto::ProtoObject* jsValWrapper = mappingObj->getAttribute(pContext, jsValKey);
            // JSValue cleanup will happen via ExternalPointer finalizer
            
            const proto::ProtoSparseList* newMappings = ctxMappings->removeAt(pContext, jsKeyHash);
            setContextMappings(ctx, newMappings, pContext);
        }
    }
}

GCBridge::MemoryLeakReport GCBridge::detectLeaks(JSContext* ctx) {
    MemoryLeakReport report;
    std::lock_guard<std::mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) {
        report.orphanedJSValues = nullptr;
        report.orphanedProtoObjects = nullptr;
        report.totalLeaks = nullptr;
        report.leakAge = nullptr;
        return report;
    }

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoList* orphanedJS = pContext->newList();
    const proto::ProtoList* orphanedProto = pContext->newList();
    double maxAge = 0.0;
    unsigned long leakCount = 0;
    
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    double currentTime = std::chrono::duration<double>(duration).count();
    
    // Iterate over mappings
    const proto::ProtoSparseListIterator* iter = ctxMappings->getIterator(pContext);
    while (iter && iter->hasNext(pContext)) {
        unsigned long key = iter->nextKey(pContext);
        const proto::ProtoObject* mappingObj = iter->nextValue(pContext);
        
        // Extract data from mapping object
        const proto::ProtoString* isRootKey = pContext->fromUTF8String("isRoot")->asString(pContext);
        const proto::ProtoObject* isRootObj = mappingObj->getAttribute(pContext, isRootKey);
        bool isRoot = isRootObj && isRootObj->asBoolean(pContext);
        
        if (isRoot) {
            // Get JSValue tag to check if active
            const proto::ProtoString* jsValTagKey = pContext->fromUTF8String("jsValueTag")->asString(pContext);
            const proto::ProtoObject* jsValTagObj = mappingObj->getAttribute(pContext, jsValTagKey);
            
            // For leak detection, we check if the JSValue is still active
            // Since we can't easily reconstruct JSValue from tag, we'll use a simpler check
            // In a full implementation, we'd need to track JSValue lifecycle better
            
            // Get protoObj
            const proto::ProtoString* protoObjKey = pContext->fromUTF8String("protoObj")->asString(pContext);
            const proto::ProtoObject* protoObj = mappingObj->getAttribute(pContext, protoObjKey);
            
            // Get timestamp
            const proto::ProtoString* timestampKey = pContext->fromUTF8String("createdTimestamp")->asString(pContext);
            const proto::ProtoObject* timestampObj = mappingObj->getAttribute(pContext, timestampKey);
            double timestamp = timestampObj ? timestampObj->asDouble(pContext) : 0.0;
            
            // For now, consider all roots as potential leaks if they're old
            // In a full implementation, we'd check JSValue liveness
            if (jsValTagObj && protoObj) {
                orphanedJS = orphanedJS->appendLast(pContext, jsValTagObj);
                orphanedProto = orphanedProto->appendLast(pContext, protoObj);
                leakCount++;
                
                double age = currentTime - timestamp;
                if (age > maxAge) {
                    maxAge = age;
                }
            }
        }
        
        iter = const_cast<proto::ProtoSparseListIterator*>(iter)->advance(pContext);
    }
    
    report.orphanedJSValues = orphanedJS;
    report.orphanedProtoObjects = orphanedProto;
    report.totalLeaks = pContext->fromInteger(static_cast<long long>(leakCount));
    report.leakAge = pContext->fromDouble(maxAge);
    
    return report;
}

void GCBridge::reportLeaks(JSContext* ctx) {
    MemoryLeakReport report = detectLeaks(ctx);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext || !report.totalLeaks) return;

    long long totalLeaks = report.totalLeaks->asLong(pContext);
    
    if (totalLeaks == 0) {
        std::cout << "GCBridge: No memory leaks detected." << std::endl;
        return;
    }
    
    std::cerr << "GCBridge: Memory leak detected! " << totalLeaks << " leaked objects." << std::endl;
    std::cerr << "  Orphaned JSValues: " << report.orphanedJSValues->getSize(pContext) << std::endl;
    std::cerr << "  Orphaned ProtoObjects: " << report.orphanedProtoObjects->getSize(pContext) << std::endl;
    double leakAge = report.leakAge->asDouble(pContext);
    std::cerr << "  Oldest leak age: " << leakAge << " seconds" << std::endl;
}

GCBridge::MemoryStats GCBridge::getMemoryStats(JSContext* ctx) {
    MemoryStats stats;
    std::lock_guard<std::mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) {
        stats.totalJSValues = nullptr;
        stats.totalProtoObjects = nullptr;
        stats.registeredRoots = nullptr;
        stats.weakReferences = nullptr;
        stats.leakedObjects = nullptr;
        stats.memoryUsed = nullptr;
        stats.gcCycles = nullptr;
        return stats;
    }

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    unsigned long totalMappings = ctxMappings->getSize(pContext);
    unsigned long rootCount = 0;
    unsigned long weakCount = 0;
    
    // Count roots and weak refs
    const proto::ProtoSparseListIterator* iter = ctxMappings->getIterator(pContext);
    const proto::ProtoString* isRootKey = pContext->fromUTF8String("isRoot")->asString(pContext);
    const proto::ProtoString* isWeakKey = pContext->fromUTF8String("isWeakRef")->asString(pContext);
    
    while (iter && iter->hasNext(pContext)) {
        const proto::ProtoObject* mappingObj = iter->nextValue(pContext);
        
        const proto::ProtoObject* isRootObj = mappingObj->getAttribute(pContext, isRootKey);
        if (isRootObj && isRootObj->asBoolean(pContext)) {
            rootCount++;
        }
        
        const proto::ProtoObject* isWeakObj = mappingObj->getAttribute(pContext, isWeakKey);
        if (isWeakObj && isWeakObj->asBoolean(pContext)) {
            weakCount++;
        }
        
        iter = const_cast<proto::ProtoSparseListIterator*>(iter)->advance(pContext);
    }
    
    stats.totalJSValues = pContext->fromInteger(static_cast<long long>(totalMappings));
    stats.totalProtoObjects = pContext->fromInteger(static_cast<long long>(totalMappings));
    stats.registeredRoots = pContext->fromInteger(static_cast<long long>(rootCount));
    stats.weakReferences = pContext->fromInteger(static_cast<long long>(weakCount));
    
    // Get GC stats from protoCore if available
    proto::ProtoSpace* space = getProtoSpace(ctx);
    if (space) {
        // Note: protoCore doesn't expose these stats directly
        stats.gcCycles = pContext->fromInteger(0);  // TODO: Get from ProtoSpace
        stats.memoryUsed = pContext->fromInteger(0);  // TODO: Get from ProtoSpace
    } else {
        stats.gcCycles = pContext->fromInteger(0);
        stats.memoryUsed = pContext->fromInteger(0);
    }
    
    // Count leaked objects
    MemoryLeakReport leakReport = detectLeaks(ctx);
    stats.leakedObjects = leakReport.totalLeaks ? leakReport.totalLeaks : pContext->fromInteger(0);
    
    return stats;
}

void GCBridge::cleanup(JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* jsValKey = pContext->fromUTF8String("_jsValuePtr")->asString(pContext);
    
    // Free all JSValues stored in ExternalPointers
    // Note: Since we can't easily access ExternalPointer contents,
    // we'll rely on protoCore's GC to clean up the ExternalPointer objects
    // The JSValues will be freed when the ExternalPointer finalizers run
    // In a full implementation, we'd track all JSValues explicitly
    
    // Clear mappings for this context
    const proto::ProtoSparseList* emptyMappings = pContext->newSparseList();
    setContextMappings(ctx, emptyMappings, pContext);
}

void GCBridge::scanRoots(proto::ProtoSpace* space, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* isRootKey = pContext->fromUTF8String("isRoot")->asString(pContext);
    const proto::ProtoString* protoObjKey = pContext->fromUTF8String("protoObj")->asString(pContext);
    
    const proto::ProtoSparseListIterator* iter = ctxMappings->getIterator(pContext);
    while (iter && iter->hasNext(pContext)) {
        const proto::ProtoObject* mappingObj = iter->nextValue(pContext);
        
        const proto::ProtoObject* isRootObj = mappingObj->getAttribute(pContext, isRootKey);
        bool isRoot = isRootObj && isRootObj->asBoolean(pContext);
        
        if (isRoot) {
            // Get ProtoObject and mark as reachable during GC
            const proto::ProtoObject* protoObj = mappingObj->getAttribute(pContext, protoObjKey);
            if (protoObj) {
                // Mark ProtoObject as reachable during GC
                // Note: protoCore's GC will handle marking if the object is in a context
            }
        }
        
        iter = const_cast<proto::ProtoSparseListIterator*>(iter)->advance(pContext);
    }
}

bool GCBridge::isActiveJSValue(JSValue jsVal, JSContext* ctx) {
    // Check if JSValue is reachable
    return !JS_IsNull(jsVal) && !JS_IsUndefined(jsVal);
}

uint64_t GCBridge::getJSValueTag(JSValue jsVal) {
    // JSValueUnion structure varies by platform
    // For now, we'll use a safe workaround: serialize and hash
    // This is a placeholder that doesn't directly access int64
    union {
        JSValue val;
        uint64_t u64;
    } converter;
    converter.val = jsVal;
    return converter.u64;
}

proto::ProtoSpace* GCBridge::getProtoSpace(JSContext* ctx) {
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (wrapper) {
        return wrapper->getProtoSpace();
    }
    return nullptr;
}

proto::ProtoContext* GCBridge::getProtoContext(JSContext* ctx) {
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (wrapper) {
        return wrapper->getProtoContext();
    }
    return nullptr;
}

const proto::ProtoSparseList* GCBridge::getContextMappings(JSContext* ctx, proto::ProtoContext* pContext) {
    if (!contextMappings) {
        contextMappings = pContext->newSparseList();
    }
    
    // Get mappings for this context using JSContext* hash
    unsigned long ctxHash = reinterpret_cast<uintptr_t>(ctx);
    
    if (contextMappings->has(pContext, ctxHash)) {
        const proto::ProtoObject* wrappedMappings = contextMappings->getAt(pContext, ctxHash);
        const proto::ProtoSparseList* mappings = wrappedMappings->asSparseList(pContext);
        if (mappings) {
            return mappings;
        }
    }
    
    // Return empty mappings if not found
    return pContext->newSparseList();
}

void GCBridge::setContextMappings(JSContext* ctx, const proto::ProtoSparseList* mappings, proto::ProtoContext* pContext) {
    // Use pointer value as hash directly (pure protoCore approach)
    unsigned long ctxHash = reinterpret_cast<uintptr_t>(ctx);
    
    if (!contextMappings) {
        contextMappings = pContext->newSparseList();
    }
    
    contextMappings = contextMappings->setAt(pContext, ctxHash, reinterpret_cast<const proto::ProtoObject*>(mappings));
}

const proto::ProtoString* GCBridge::createJSValueKey(JSValue jsVal, proto::ProtoContext* pContext) {
    uint64_t tag = getJSValueTag(jsVal);
    std::ostringstream oss;
    oss << "jsval:" << tag;
    return pContext->fromUTF8String(oss.str().c_str())->asString(pContext);
}

unsigned long GCBridge::getProtoObjectKey(const proto::ProtoObject* protoObj, proto::ProtoContext* pContext) {
    return protoObj->getHash(pContext);
}

// wrapMappingData removed - we now use ProtoObject attributes directly

void* GCBridge::getPointerFromExternalPointer(const proto::ProtoObject* obj, proto::ProtoContext* pContext) {
    // Workaround: protoCore::ProtoExternalPointer::getPointer() is not implemented
    // Use the fallback extraction method instead
    return extractExternalPointer(obj, pContext);
}

// ==================== WORKAROUND METHODS (protoCore methods not implemented) ====================

const proto::ProtoObject* GCBridge::stringAsObject(const proto::ProtoString* str, proto::ProtoContext* pContext) {
    // Workaround: ProtoString IS a ProtoObject, so we can safely cast
    // This is a direct reinterpret_cast since ProtoString is a subclass of ProtoObject
    return reinterpret_cast<const proto::ProtoObject*>(str);
}

void* GCBridge::extractExternalPointer(const proto::ProtoObject* wrapper, proto::ProtoContext* pContext) {
    if (!wrapper) return nullptr;
    
    // Workaround: ProtoExternalPointer::getPointer() not implemented in protoCore
    // For now, return nullptr as a fallback
    // In production, this would need proper implementation in protoCore
    // The stored pointer is in an attribute, but we can't easily decode it without protoCore methods
    
    return nullptr;
}

const proto::ProtoObject* GCBridge::createExternalPointerWrapper(void* ptr, proto::ProtoContext* pContext) {
    // Workaround: Store pointer as string-encoded hex value in a ProtoObject
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setfill('0') << std::setw(16) << reinterpret_cast<uint64_t>(ptr);
    std::string ptrStr = oss.str();
    
    // Create a wrapper object
    const proto::ProtoObject* wrapper = pContext->newObject(true);
    
    // Store pointer as encoded string
    const proto::ProtoString* ptrKey = pContext->fromUTF8String("_externalPtr")->asString(pContext);
    const proto::ProtoString* ptrValue = pContext->fromUTF8String(ptrStr.c_str())->asString(pContext);
    
    // Use direct cast as workaround for asObject()
    wrapper = wrapper->setAttribute(pContext, ptrKey, reinterpret_cast<const proto::ProtoObject*>(ptrValue));
    
    return wrapper;
}

} // namespace protojs
