#ifndef PROTOJS_DEFERRED_H
#define PROTOJS_DEFERRED_H

#include "quickjs.h"
#include "headers/protoCore.h"

namespace protojs {

class Deferred {
public:
    static void init(JSContext* ctx);

private:
    static JSValue constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static void finalizer(JSRuntime* rt, JSValue val);
    
    // This is the function that protoCore thread will run
    static const proto::ProtoObject* threadMain(
        proto::ProtoContext* context,
        const proto::ProtoObject* self,
        const proto::ParentLink* parentLink,
        const proto::ProtoList* positionalParameters,
        const proto::ProtoSparseList* keywordParameters
    );
};

} // namespace protojs

#endif // PROTOJS_DEFERRED_H
