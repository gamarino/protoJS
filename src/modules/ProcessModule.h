#ifndef PROTOJS_PROCESSMODULE_H
#define PROTOJS_PROCESSMODULE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Process module providing Node.js-compatible process information.
 *
 * Migrated to protoCore-native (no QuickJS): registers `process` on the
 * protoCore-native global as a ProtoObject whose ProtoMethods implement
 * the public API.  See docs/MIGRATION_QUICKJS_TO_PROTOCORE.md.
 */
class ProcessModule {
public:
    /**
     * @brief Build the `process` object and register it on the global.
     * @param ctx          protoCore context.
     * @param globalObj    Current native global.  Returned with `process`
     *                     attached.
     * @param argc / argv  Command-line arguments to expose as `process.argv`.
     */
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj,
        int argc, char** argv);
};

} // namespace protojs

#endif // PROTOJS_PROCESSMODULE_H
