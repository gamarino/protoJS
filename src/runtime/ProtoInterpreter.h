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
 * Invoke a protoCore-callable (bytecode function or ProtoMethod) from
 * outside an active runBytecode call — e.g. from an event-loop callback,
 * a worker-thread completion handler, or a Deferred resolution.  Restores
 * the thread-local module and global-root pointers from the supplied
 * pointers, then delegates to callJSFunction, then restores the previous
 * thread-local state.
 *
 * `module` and `globalRoot` should typically come from the wrapper
 * (`wrapper.getRootModule()` / `wrapper.getNativeGlobalRootPtr()`).
 *
 * Returns the callee's result, or PROTO_NONE on error.
 */
const proto::ProtoObject* callJSFunctionFromAsync(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* fn,
    const proto::ProtoObject* thisVal,
    const proto::ProtoList* args,
    const ProtoBytecodeModule* module,
    const proto::ProtoObject** globalRoot);

/**
 * Eagerly initializes the thread-local null sentinel using @p ctx.
 * Safe to call multiple times; a no-op if the sentinel is already set.
 * Intended for use by JSContextWrapper so TypeBridge null round-trips work
 * before the first script execution.
 */
void initializeNullSentinel(proto::ProtoContext* ctx);

/**
 * Returns a pointer to the thread-local current global root.
 * Valid only while a runBytecode frame is active on this thread.
 * Used by native helpers that need to read/write global properties.
 */
const proto::ProtoObject** getCurrentGlobalRoot();

/**
 * Signal a pending exception from within a native ProtoMethod function.
 *
 * Call this before returning PROTO_NONE from a native method to indicate that
 * the return represents a thrown exception rather than undefined.  The interpreter
 * will promote it to a real exception after the native call returns.
 *
 * Only one pending exception is stored at a time; calling this twice before the
 * interpreter checks it will overwrite the first exception.
 */
void signalNativeException(const proto::ProtoObject* errorObj);

/**
 * Create an Error-like ProtoObject of the given type (e.g. "TypeError") with message.
 * Equivalent to the internal makeError() helper but accessible from native code.
 * The object inherits from the matching error prototype so `instanceof` works.
 */
const proto::ProtoObject* makeNativeError(proto::ProtoContext* ctx,
                                          const char* errorType,
                                          const char* message);

/**
 * Returns true if a JS callback called via callJSFunction() set a pending
 * exception that has not yet been consumed.  Native methods that invoke user
 * callbacks (forEach, map, filter, every, some, …) must call this after each
 * callJSFunction() and return PROTO_NONE immediately when true, so the
 * exception is propagated rather than silently discarded.
 */
bool hasCallException();

} // namespace protojs

#endif /* PROTOJS_PROTO_INTERPRETER_H */
