#ifndef PROTOJS_MATHBUILTIN_H
#define PROTOJS_MATHBUILTIN_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Ensure the Math object is registered in the global root.
 * Adds all standard Math constants and methods.
 * Idempotent — no-op when "Math" is already present.
 */
void ensureMathObject(proto::ProtoContext* ctx,
                      const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_MATHBUILTIN_H
