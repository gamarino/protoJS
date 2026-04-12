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

/**
 * Call a JS function (bytecode closure or native ProtoMethod) from native C++ code.
 *
 * Must be called only while a runBytecode frame is active on the same thread
 * (the required thread-locals t_currentModule / t_currentGlobalRoot must be set).
 *
 * - fn:      a ProtoObject with __bytecode_id__ (closure) or isMethod() (native method).
 * - thisVal: the `this` binding for the call (use PROTO_NONE for non-method calls).
 * - args:    positional arguments.
 *
 * Returns PROTO_NONE on error or when the function cannot be resolved.
 * Exceptions thrown by callees are silently suppressed (not propagated to the caller).
 */
const proto::ProtoObject* callJSFunction(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* fn,
    const proto::ProtoObject* thisVal,
    const proto::ProtoList* args);

/**
 * Returns the thread-local null sentinel — the ProtoObject that represents JS null.
 * Returns nullptr if called before runBytecode has been entered on this thread.
 */
const proto::ProtoObject* getNullSentinel();

/**
 * Returns a pointer to the thread-local current global root.
 * Valid only while a runBytecode frame is active on this thread.
 * Used by native helpers that need to read/write global properties.
 */
const proto::ProtoObject** getCurrentGlobalRoot();

} // namespace protojs

#endif /* PROTOJS_PROTO_INTERPRETER_H */
