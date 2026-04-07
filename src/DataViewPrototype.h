#ifndef PROTOJS_DATAVIEWPROTOTYPE_H
#define PROTOJS_DATAVIEWPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

void ensureDataViewConstructor(proto::ProtoContext* ctx,
                               const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_DATAVIEWPROTOTYPE_H
