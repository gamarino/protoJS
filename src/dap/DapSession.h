#ifndef PROTOJS_DAP_DAP_SESSION_H
#define PROTOJS_DAP_DAP_SESSION_H

namespace protojs::dap {

/** Runs a program the way the command line does (`protojs <program> args...`)
 *  and answers its exit status. */
using ProgramRunner = int (*)(int argc, char** argv);

/**
 * `protojs --dap`: a Debug Adapter Protocol server over stdin/stdout
 * (docs/DEBUGGING.md).  Waits for the client's `launch` and
 * `configurationDone`, turns the engine's debug hooks on
 * (debugger/DebugHooks.h), runs the program on this thread with
 * `runProgram`, and answers the program's exit status once the client has
 * disconnected.  `argv0` is protojs's own argv[0].
 */
int runDapSession(char* argv0, ProgramRunner runProgram);

} // namespace protojs::dap

#endif // PROTOJS_DAP_DAP_SESSION_H
