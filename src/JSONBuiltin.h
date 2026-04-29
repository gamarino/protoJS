#ifndef PROTOJS_JSON_BUILTIN_H
#define PROTOJS_JSON_BUILTIN_H

#include "headers/protoCore.h"

namespace protojs {

class JSONBuiltin {
public:
    /**
     * JSON.stringify(value [, replacer [, space]])
     */
    static const proto::ProtoObject* stringify(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* self,
                                            const proto::ParentLink* parentLink,
                                            const proto::ProtoList* args,
                                            const proto::ProtoSparseList* kwargs);

    /**
     * JSON.parse(text [, reviver])
     */
    static const proto::ProtoObject* parse(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* self,
                                        const proto::ParentLink* parentLink,
                                        const proto::ProtoList* args,
                                        const proto::ProtoSparseList* kwargs);

    /**
     * Registers the global JSON object.
     */
    static void init(proto::ProtoContext* ctx, const proto::ProtoObject*& globalObj);
};

} // namespace protojs

#endif // PROTOJS_JSON_BUILTIN_H
