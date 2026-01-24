#ifndef PROTOJS_PROTOCOREMODULE_H
#define PROTOJS_PROTOCOREMODULE_H

#include "quickjs.h"

namespace protojs {

/**
 * @brief Module exposing protoCore special collections and utilities.
 * 
 * Provides access to ProtoSet, ProtoMultiset, ProtoSparseList, ProtoTuple,
 * and utilities for controlling mutability.
 */
class ProtoCoreModule {
public:
    /**
     * @brief Initialize the protoCore module and register it in the global scope.
     * @param ctx QuickJS context
     */
    static void init(JSContext* ctx);

private:
    // Set operations
    static JSValue SetConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static JSValue SetAdd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue SetHas(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue SetRemove(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue SetSize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void SetFinalizer(JSRuntime* rt, JSValue val);
    
    // Multiset operations
    static JSValue MultisetConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static JSValue MultisetAdd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue MultisetCount(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue MultisetRemove(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue MultisetSize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void MultisetFinalizer(JSRuntime* rt, JSValue val);
    
    // Tuple operations
    static JSValue Tuple(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // SparseList operations
    static JSValue SparseListConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static JSValue SparseListSet(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue SparseListGet(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue SparseListHas(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue SparseListSize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void SparseListFinalizer(JSRuntime* rt, JSValue val);
    
    // Mutability utilities
    static JSValue ImmutableObject(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue MutableObject(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue IsImmutable(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue MakeImmutable(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue MakeMutable(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};

} // namespace protojs

#endif // PROTOJS_PROTOCOREMODULE_H
