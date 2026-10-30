#ifndef PROTOJS_DATAVIEWPROTOTYPE_H
#define PROTOJS_DATAVIEWPROTOTYPE_H

#include "protoCore.h"

namespace protojs {

void ensureDataViewConstructor(proto::ProtoContext* ctx,
                               const proto::ProtoObject** globalRoot);

/** True for a DataView instance (one with its own [[ViewedArrayBuffer]]). */
bool isDataView(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

/**
 * new DataView(buffer[, byteOffset[, byteLength]]) with NewTarget's prototype
 * `proto` (ECMA-262 §25.3.2.1): a TypeError when buffer is not an
 * ArrayBuffer, a RangeError for an offset or length outside it.  Returns
 * PROTO_NONE with the exception signalled on failure.
 */
const proto::ProtoObject* constructDataView(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* proto,
                                            const proto::ProtoList* args);

} // namespace protojs

#endif // PROTOJS_DATAVIEWPROTOTYPE_H
