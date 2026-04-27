#ifndef PROTOJS_EVENTSMODULE_H
#define PROTOJS_EVENTSMODULE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Node-style `events` module — exposes the EventEmitter class.
 *        Migrated to protoCore-native (no QuickJS bridge); see
 *        docs/MIGRATION_QUICKJS_TO_PROTOCORE.md.
 */
class EventsModule {
public:
    /**
     * @brief Build the `events` module object (containing the
     *        EventEmitter constructor) and register it on the
     *        protoCore-native global.  Returns the updated global.
     */
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);
};

} // namespace protojs

#endif // PROTOJS_EVENTSMODULE_H
