#ifndef PROTOJS_IOMODULE_H
#define PROTOJS_IOMODULE_H

#include "headers/protoCore.h"
#include "../IOThreadPool.h"
#include "../EventLoop.h"
#include <string>

namespace protojs {

/**
 * @brief I/O module — explicit `io.{readFile,writeFile,readFileAsync,
 *        writeFileAsync}` exposed on the protoCore-native global.
 *        All I/O runs on the IO thread pool.  Migrated to
 *        protoCore-native; see docs/MIGRATION_QUICKJS_TO_PROTOCORE.md.
 */
class IOModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);

    // Helpers (stay public so the migrated `fs` module can reuse).
    static std::string readFileSync(const std::string& path);
    static bool        writeFileSync(const std::string& path,
                                      const std::string& content);
};

} // namespace protojs

#endif // PROTOJS_IOMODULE_H
