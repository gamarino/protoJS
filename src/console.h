#ifndef PROTOJS_CONSOLE_H
#define PROTOJS_CONSOLE_H

#include "headers/protoCore.h"

namespace protojs {

class Console {
public:
    /**
     * Build the console object and register it on the native global.
     * Updates globalObj in-place (protoCore persistent-update semantics).
     */
    static void init(proto::ProtoContext* ctx, const proto::ProtoObject*& globalObj);

private:
    static const proto::ProtoObject* log(proto::ProtoContext*, const proto::ProtoObject*,
                                         const proto::ParentLink*,
                                         const proto::ProtoList*,
                                         const proto::ProtoSparseList*);
    static const proto::ProtoObject* error(proto::ProtoContext*, const proto::ProtoObject*,
                                           const proto::ParentLink*,
                                           const proto::ProtoList*,
                                           const proto::ProtoSparseList*);
    static const proto::ProtoObject* warn(proto::ProtoContext*, const proto::ProtoObject*,
                                          const proto::ParentLink*,
                                          const proto::ProtoList*,
                                          const proto::ProtoSparseList*);
};

} // namespace protojs

#endif // PROTOJS_CONSOLE_H
