#ifndef PROTOJS_DISASSEMBLER_H
#define PROTOJS_DISASSEMBLER_H

/**
 * Disassembler for the bytecode that ProtoInterpreter runs.
 *
 * `protojs --disasm script.js` compiles the script exactly as `protojs
 * script.js` would (QuickJS compile-only, then loadBytecode, which applies
 * the object-literal, write-group and specialiser rewrites), prints the
 * top-level function and every nested function, depth first, and exits
 * without running the script.
 *
 * The listing shows the bytecode after the loader's rewrites, that is, what
 * the dispatch loop executes. Constants, local names and closure-variable
 * names come from the QuickJS function the module was loaded from. Nothing
 * in the output depends on addresses or on the run, so the listings can be
 * kept as golden files (docs/vm-examples, docs/VM.md).
 */

#include <ostream>
#include <string>

struct JSContext;

namespace proto {
class ProtoContext;
}

namespace protojs {

struct ProtoBytecodeModule;

/** Prints `module`, loaded from the QuickJS function `quickjsBytecode`, and
 *  every function nested in it. */
void disassembleModule(JSContext* ctx, void* quickjsBytecode,
                       const ProtoBytecodeModule& module,
                       proto::ProtoContext* pContext, std::ostream& out);

/** Compiles and loads `source` as a script (the path `protojs file.js`
 *  takes) and prints it with disassembleModule. Returns 0 on success, 1 on a
 *  compile or load error (reported on `err`). The script is not run. */
int disassembleSource(JSContext* ctx, proto::ProtoContext* parent,
                      const std::string& source, const std::string& filename,
                      std::ostream& out, std::ostream& err);

}  // namespace protojs

#endif  // PROTOJS_DISASSEMBLER_H
