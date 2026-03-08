#ifndef PROTOJS_PROTO_INTERPRETER_H
#define PROTOJS_PROTO_INTERPRETER_H

/**
 * ProtoCore interpreter: runs QuickJS bytecode using only ProtoContext and ProtoObject.
 * Stack and locals are ProtoObject*; no JSValue during execution.
 */

#include "ProtoBytecodeModule.h"
#include "headers/protoCore.h"
#include <vector>

namespace protojs {

/**
 * Run the loaded bytecode module with the given `this` value, arguments and
 * global object.
 *
 * - `thisObj`: JS `this` binding for this frame.
 * - `args`: positional arguments for the frame (may be null for top-level).
 * - `pGlobalRoot`: pointer to current global object; used for reads and updated on put_field.
 * - `outException`: if non-null, receives the thrown value on OP_throw / TDZ errors
 *   instead of crashing. The caller must check `*outException != PROTO_NONE`.
 *
 * Returns the result ProtoObject, or PROTO_NONE on error/void return.
 * QuickJS (JSContext*) is not used during execution; atoms must be pre-resolved
 * via preResolveAllAtoms() before calling this function.
 */
const proto::ProtoObject* runBytecode(proto::ProtoContext* pContext,
                                      const ProtoBytecodeModule* module,
                                      const proto::ProtoObject* thisObj,
                                      const proto::ProtoList* args,
                                      const proto::ProtoObject** pGlobalRoot,
                                      const proto::ProtoObject** outException = nullptr);

} // namespace protojs

#endif /* PROTOJS_PROTO_INTERPRETER_H */
