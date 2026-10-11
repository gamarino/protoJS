#ifndef PROTOJS_TYPEBRIDGE_H
#define PROTOJS_TYPEBRIDGE_H

#include "quickjs.h"
#include "protoCore.h"

namespace protojs {

/**
 * TypeBridge: conversion between QuickJS JSValue and protoCore ProtoObject.
 * Used on both eval paths: legacy (dual-heap mapping during execution) and
 * protoCore path (only at the boundary: global object and script result).
 */
class TypeBridge {
public:
    /**
     * @brief Converts a QuickJS JSValue to a protoCore ProtoObject.
     */
    static const proto::ProtoObject* fromJS(JSContext* ctx, JSValue val, proto::ProtoContext* pContext);

    /**
     * @brief Converts the result of QuickJS's JS_ParseJSON (null, booleans,
     * numbers, strings, arrays and plain objects) to protoCore values for
     * JSON.parse.
     *
     * Unlike fromJS it treats the value as data: string values are ordinary
     * collectable strings, not interned symbols; keys that are not program
     * vocabulary go to the object's property dictionary
     * (runtime/PropertyDictionary.h); and no GCBridge mapping is registered,
     * since a parsed value is fresh and acyclic. fromJS interned every string
     * value and key for ever and kept every parsed object and array (and its
     * QuickJS original) in the bridge's table for the life of the context.
     */
    static const proto::ProtoObject* fromJSON(JSContext* ctx, JSValue val, proto::ProtoContext* pContext);

    /**
     * @brief Converts a protoCore ProtoObject to a QuickJS JSValue.
     */
    static JSValue toJS(JSContext* ctx, const proto::ProtoObject* obj, proto::ProtoContext* pContext);
};

void initializeUndefinedSentinel(proto::ProtoContext* ctx);
void initializeNullSentinel(proto::ProtoContext* ctx);
const proto::ProtoObject* getUndefinedSentinel();
const proto::ProtoObject* getNullSentinel();

} // namespace protojs

#endif // PROTOJS_TYPEBRIDGE_H
