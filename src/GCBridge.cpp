#include "GCBridge.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace protojs {

// Static member definitions
const proto::ProtoSparseList* GCBridge::contextMappings = nullptr;
proto::ProtoRootSet* GCBridge::rootSet = nullptr;
proto::ProtoRootSet::Handle GCBridge::mappingsHandle = 0;
const proto::ProtoObject* GCBridge::rootAnchor = nullptr;
std::recursive_mutex GCBridge::mapMutex;

// Internal wrapper for JSValue with context
struct JSValueWrapper {
    JSContext* ctx;
    JSValue val;
};

// GC finalizer for JSValueWrapper
void finalizeJSValue(void* ptr) {
    if (ptr) {
        JSValueWrapper* wrapper = static_cast<JSValueWrapper*>(ptr);
        if (wrapper->ctx && !JS_IsUndefined(wrapper->val)) {
            JS_FreeValue(wrapper->ctx, wrapper->val);
        }
        delete wrapper;
    }
}

void GCBridge::initialize(JSContext* ctx) {
    std::lock_guard<std::recursive_mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;
    
    proto::ProtoSpace* space = pContext->space;
    if (!rootSet) {
        rootSet = space->createRootSet("GCBridge");
        rootAnchor = pContext->newObject(true); // Mutable anchor
        // Removed rootSet->add(rootAnchor) to allow cleanup.
    }

    // Initialize empty mappings for this context if not already present
    if (!contextMappings) {
        contextMappings = pContext->newSparseList();
        // Root the global contextMappings via the global rootSet
        if (rootSet) {
            mappingsHandle = rootSet->add(contextMappings->asObject(pContext));
        }
    }
}

void GCBridge::registerMapping(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx) {
    if (!protoObj || JS_IsNull(jsVal) || JS_IsUndefined(jsVal)) {
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    // Get context mappings
    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    
    // Create key for JSValue (as string representation of tag)
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    const proto::ProtoSparseList* newMappings = ctxMappings;

    // 1. Check for OLD mapping to clean up reverse entry if this JSValue was already mapped
    if (ctxMappings->has(pContext, jsKeyHash)) {
        const proto::ProtoObject* oldMappingObj = ctxMappings->getAt(pContext, jsKeyHash);
        if (oldMappingObj && oldMappingObj != PROTO_NONE) {
            // Check if it was already pointing to the SAME protoObj to avoid redundant work
            const proto::ProtoObject* oldProtoObj = oldMappingObj->getAttribute(pContext, JSSymbols::protoObj(pContext), false);
            if (oldProtoObj == protoObj) {
                // Identity preserved, just refresh the wrapper if needed (or do nothing)
                return;
            }
            if (oldProtoObj && oldProtoObj != PROTO_NONE) {
                unsigned long oldProtoKey = getProtoObjectKey(oldProtoObj, pContext);
                newMappings = newMappings->removeAt(pContext, oldProtoKey);
            }
        }
    }

    // 2. Create NEW mapping object (IMMUTABLE to avoid mutable_ref leak)
    const proto::ProtoObject* mappingObj = pContext->newObject(false);
    
    // Store JSValue tag
    mappingObj = mappingObj->setAttribute(pContext, JSSymbols::jsValueTag(pContext), stringAsObject(jsKey, pContext));

    // Store ProtoObject
    mappingObj = mappingObj->setAttribute(pContext, JSSymbols::protoObj(pContext), const_cast<proto::ProtoObject*>(protoObj));

    // Store flags
    mappingObj = mappingObj->setAttribute(pContext, JSSymbols::isRoot(pContext), pContext->fromBoolean(false));

    // Store JSValue in ExternalPointer
    JSValueWrapper* wrapper = new JSValueWrapper{ctx, JS_DupValue(ctx, jsVal)};
    const proto::ProtoObject* jsValWrapper = pContext->fromExternalPointer(wrapper, finalizeJSValue);
    mappingObj = mappingObj->setAttribute(pContext, JSSymbols::jsValuePtrField(pContext), jsValWrapper);
    
    // 3. Register JS -> Proto mapping
    newMappings = newMappings->setAt(pContext, jsKeyHash, mappingObj);
    
    // 4. Register Proto -> JS mapping
    unsigned long protoKey = getProtoObjectKey(protoObj, pContext);
    newMappings = newMappings->setAt(pContext, protoKey, jsValWrapper);
    
    setContextMappings(ctx, newMappings, pContext);
}

void GCBridge::unregisterMapping(JSValue jsVal, JSContext* ctx) {
    std::lock_guard<std::recursive_mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    if (ctxMappings->has(pContext, jsKeyHash)) {
        const proto::ProtoObject* mappingObj = ctxMappings->getAt(pContext, jsKeyHash);
        if (!mappingObj || mappingObj == PROTO_NONE) return;
        
        // Get isRoot flag
        const proto::ProtoObject* isRootObj = mappingObj->getAttribute(pContext, JSSymbols::isRoot(pContext));
        bool isRoot = isRootObj && isRootObj != PROTO_NONE && isRootObj->asBoolean(pContext);

        if (isRoot) {
            unregisterRoot(jsVal, ctx);
        }

        // Get JSValue from ExternalPointer and free it
        const proto::ProtoObject* jsValWrapper = mappingObj->getAttribute(pContext, JSSymbols::jsValuePtrField(pContext));
        if (jsValWrapper && jsValWrapper != PROTO_NONE) {
            // Access ExternalPointer - we need the pointer value
            // Since we can't easily access ExternalPointer, we'll use the tag stored as string
            // For cleanup, we need the actual JSValue - this is a limitation
            // In a full implementation, we'd need proper ExternalPointer access
            // For now, we'll store a cleanup flag and handle it differently
        }

        // Get protoObj for reverse mapping removal
        const proto::ProtoObject* storedProtoObj = mappingObj->getAttribute(pContext, JSSymbols::protoObj(pContext));
        
        // Remove from mappings
        const proto::ProtoSparseList* newMappings = ctxMappings->removeAt(pContext, jsKeyHash);
        
        // Also remove reverse mapping
        if (storedProtoObj && storedProtoObj != PROTO_NONE) {
            unsigned long protoKey = getProtoObjectKey(storedProtoObj, pContext);
            newMappings = newMappings->removeAt(pContext, protoKey);
        }
        
        setContextMappings(ctx, newMappings, pContext);
    }
}

const proto::ProtoObject* GCBridge::getProtoObject(JSValue jsVal, JSContext* ctx) {
    std::lock_guard<std::recursive_mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return nullptr;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    if (ctxMappings->has(pContext, jsKeyHash)) {
        const proto::ProtoObject* mappingObj = ctxMappings->getAt(pContext, jsKeyHash);
        if (!mappingObj || mappingObj == PROTO_NONE) return nullptr;
        const proto::ProtoObject* protoObj = mappingObj->getAttribute(pContext, JSSymbols::protoObj(pContext));
        return (protoObj && protoObj != PROTO_NONE) ? protoObj : nullptr;
    }
    
    return nullptr;
}

JSValue GCBridge::getJSValue(const proto::ProtoObject* protoObj, JSContext* ctx) {
    std::lock_guard<std::recursive_mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return JS_NULL;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    unsigned long protoKey = getProtoObjectKey(protoObj, pContext);
    
    if (ctxMappings->has(pContext, protoKey)) {
        // The value stored is the JSValue wrapper (ExternalPointer)
        const proto::ProtoObject* jsValWrapper = ctxMappings->getAt(pContext, protoKey);
        if (!jsValWrapper || jsValWrapper == PROTO_NONE) return JS_NULL;
        void* ptr = extractExternalPointer(jsValWrapper, pContext);
        if (ptr) {
            JSValueWrapper* wrapper = static_cast<JSValueWrapper*>(ptr);
            return JS_DupValue(ctx, wrapper->val);
        }
    }
    
    return JS_NULL;
}

void GCBridge::registerRoot(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx) {
    std::lock_guard<std::recursive_mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    if (ctxMappings->has(pContext, jsKeyHash)) {
        const proto::ProtoObject* mappingObj = ctxMappings->getAt(pContext, jsKeyHash);
        if (!mappingObj || mappingObj == PROTO_NONE) return;
        mappingObj = mappingObj->setAttribute(pContext, JSSymbols::isRoot(pContext), pContext->fromBoolean(true));

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
    std::lock_guard<std::recursive_mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    if (ctxMappings->has(pContext, jsKeyHash)) {
        const proto::ProtoObject* mappingObj = ctxMappings->getAt(pContext, jsKeyHash);
        if (!mappingObj || mappingObj == PROTO_NONE) return;
        mappingObj = mappingObj->setAttribute(pContext, JSSymbols::isRoot(pContext), pContext->fromBoolean(false));

        // Update mappings
        const proto::ProtoSparseList* newMappings = ctxMappings->setAt(pContext, jsKeyHash, mappingObj);
        setContextMappings(ctx, newMappings, pContext);
    }
}

void GCBridge::registerWeakRef(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx) {
    std::lock_guard<std::recursive_mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    // Create mapping object marked as weak (similar to registerMapping but with isWeakRef=true)
    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    const proto::ProtoObject* mappingObj = pContext->newObject(false);
    
    // Store JSValue tag
    mappingObj = mappingObj->setAttribute(pContext, JSSymbols::jsValueTag(pContext), stringAsObject(jsKey, pContext));

    // Store ProtoObject
    mappingObj = mappingObj->setAttribute(pContext, JSSymbols::protoObj(pContext), const_cast<proto::ProtoObject*>(protoObj));

    // Store flags
    mappingObj = mappingObj->setAttribute(pContext, JSSymbols::isRoot(pContext), pContext->fromBoolean(false));

    mappingObj = mappingObj->setAttribute(pContext, JSSymbols::isWeakRef(pContext), pContext->fromBoolean(true));

    // Store timestamp
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    double timestamp = std::chrono::duration<double>(duration).count();
    mappingObj = mappingObj->setAttribute(pContext, JSSymbols::createdTimestamp(pContext), pContext->fromDouble(timestamp));

    // Store JSValue in ExternalPointer
    JSValueWrapper* wrapper = new JSValueWrapper{ctx, JS_DupValue(ctx, jsVal)};
    const proto::ProtoObject* jsValWrapper = pContext->fromExternalPointer(wrapper, finalizeJSValue);
    mappingObj = mappingObj->setAttribute(pContext, JSSymbols::jsValuePtrField(pContext), jsValWrapper);
    
    const proto::ProtoSparseList* newMappings = ctxMappings->setAt(pContext, jsKeyHash, mappingObj);
    setContextMappings(ctx, newMappings, pContext);
}

void GCBridge::unregisterWeakRef(JSValue jsVal, JSContext* ctx) {
    std::lock_guard<std::recursive_mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);
    const proto::ProtoString* jsKey = createJSValueKey(jsVal, pContext);
    unsigned long jsKeyHash = jsKey->getHash(pContext);
    
    if (ctxMappings->has(pContext, jsKeyHash)) {
        const proto::ProtoObject* mappingObj = ctxMappings->getAt(pContext, jsKeyHash);
        if (!mappingObj || mappingObj == PROTO_NONE) return;
        
        // Check if it's a weak ref
        const proto::ProtoObject* isWeakObj = mappingObj->getAttribute(pContext, JSSymbols::isWeakRef(pContext));
        bool isWeak = isWeakObj && isWeakObj != PROTO_NONE && isWeakObj->asBoolean(pContext);

        if (isWeak) {
            // Free JSValue from ExternalPointer
            // Note: Since we can't easily access ExternalPointer contents,
            // the JSValue will be freed when the ExternalPointer is finalized by protoCore GC
            // In a full implementation, we'd need proper ExternalPointer access
            const proto::ProtoObject* jsValWrapper = mappingObj->getAttribute(pContext, JSSymbols::jsValuePtrField(pContext));
            // JSValue cleanup will happen via ExternalPointer finalizer
            
            const proto::ProtoSparseList* newMappings = ctxMappings->removeAt(pContext, jsKeyHash);
            setContextMappings(ctx, newMappings, pContext);
        }
    }
}

GCBridge::MemoryLeakReport GCBridge::detectLeaks(JSContext* ctx) {
    MemoryLeakReport report;
    std::lock_guard<std::recursive_mutex> lock(mapMutex);
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
        const proto::ProtoObject* isRootObj = mappingObj->getAttribute(pContext, JSSymbols::isRoot(pContext));
        bool isRoot = isRootObj && isRootObj != PROTO_NONE && isRootObj->asBoolean(pContext);

        if (isRoot) {
            // Get JSValue tag to check if active
            const proto::ProtoObject* jsValTagObj = mappingObj->getAttribute(pContext, JSSymbols::jsValueTag(pContext));

            // For leak detection, we check if the JSValue is still active
            // Since we can't easily reconstruct JSValue from tag, we'll use a simpler check
            // In a full implementation, we'd need to track JSValue lifecycle better

            // Get protoObj
            const proto::ProtoObject* protoObj = mappingObj->getAttribute(pContext, JSSymbols::protoObj(pContext));

            // Get timestamp
            const proto::ProtoObject* timestampObj = mappingObj->getAttribute(pContext, JSSymbols::createdTimestamp(pContext));
            double timestamp = (timestampObj && timestampObj != PROTO_NONE) ? timestampObj->asDouble(pContext) : 0.0;
            
            // For now, consider all roots as potential leaks if they're old
            // In a full implementation, we'd check JSValue liveness
            if (jsValTagObj && jsValTagObj != PROTO_NONE && protoObj && protoObj != PROTO_NONE) {
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
    std::lock_guard<std::recursive_mutex> lock(mapMutex);
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

    while (iter && iter->hasNext(pContext)) {
        const proto::ProtoObject* mappingObj = iter->nextValue(pContext);

        const proto::ProtoObject* isRootObj = mappingObj->getAttribute(pContext, JSSymbols::isRoot(pContext));
        if (isRootObj && isRootObj != PROTO_NONE && isRootObj->asBoolean(pContext)) {
            rootCount++;
        }

        const proto::ProtoObject* isWeakObj = mappingObj->getAttribute(pContext, JSSymbols::isWeakRef(pContext));
        if (isWeakObj && isWeakObj != PROTO_NONE && isWeakObj->asBoolean(pContext)) {
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
    std::lock_guard<std::recursive_mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext || !contextMappings) return;

    uint64_t ctxHash = reinterpret_cast<uint64_t>(ctx);
    if (contextMappings->has(pContext, ctxHash)) {
        const proto::ProtoObject* wrappedMappings = contextMappings->getAt(pContext, ctxHash);
        const proto::ProtoSparseList* ctxMappings = reinterpret_cast<const proto::ProtoObject*>(wrappedMappings)->asSparseList(pContext);
        
        // Iterate all mappings to invalidate JSValueWrappers
        const proto::ProtoSparseListIterator* iter = ctxMappings->getIterator(pContext);
        while (iter && iter->hasNext(pContext)) {
            const proto::ProtoObject* val = iter->nextValue(pContext);
            if (val && val != PROTO_NONE) {
                // If it's a mappingObj, it has a jsValuePtrField
                const proto::ProtoObject* wrapperObj = val->getAttribute(pContext, JSSymbols::jsValuePtrField(pContext), false);
                if (!wrapperObj || wrapperObj == PROTO_NONE) {
                    // Might be a reverse mapping entry (direct wrapper)
                    wrapperObj = val;
                }

                void* ptr = extractExternalPointer(wrapperObj, pContext);
                if (ptr) {
                    JSValueWrapper* wrapper = static_cast<JSValueWrapper*>(ptr);
                    if (wrapper->ctx == ctx) {
                        if (!JS_IsUndefined(wrapper->val)) {
                            JS_FreeValue(ctx, wrapper->val);
                            wrapper->val = JS_UNDEFINED;
                        }
                        wrapper->ctx = nullptr; // Invalidate for finalizer
                    }
                }
            }
            iter = const_cast<proto::ProtoSparseListIterator*>(iter)->advance(pContext);
        }

        // Remove from global mappings
        contextMappings = contextMappings->removeAt(pContext, ctxHash);
        
        // Update anchor
        if (rootAnchor) {
            rootAnchor->setAttribute(pContext, proto::ProtoString::createSymbol(pContext, "__gc_mappings__"), 
                                    reinterpret_cast<const proto::ProtoObject*>(contextMappings));
        }
    }
}

void GCBridge::scanRoots(proto::ProtoSpace* space, JSContext* ctx) {
    std::lock_guard<std::recursive_mutex> lock(mapMutex);
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) return;

    const proto::ProtoSparseList* ctxMappings = getContextMappings(ctx, pContext);

    const proto::ProtoSparseListIterator* iter = ctxMappings->getIterator(pContext);
    while (iter && iter->hasNext(pContext)) {
        const proto::ProtoObject* mappingObj = iter->nextValue(pContext);

        const proto::ProtoObject* isRootObj = mappingObj->getAttribute(pContext, JSSymbols::isRoot(pContext));
        bool isRoot = isRootObj && isRootObj != PROTO_NONE && isRootObj->asBoolean(pContext);

        if (isRoot) {
            // Get ProtoObject and mark as reachable during GC
            const proto::ProtoObject* protoObj = mappingObj->getAttribute(pContext, JSSymbols::protoObj(pContext));
            if (protoObj && protoObj != PROTO_NONE) {
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
        if (wrappedMappings && wrappedMappings != PROTO_NONE) {
            const proto::ProtoSparseList* mappings = wrappedMappings->asSparseList(pContext);
            if (mappings) {
                return mappings;
            }
        }
    }
    
    // Return empty mappings if not found
    return pContext->newSparseList();
}

void GCBridge::setContextMappings(JSContext* ctx, const proto::ProtoSparseList* mappings, proto::ProtoContext* pContext) {
    std::lock_guard<std::recursive_mutex> lock(mapMutex);
    uint64_t ctxHash = reinterpret_cast<uint64_t>(ctx);
    const proto::ProtoSparseList* next = contextMappings->setAt(pContext, ctxHash, reinterpret_cast<const proto::ProtoObject*>(mappings));
    if (next) {
        contextMappings = next;
        // Update root handle with the new immutable list version.
        if (rootSet && mappingsHandle) {
            rootSet->remove(mappingsHandle);
            mappingsHandle = rootSet->add(contextMappings->asObject(pContext));
        }
    }
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
    if (!obj || !pContext) return nullptr;
    const proto::ProtoExternalPointer* ext = obj->asExternalPointer(pContext);
    if (ext) {
        return ext->getPointer(pContext);
    }
    return extractExternalPointer(obj, pContext);
}

// ==================== WORKAROUND METHODS (protoCore methods not implemented) ====================

const proto::ProtoObject* GCBridge::stringAsObject(const proto::ProtoString* str, proto::ProtoContext* pContext) {
    // Workaround: ProtoString IS a ProtoObject, so we can safely cast
    // This is a direct reinterpret_cast since ProtoString is a subclass of ProtoObject
    return reinterpret_cast<const proto::ProtoObject*>(str);
}

void* GCBridge::extractExternalPointer(const proto::ProtoObject* wrapper, proto::ProtoContext* pContext) {
    if (!wrapper || !pContext) return nullptr;
    
    // First try direct external pointer
    const proto::ProtoExternalPointer* ext = wrapper->asExternalPointer(pContext);
    if (ext) {
        return ext->getPointer(pContext);
    }

    // Fallback for legacy hex-string-encoded workaround
    const proto::ProtoObject* val = wrapper->getAttribute(pContext, JSSymbols::externalPtrField(pContext), false);
    if (!val || val == PROTO_NONE) return nullptr;
    const proto::ProtoString* strVal = val->asString(pContext);
    if (!strVal) return nullptr;
    std::string hexStr;
    strVal->toUTF8String(pContext, hexStr);
    if (hexStr.empty()) return nullptr;
    
    if (hexStr.compare(0, 2, "0x") == 0) {
        return reinterpret_cast<void*>(std::stoull(hexStr.substr(2), nullptr, 16));
    }
    return nullptr;
}

const proto::ProtoObject* GCBridge::createExternalPointerWrapper(void* ptr, proto::ProtoContext* pContext) {
    if (!ptr || !pContext) return PROTO_NONE;
    // Use native ExternalPointer with no finalizer (the pointer is not owned by the wrapper)
    return pContext->fromExternalPointer(ptr, nullptr);
}

} // namespace protojs
