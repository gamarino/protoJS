#ifndef PROTOJS_PROTO_INTERPRETER_H
#define PROTOJS_PROTO_INTERPRETER_H

/**
 * ProtoCore interpreter: runs QuickJS bytecode using only ProtoContext and ProtoObject.
 * Stack and locals are ProtoObject*; no JSValue during execution.
 */

#include "ProtoBytecodeModule.h"
#include "headers/protoCore.h"
#include <vector>

struct JSContext;

namespace protojs {

/**
 * Run the loaded bytecode module with the given `this` value, arguments and
 * global object.
 *
 * - `thisObj`: JS `this` binding for this frame.
 * - `args`: positional arguments for the frame (may be null for top-level).
 * - `pGlobalRoot`: pointer to current global object; used for reads and updated on put_field (Phase 6).
 *
 * Returns the result ProtoObject, or PROTO_NONE on error/exception.
 */
const proto::ProtoObject* runBytecode(proto::ProtoContext* pContext,
                                      const ProtoBytecodeModule* module,
                                      const proto::ProtoObject* thisObj,
                                      const proto::ProtoList* args,
                                      const proto::ProtoObject** pGlobalRoot,
                                      JSContext* jsContextForAtoms);

} // namespace protojs

#endif /* PROTOJS_PROTO_INTERPRETER_H */
