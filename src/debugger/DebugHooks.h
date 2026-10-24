#ifndef PROTOJS_DEBUGGER_DEBUG_HOOKS_H
#define PROTOJS_DEBUGGER_DEBUG_HOOKS_H

/**
 * The engine side of the debugger: what the interpreter reports while a
 * debug session (`protojs --dap`, src/dap) is active, and what the debugger
 * reads from a stopped frame.
 *
 * Everything here is off unless enable() was called, once, before any
 * JavaScript is compiled.  Off, the interpreter pays one predictable branch
 * per dispatched instruction and per call, and the loader keeps no line
 * tables.  On:
 *
 *   - loadBytecode keeps each function's pc -> line table, its slot names
 *     and its file name (ProtoBytecodeModule::debugLines & co.), and turns
 *     the bytecode specialiser off, so the byte offsets the interpreter runs
 *     are the ones the line tables describe;
 *   - every activation of a bytecode function (a call, a generator or async
 *     resume) is pushed on this thread's activation stack, and popped when it
 *     ends, by any path;
 *   - every JavaScript exception is reported to the throw handler before it
 *     is thrown (throwJS, the `throw` opcode, protoCore's error factory), and
 *     every try range that takes one to the catch handler;
 *   - before every instruction the interpreter calls the installed
 *     instruction handler with the current context and the activation stack,
 *     the innermost activation's `pc` already set to the instruction's byte
 *     offset.  The handler may block (a stop) and may run JavaScript (an
 *     evaluate); while it does, setSuppressed(true) keeps the code it runs
 *     from re-entering it.
 *
 * Activations hold raw pointers into the interpreter's frames: valid only
 * while the frame is on the stack, i.e. inside the handler, on this thread.
 */

#include "protoCore.h"
#include <string>
#include <vector>

namespace protojs {

struct ProtoBytecodeModule;

namespace debughooks {

/** One activation of a bytecode function on this thread. */
struct Activation {
    proto::ProtoContext* ctx;                 // the activation's context (slots)
    const ProtoBytecodeModule* module;
    const proto::ProtoObject* thisObj;
    const proto::ProtoObject** pGlobalRoot;
    int pc;        // byte offset of the instruction being executed
    int lastLine;  // line of the last instruction reported (0: none yet)
    int lastPc;
    const void* catchStack;   // the activation's open try ranges (the
                              // interpreter's std::vector<CatchFrame>)
};

/** The instruction handler: `ctx` is the running context, `stack` this
 *  thread's activations, innermost last. */
using InstructionHandler = void (*)(proto::ProtoContext* ctx, std::vector<Activation>& stack);

/** Where a thrown value comes from. */
enum class ThrowKind {
    Statement,   // a `throw` statement (OP_throw)
    Runtime,     // an opcode or a native function (TypeError, RangeError...)
    Kernel,      // protoCore's error factory (BigInt division by zero...)
};

/** A JavaScript exception about to be thrown on this thread, before the C++
 *  exception that carries it: the frames, their locals and the activation
 *  stack are intact.  The handler may block and run code, like the
 *  instruction handler.  `value` is rooted only by the caller's C++ frame:
 *  the handler pins it if it allocates. */
using ThrowHandler = void (*)(proto::ProtoContext* ctx, std::vector<Activation>& stack,
                              const proto::ProtoObject* value, ThrowKind kind);

/** A JavaScript try range of this thread took `value`: a `catch` clause, or,
 *  with `finallyOnly`, a `finally` block that will rethrow it. */
using CatchHandler = void (*)(const proto::ProtoObject* value, bool finallyOnly);

/** A function's module loaded with its line tables: `root` and every function
 *  of its flat nested table.  Called on the loading thread, after the table
 *  has stopped growing. */
using ModuleLoadedHandler = void (*)(const ProtoBytecodeModule& root);

extern bool g_enabled;
inline bool enabled() { return g_enabled; }

/** Turns the hooks on.  Call once, before any JavaScript is compiled. */
void enable(InstructionHandler onInstruction, ModuleLoadedHandler onModuleLoaded);

/** Installs the exception handlers (either may be null).  Call after
 *  enable(), before any JavaScript runs. */
void setExceptionHandlers(ThrowHandler onThrow, CatchHandler onCatch);

/** While true on this thread neither the instruction handler nor the throw
 *  handler is called. */
void setSuppressed(bool suppressed);
bool suppressed();

/** Engine-internal exceptions: while one of these is alive on this thread,
 *  what is thrown is not reported to the throw handler.  For the native code
 *  that throws and catches as a probe (a kernel parse that fails, an abrupt
 *  completion the specification turns into `false`), never for code that
 *  runs the user's functions. */
struct InternalTry {
    InternalTry();
    ~InternalTry();
    InternalTry(const InternalTry&) = delete;
    InternalTry& operator=(const InternalTry&) = delete;
    bool on;
};

/** A native that catches whatever the JavaScript it calls throws and turns it
 *  into something else (a promise rejection, a discarded error): while one of
 *  these is alive, an exception thrown by the activations above it is caught
 *  (see throwWillBeCaught).  Natives that rethrow need none. */
struct CatchingNative {
    CatchingNative();
    ~CatchingNative();
    CatchingNative(const CatchingNative&) = delete;
    CatchingNative& operator=(const CatchingNative&) = delete;
    bool on;
};

/** Whether an exception thrown now on this thread will be caught: by a
 *  JavaScript `catch` of an activation on the stack (a `finally` that
 *  rethrows does not count), by an async function (it rejects the
 *  function's promise) or by a CatchingNative, before it leaves the
 *  outermost activation.  Natives between activations are assumed to
 *  propagate it. */
bool throwWillBeCaught(const std::vector<Activation>& stack);

// --- called by the interpreter and the loader (only when enabled()) -------
void pushActivation(proto::ProtoContext* ctx, const ProtoBytecodeModule* module,
                    const proto::ProtoObject* thisObj, const proto::ProtoObject** pGlobalRoot,
                    const void* catchStack);
void popActivation();
void onInstruction(proto::ProtoContext* ctx, int pc);
/** The innermost activation, of context `ctx`, is at byte offset `pc` (a
 *  throw before its first instruction ran: a resume's throw completion). */
void setPc(proto::ProtoContext* ctx, int pc);
void onThrow(proto::ProtoContext* ctx, const proto::ProtoObject* value, ThrowKind kind);
void onCatch(const proto::ProtoObject* value, bool finallyOnly);
void moduleLoaded(const ProtoBytecodeModule& root);

/** Whether activation `a` has an open try range with a `catch` clause
 *  (defined in the interpreter, which knows the catch frames and opcodes). */
bool activationCatches(const Activation& a);

/** This thread's activation stack, innermost last. */
std::vector<Activation>& activations();

/** The source line of byte offset `pc` of `module` (0 when unknown).  With
 *  `prologue`, also whether the instruction comes before the function's first
 *  statement (hoisted declarations, parameter setup): not a place to stop. */
int lineForPc(const ProtoBytecodeModule* module, int pc, bool* prologue = nullptr);

/** A named variable of a frame, its value read through its closure cell. */
struct FrameVar {
    enum Kind { Argument, Local, Closure };
    std::string name;
    const proto::ProtoObject* value;
    Kind kind;
};

/** The arguments, locals and closure variables of activation `a` (defined in
 *  the interpreter, which knows the slot layout and the closure cells). */
void collectFrameVars(const Activation& a, std::vector<FrameVar>& out);

} // namespace debughooks
} // namespace protojs

#endif // PROTOJS_DEBUGGER_DEBUG_HOOKS_H
