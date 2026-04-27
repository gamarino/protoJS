#ifndef PROTOJS_CLUSTERMODULE_H
#define PROTOJS_CLUSTERMODULE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Node-style `cluster` module — fork()-based worker
 *        management.  Migrated to protoCore-native: per-worker state
 *        (pid, IPC fds, id) lives as ProtoObject attributes; no
 *        JS_SetOpaque struct.  See docs/MIGRATION_QUICKJS_TO_PROTOCORE.md.
 */
class ClusterModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);
};

} // namespace protojs

#endif // PROTOJS_CLUSTERMODULE_H
