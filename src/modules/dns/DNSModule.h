#ifndef PROTOJS_DNSMODULE_H
#define PROTOJS_DNSMODULE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Node-style `dns` module — name resolution, reverse lookup,
 *        async lookup callbacks via the IO thread pool.  Migrated to
 *        protoCore-native (no QuickJS bridge); see
 *        docs/MIGRATION_QUICKJS_TO_PROTOCORE.md.
 */
class DNSModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);
};

} // namespace protojs

#endif // PROTOJS_DNSMODULE_H
