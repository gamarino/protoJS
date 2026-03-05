#ifndef PROTOJS_TYPEBRIDGE_H
#define PROTOJS_TYPEBRIDGE_H

#include "quickjs.h"
#include "headers/protoCore.h"

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
     * @brief Converts a protoCore ProtoObject to a QuickJS JSValue.
     */
    static JSValue toJS(JSContext* ctx, const proto::ProtoObject* obj, proto::ProtoContext* pContext);
};

} // namespace protojs

#endif // PROTOJS_TYPEBRIDGE_H
