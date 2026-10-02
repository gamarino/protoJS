#ifndef PROTOJS_FSMODULE_H
#define PROTOJS_FSMODULE_H

#include "protoCore.h"

namespace protojs {

/**
 * @brief Node-style `fs` module — file IO, directory listing, stat,
 *        rename / unlink / copy / mkdir / rmdir, plus a `fs.promises`
 *        sub-namespace returning real ProtoDeferred objects.
 *        Migrated to protoCore-native (no QuickJS bridge); see
 *        docs/MIGRATION_QUICKJS_TO_PROTOCORE.md.
 */
class FSModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);

    /**
     * @brief Callback-API operations (fs.readFile(path, cb) and siblings)
     *        whose callback has not run yet.  The event-loop drain in
     *        main.cpp waits for them.
     */
    static int getActiveCount();
};

} // namespace protojs

#endif // PROTOJS_FSMODULE_H
