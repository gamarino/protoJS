#include "PinnedBuiltin.h"

#include "../JSContext.h"

namespace protojs {

namespace {
proto::ProtoRootSet* currentRootSet() {
    JSContextWrapper* w = JSContextWrapper::current();
    return w ? w->getRootSet() : nullptr;
}
}  // namespace

void PinnedBuiltin::pin() const {
    if (pinned_ || !obj_) return;
    if (proto::ProtoRootSet* rs = currentRootSet()) {
        rs->add(obj_);  // for the life of the root set; never removed
        pinned_ = true;
    }
}

const proto::ProtoObject* PinnedBuiltin::get(proto::ProtoContext* ctx) const {
    if (!obj_ || !ctx || ctx->space != space_) return nullptr;
    pin();
    return obj_;
}

const proto::ProtoObject* PinnedBuiltin::keep(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* obj) {
    if (!ctx || !obj || obj == PROTO_NONE) return obj;
    obj_ = obj;
    space_ = ctx->space;
    pinned_ = false;
    pin();
    return obj;
}

}  // namespace protojs
