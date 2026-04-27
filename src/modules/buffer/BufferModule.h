#ifndef PROTOJS_BUFFERMODULE_H
#define PROTOJS_BUFFERMODULE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Node-style `Buffer` global — protoCore-native.
 *
 * Replaces the original QuickJS-side implementation: each Buffer
 * instance is a ProtoObject whose `__byte_buffer__` attribute is the
 * `ProtoByteBuffer::asObject()` handle, traced naturally by protoCore's
 * GC.  No JS_NewClassID / JS_SetOpaque is used.
 *
 * The constructor (`new Buffer(size|string|array)`) and the static
 * factories (`Buffer.from`, `Buffer.alloc`, `Buffer.concat`,
 * `Buffer.isBuffer`) live on the same `Buffer` callable, which is
 * installed on the protoCore-native global.
 */
class BufferModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);
};

} // namespace protojs

#endif // PROTOJS_BUFFERMODULE_H
