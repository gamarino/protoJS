#include "ThreadIdentity.h"

namespace protojs {

ThreadIdentity captureThreadIdentity(proto::ProtoContext* ctx) {
    ThreadIdentity id;
    captureInterpreterIdentity(ctx, id);
    captureBigIntIdentity(id);
    captureRegExpIdentity(id);
    captureProtoCoreBindingsIdentity(id);
    return id;
}

void adoptThreadIdentity(const ThreadIdentity& identity) {
    adoptInterpreterIdentity(identity);
    adoptBigIntIdentity(identity);
    adoptRegExpIdentity(identity);
    adoptProtoCoreBindingsIdentity(identity);
}

}  // namespace protojs
