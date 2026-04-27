#ifndef PROTOJS_STREAMMODULE_H
#define PROTOJS_STREAMMODULE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Node-style `stream` module — Readable / Writable / Duplex /
 *        Transform / PassThrough constructors.  Migrated to
 *        protoCore-native: per-instance state (buffer, ended flag,
 *        highWaterMark) lives as attributes on the ProtoObject
 *        instance instead of a JS_SetOpaque C++ struct.
 */
class StreamModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);
};

} // namespace protojs

#endif // PROTOJS_STREAMMODULE_H
