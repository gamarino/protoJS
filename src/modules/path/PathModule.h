#ifndef PROTOJS_PATHMODULE_H
#define PROTOJS_PATHMODULE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Node-style `path` module — string-only path manipulation,
 *        no filesystem access.  Migrated to protoCore-native (no
 *        QuickJS bridge); see docs/MIGRATION_QUICKJS_TO_PROTOCORE.md.
 */
class PathModule {
public:
    /**
     * @brief Build the `path` module object and register it on the
     *        protoCore-native global.  Returns the updated global.
     */
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);
};

} // namespace protojs

#endif // PROTOJS_PATHMODULE_H
