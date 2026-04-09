#ifndef PROTOJS_GENERATOR_FRAME_H
#define PROTOJS_GENERATOR_FRAME_H

/**
 * Shared definitions for the generator protocol.
 * CatchFrame is moved here from ProtoInterpreter.cpp so it can be
 * referenced by both runBytecode and generatorNext/Return/Throw.
 */

namespace protojs {

/**
 * Represents a single try/catch entry on the interpreter's catch stack.
 * Previously a local struct inside runBytecode; now shared so generator
 * resume can restore the catch stack across runBytecode invocations.
 */
struct CatchFrame {
    int           handler_pc;
    unsigned long placeholder_stack_pos;
};

// ---------------------------------------------------------------------------
// Attribute key names used to store generator state on the iterator object.
// All keys start and end with __ to avoid collisions with user properties.
// ---------------------------------------------------------------------------
static constexpr const char* kGenPc       = "__gen_pc__";
static constexpr const char* kGenLocals   = "__gen_locals__";
static constexpr const char* kGenThis     = "__gen_this__";
static constexpr const char* kGenMod      = "__gen_mod__";
static constexpr const char* kGenState    = "__gen_state__";   // 0=suspended, 1=completed
static constexpr const char* kGenNcc      = "__gen_ncc__";     // number of catch frames
// Individual catch frame keys: __gen_cc_N_pc__ and __gen_cc_N_sp__
// (N = decimal index, generated at runtime)

} // namespace protojs

#endif /* PROTOJS_GENERATOR_FRAME_H */
