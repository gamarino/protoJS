#ifndef PROTOJS_DGRAMMODULE_H
#define PROTOJS_DGRAMMODULE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Node-style `dgram` module — UDP sockets via createSocket.
 *        Migrated to protoCore-native: per-socket fd lives as an
 *        attribute on the ProtoObject; no JS_SetOpaque struct.
 */
class DgramModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);
};

} // namespace protojs

#endif // PROTOJS_DGRAMMODULE_H
