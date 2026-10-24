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
};

/** The instruction handler: `ctx` is the running context, `stack` this
 *  thread's activations, innermost last. */
using InstructionHandler = void (*)(proto::ProtoContext* ctx, std::vector<Activation>& stack);

/** A function's module loaded with its line tables: `root` and every function
 *  of its flat nested table.  Called on the loading thread, after the table
 *  has stopped growing. */
using ModuleLoadedHandler = void (*)(const ProtoBytecodeModule& root);

extern bool g_enabled;
inline bool enabled() { return g_enabled; }

/** Turns the hooks on.  Call once, before any JavaScript is compiled. */
void enable(InstructionHandler onInstruction, ModuleLoadedHandler onModuleLoaded);

/** While true on this thread the instruction handler is not called. */
void setSuppressed(bool suppressed);
bool suppressed();

// --- called by the interpreter and the loader (only when enabled()) -------
void pushActivation(proto::ProtoContext* ctx, const ProtoBytecodeModule* module,
                    const proto::ProtoObject* thisObj, const proto::ProtoObject** pGlobalRoot);
void popActivation();
void onInstruction(proto::ProtoContext* ctx, int pc);
void moduleLoaded(const ProtoBytecodeModule& root);

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
