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
 * Run the loaded bytecode module with the given global object as scope.
 * Returns the result ProtoObject, or PROTO_NONE on error/exception.
 * globalObj: used as "this" and as the variable scope for global names.
 */
const proto::ProtoObject* runBytecode(proto::ProtoContext* pContext,
                                      const ProtoBytecodeModule* module,
                                      const proto::ProtoObject* globalObj,
                                      JSContext* jsContextForAtoms);

} // namespace protojs

#endif /* PROTOJS_PROTO_INTERPRETER_H */
