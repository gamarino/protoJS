#ifndef PROTOJS_CHILDPROCESSMODULE_H
#define PROTOJS_CHILDPROCESSMODULE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Node-style `child_process` module — spawn / exec / execFile /
 *        fork.  Migrated to protoCore-native: per-child state (pid,
 *        stdin/stdout/stderr fds, killed flag) lives as ProtoObject
 *        attributes; no JS_SetOpaque struct.
 */
class ChildProcessModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);
};

} // namespace protojs

#endif // PROTOJS_CHILDPROCESSMODULE_H
