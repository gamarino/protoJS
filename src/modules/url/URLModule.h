#ifndef PROTOJS_URLMODULE_H
#define PROTOJS_URLMODULE_H
#include "headers/protoCore.h"
namespace protojs {

/**
 * @brief Node-style `url` module — exposes the URL constructor.
 *        Migrated to protoCore-native (no QuickJS bridge); see
 *        docs/MIGRATION_QUICKJS_TO_PROTOCORE.md.
 */
class URLModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);
};
} // namespace protojs
#endif
