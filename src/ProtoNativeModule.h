#ifndef PROTOJS_PROTO_NATIVE_MODULE_H
#define PROTOJS_PROTO_NATIVE_MODULE_H

#include "headers/protoCore.h"
#include <cstddef>

namespace protojs {

/**
 * Descriptor for a single native function entry in a module table.
 */
struct NativeEntry {
    const char*       name;
    proto::ProtoMethod fn;
};

/** Sentinel to terminate a NativeEntry table. */
#define NATIVE_MODULE_END {nullptr, nullptr}

/**
 * Helpers for building and registering native modules as ProtoMethod objects.
 * Used by module init functions that have been converted from JSValue/JSContext
 * to the all-ProtoObject execution path.
 */
class ProtoNativeModule {
public:
    /**
     * Add a single ProtoMethod to an existing object under the given name.
     * Returns the updated object (protoCore objects are persistent/immutable-update).
     */
    static const proto::ProtoObject* addMethod(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* obj,
        const char* name,
        proto::ProtoMethod fn);

    /**
     * Build a new object from a NativeEntry table terminated by NATIVE_MODULE_END.
     */
    static const proto::ProtoObject* buildModule(
        proto::ProtoContext* ctx,
        const NativeEntry* entries,
        size_t count);

    /**
     * Register a module object as a property on the global object.
     * Returns the updated global object.
     */
    static const proto::ProtoObject* registerOnGlobal(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj,
        const char* name,
        const proto::ProtoObject* moduleObj);
};

} // namespace protojs

#endif // PROTOJS_PROTO_NATIVE_MODULE_H
