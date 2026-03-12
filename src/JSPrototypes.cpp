#include "JSPrototypes.h"
#include "NumberPrototype.h"
#include "StringPrototype.h"

namespace protojs {

void BootstrapJSPrototypes(proto::ProtoSpace* space, proto::ProtoContext* ctx, JSPrototypes* out) {
    if (!space || !ctx || !out) return;

    const proto::ProtoObject* objectProto = space->objectPrototype;
    if (!objectProto) return;

    out->object = objectProto;
    out->array = objectProto->newChild(ctx, false);
    out->arguments = objectProto->newChild(ctx, false);
    out->regexp = objectProto->newChild(ctx, false);

    BuildNumberPrototype(space, ctx, objectProto);
    BuildStringPrototype(space, ctx, objectProto);
}

} // namespace protojs
