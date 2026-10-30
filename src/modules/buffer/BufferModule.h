#ifndef PROTOJS_BUFFERMODULE_H
#define PROTOJS_BUFFERMODULE_H

#include "protoCore.h"
#include <cstddef>

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

    /** A new Buffer (a Uint8Array) holding a copy of `len` bytes at `data`. */
    static const proto::ProtoObject* fromBytes(proto::ProtoContext* ctx,
                                               const char* data, size_t len);

    /**
     * The bytes a Buffer, Uint8Array, Int8Array or Uint8ClampedArray views:
     * true with `data` and `len` set, false for any other value.
     */
    static bool bytesOf(proto::ProtoContext* ctx, const proto::ProtoObject* val,
                        const char*& data, size_t& len);
};

} // namespace protojs

#endif // PROTOJS_BUFFERMODULE_H
