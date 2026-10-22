#ifndef PROTOJS_PROTO_INTERPRETER_H
#define PROTOJS_PROTO_INTERPRETER_H

/**
 * ProtoCore interpreter: runs QuickJS bytecode using only ProtoContext and ProtoObject.
 * Stack and locals are ProtoObject*; no JSValue during execution.
 */

#include "ProtoBytecodeModule.h"
#include "protoCore.h"
#include <exception>
#include <memory>
#include <vector>

namespace protojs {

/**
 * A JavaScript exception in flight is a C++ exception: `throw` (OP_throw) and
 * a native that raises (signalNativeException) throw a JSException carrying
 * the thrown value, which may be any value (`throw undefined` included).
 *
 * JSException is protoCore's one exception type, proto::ProtoException, under
 * the name the interpreter throws; it adds no state.  protoCore pins the value
 * while any copy exists, so unwinding past the contexts that allocated it
 * cannot reclaim it.  Every handler -- a JavaScript `catch`, a boundary to
 * QuickJS, an event-loop job -- catches the base, `const proto::ProtoException&`,
 * so that the errors protoCore itself raises (through the error factory, see
 * installErrorFactory) and the errors a module written in C++ or in another
 * language raises are caught exactly like a JavaScript throw.  See
 * docs/EXCEPTIONS.md.
 */
class JSException : public proto::ProtoException {
public:
    using proto::ProtoException::ProtoException;
};

/** Throws `value` as a JSException (protoCore pins it while it is in flight). */
[[noreturn]] void throwJS(proto::ProtoContext* ctx, const proto::ProtoObject* value);

/**
 * Raises the JavaScript `Error` that stands for a foreign C++ exception (a
 * `std::exception` that a native or a module method let escape): its message is
 * `what`.  A C++ standard exception is a defect of the module, not a JavaScript
 * value; it never crosses the interpreter as is.  See callNative.
 */
[[noreturn]] void raiseForeignCppError(proto::ProtoContext* ctx, const char* what);

/**
 * Calls a native (a protoCore ProtoMethod: a built-in, an addon function, a
 * method of a UMD module) from the interpreter.  A proto::ProtoException the
 * method raises passes through untouched; a `std::exception` is translated into
 * a JavaScript `Error` with `what()` as its message (raiseForeignCppError).
 */
inline const proto::ProtoObject* callNative(proto::ProtoMethod method,
                                            proto::ProtoContext* ctx,
                                            const proto::ProtoObject* self,
                                            const proto::ProtoArguments* args,
                                            const proto::ProtoKeywordArguments* kwargs = nullptr) {
    try {
        return method(ctx, self, nullptr, args, kwargs);
    } catch (const std::exception& e) {
        raiseForeignCppError(ctx, e.what());
    }
}

/**
 * Whether `v` is an object of a module written outside JavaScript (in C++, or
 * in another language on protoCore): a plain protoCore object -- not
 * callable, not a Proxy -- whose prototype chain holds no JavaScript
 * Object.prototype and that has no JavaScript [[Prototype]] override.  The
 * error types and error values of such a module are this kind of object
 * (docs/EXCEPTIONS.md).  For them protoJS extends two operations:
 * `x instanceof T` with such a T tests T against x's prototype chain, and a
 * conversion to a primitive answers foreignObjectString.
 */
bool isForeignObject(proto::ProtoContext* ctx, const proto::ProtoObject* v);

/**
 * The string form of a foreign object (isForeignObject): "<name>: <message>"
 * from its `name` and `message` attributes, as Error.prototype.toString
 * builds it, or "[object Object]" when it has neither.
 */
const proto::ProtoObject* foreignObjectString(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* v);

/**
 * Installs protoJS's error factory in `space` (ProtoSpace::setErrorFactory):
 * protoCore's own errors are raised as the JavaScript error classes of the
 * calling thread's realm.  Called once, when the space is created.
 *
 *   ProtoErrorKind     JavaScript class
 *   InvalidArgument    SyntaxError   (BigInt("x"))
 *   TypeMismatch       TypeError
 *   Overflow           RangeError
 *   DivisionByZero     RangeError    (1n / 0n, 1n % 0n: "Division by zero")
 *   ArityMismatch      TypeError
 */
void installErrorFactory(proto::ProtoSpace* space);

/**
 * Run the loaded bytecode module with the given `this` value, arguments and
 * global object.
 *
 * - `thisObj`: JS `this` binding for this frame.
 * - `args`: positional arguments for the frame (may be null for top-level).
 * - `pGlobalRoot`: pointer to current global object; used for reads and updated on put_field.
 *
 * Returns the result ProtoObject, or PROTO_NONE on a void return.  A JavaScript
 * exception the code does not catch propagates as a proto::ProtoException (a
 * JSException) carrying the thrown value.
 * QuickJS (JSContext*) is not used during execution; atoms must be pre-resolved
 * via preResolveAllAtoms() before calling this function.
 */
const proto::ProtoObject* runBytecode(proto::ProtoContext* pContext,
                                      const ProtoBytecodeModule* module,
                                      const proto::ProtoObject* thisObj,
                                      const proto::ProtoList* args,
                                      const proto::ProtoObject** pGlobalRoot);

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
    const proto::ProtoArguments* args);

/**
 * A descriptor for a call made inside one full expression, from a temporary:
 *   fn(ctx, self, nullptr, argsView(proto::ProtoListArguments(ctx, list)), nullptr)
 * The temporary lives until the end of the full expression, which outlasts the
 * call; never keep the pointer.
 */
inline const proto::ProtoArguments* argsView(const proto::ProtoArguments& args) { return &args; }

/**
 * The same call with the arguments in a ProtoList (read in place when it is a
 * small list, otherwise copied into a stack buffer; protoCore's
 * ProtoListArguments).  `args` may be null.
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
/**
 * IsConstructor (ECMA-262 §7.2.4) for a bytecode closure: true for an ordinary
 * `function` or a class constructor, false for an arrow function, method,
 * accessor, generator or async function, and false for anything that is not a
 * bytecode closure.
 */
bool isBytecodeConstructor(proto::ProtoContext* ctx, const proto::ProtoObject* fn);

const proto::ProtoObject* getNullSentinel();
const proto::ProtoObject* getUndefinedSentinel();

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
 * Eagerly initializes the thread-local undefined sentinel using @p ctx.
 * Safe to call multiple times; a no-op if the sentinel is already set.
 */
void initializeUndefinedSentinel(proto::ProtoContext* ctx);

/**
 * Returns a pointer to the thread-local current global root.
 * Valid only while a runBytecode frame is active on this thread.
 * Used by native helpers that need to read/write global properties.
 */
const proto::ProtoObject** getCurrentGlobalRoot();

/**
 * Raise `errorObj` from a native function: throws it as a JSException (a
 * proto::ProtoException; protoCore pins the value).  Does not return.
 */
[[noreturn]] void signalNativeException(const proto::ProtoObject* errorObj);

/**
 * The ECMAScript global object for the value of a global root slot.  The root
 * script's top-level bindings live on a child of the global object (the
 * module-scope split in runBytecode); for that scope this answers its parent,
 * the object `globalThis` names.  Any other root is answered unchanged.
 */
const proto::ProtoObject* globalObjectForRoot(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* root);

/**
 * Create an Error-like ProtoObject of the given type (e.g. "TypeError") with message.
 * Equivalent to the internal makeError() helper but accessible from native code.
 * The object inherits from the matching error prototype so `instanceof` works.
 *
 * The prototype is read from `globalRoot`, by default the global of the active
 * runBytecode frame.  Code running outside any frame -- an event-loop callback
 * that builds an error before calling into JavaScript -- has no such global and
 * must pass the wrapper's (getNativeGlobalRootPtr()); otherwise the result is a
 * plain object that is not an `instanceof Error`.
 */
const proto::ProtoObject* makeNativeError(proto::ProtoContext* ctx,
                                          const char* errorType,
                                          const char* message,
                                          const proto::ProtoObject* const* globalRoot = nullptr);

/**
 * Spec ToNumber(value): coerces a JS value to a number (Integer/Double).
 * Invokes valueOf / toString on objects per ToPrimitive(hint:"number").
 * Returns PROTO_NONE if a callback raised an exception (drain via
 * hasCallException()). Use this from native methods that need spec-correct
 * coercion (Math.max/min, etc.).
 */
const proto::ProtoObject* jsToNumber(proto::ProtoContext* context,
                                     const proto::ProtoObject* value);

/**
 * True while the interpreter has a frame on this thread's stack.  A microtask
 * checkpoint runs only when it is false (jobs run on an empty stack).
 */
bool interpreterIsRunning();

/**
 * The module the calling thread's interpreter is running (the one closures
 * created right now are resolved against), or nullptr outside any frame.
 */
const ProtoBytecodeModule* currentInterpreterModule();

/**
 * Publish the module and global root that callJSFunction and runBytecode
 * resolve against, for code that runs JavaScript from outside any frame --
 * the microtask checkpoint, event-loop callbacks.  Restores the previous
 * values on destruction.
 */
class InterpreterEntryScope {
public:
    InterpreterEntryScope(const ProtoBytecodeModule* module,
                          const proto::ProtoObject** globalRoot);
    ~InterpreterEntryScope();
    InterpreterEntryScope(const InterpreterEntryScope&) = delete;
    InterpreterEntryScope& operator=(const InterpreterEntryScope&) = delete;
private:
    const ProtoBytecodeModule* prevCurrent_;
    const ProtoBytecodeModule* prevRoot_;
    const proto::ProtoObject** prevGlobalRoot_;
};

/**
 * Construct(target, args) from native code, as Reflect.construct(target,
 * args) does.  Signals a TypeError (hasCallException) when target is not a
 * constructor.
 */
const proto::ProtoObject* jsConstruct(proto::ProtoContext* ctx,
                                      const proto::ProtoObject* target,
                                      const proto::ProtoList* args);

/**
 * Resume an async function or async generator suspended at an `await`, from
 * the reaction job of the awaited promise: `value` is pushed as the result of
 * the await, or thrown there when `isThrow`.
 */
void resumeAwait(proto::ProtoContext* ctx, const proto::ProtoObject* continuation,
                 const proto::ProtoObject* value, bool isThrow);

/**
 * The reaction of AsyncGeneratorAwaitReturn (§27.6.3.9): the awaited return
 * value of a completed or not-yet-started async generator has settled.
 */
void asyncGeneratorReturnSettled(proto::ProtoContext* ctx, const proto::ProtoObject* generator,
                                 const proto::ProtoObject* value, bool isThrow);

} // namespace protojs

#endif /* PROTOJS_PROTO_INTERPRETER_H */
