#ifndef PROTOJS_UTILMODULE_H
#define PROTOJS_UTILMODULE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Node-style `util` module — type predicates, simple stringify
 *        helpers.  Migrated to protoCore-native (no QuickJS bridge);
 *        see docs/MIGRATION_QUICKJS_TO_PROTOCORE.md.
 */
class UtilModule {
public:
    /**
     * @brief Build the `util` module object and register it on the
     *        protoCore-native global.  Returns the updated global.
     */
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);
};

} // namespace protojs

#endif // PROTOJS_UTILMODULE_H
