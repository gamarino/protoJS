#ifndef PROTOJS_BYTECODE_SPECIALISER_H
#define PROTOJS_BYTECODE_SPECIALISER_H

/**
 * BytecodeSpecialiser -- post-codegen peephole pass that adds fused
 * "super-instructions" with inline Number fast paths in front of the
 * common loop shapes QuickJS emits.
 *
 * Patterns recognised (`get_loc` is any of get_loc0..3, get_loc8, get_loc
 * with an index up to 255, and get_loc_check):
 *
 *   P1 (accumulator), `s += i` with both locals:
 *       get_loc s; get_loc i; add; put_loc s     (or get_loc i; add_loc s)
 *         -> OP_proto_acc_loc8_loc8 s i skip
 *   P2 (comparison and jump), `i < n` with both locals as a loop test:
 *       get_loc i; get_loc n; lt; if_false[8] T
 *         -> OP_proto_lt_loc8_loc8_jfalse i n skip T
 *   P3 (comparison and jump), `i < N` with N a global (OP_get_var):
 *       get_loc i; get_var N; lt; if_false[8] T
 *         -> OP_proto_lt_loc_var_jfalse i N skip T
 *
 * The fused instruction is a guarded header: the original instructions
 * stay right after it.  Its fast path runs only when both operands are
 * Numbers (SmallIntegers or doubles) and skips them; in every other case execution falls
 * through into the original instructions, so BigInts, objects, strings,
 * the TDZ sentinel and undeclared globals keep their exact
 * semantics.  Every code offset (jumps, catch handlers, gosub targets,
 * with_* labels) is re-encoded through an old-to-new offset table, and a
 * sequence with a jump target inside it is not fused.  See
 * specialiseGuarded in the .cpp for the details.
 *
 * `PROTOJS_SPECIALISER=off` disables the pass; it is on by default and off
 * under the debugger.
 */

#include <cstdint>
#include <vector>

namespace protojs {

enum class SpecialiseMode {
    Off = 0,        ///< No-op pass-through.
    On = 1,         ///< Insert guarded fused headers.
};

/** Resolve the active mode from the env var `PROTOJS_SPECIALISER`: On
 *  when it is unset, "on" (or the former layout names "compact" and
 *  "nop"); Off for "off", an unknown value, and under the debugger. */
SpecialiseMode getSpecialiseMode();

/// Lengths of the fused headers (opcodes 244, 245, 246).
constexpr int kFusedAccLength = 4;
constexpr int kFusedLtLength = 8;
constexpr int kFusedLtVarLength = 9;

/** Instruction size of every opcode byte (QuickJS's final opcodes and
 *  protoJS's 244..250); 0 for a byte that is not a final opcode.  The one
 *  size table of the loader, the specialiser and the disassembler. */
const uint8_t* bytecodeOpcodeSizes();

/** Run the chosen pass.  When `mode == Off` returns a copy of `buf`
 *  unchanged (so the caller can always replace its bytecode buffer
 *  with the result).  When the pass cannot find any pattern, returns
 *  the unchanged buffer too. */
std::vector<uint8_t> specialise(const uint8_t* buf,
                                int len,
                                SpecialiseMode mode);

/**
 * Rewrites object literals so that they are built immutable and made
 * mutable once, after their last field (see the comment above the
 * implementation).  `levels[pc]` is the stack level before the instruction
 * at `pc` (0xffff: unreachable), as protojs_bytecode_stack_levels reports it
 * for the unmodified QuickJS bytecode in `code`.  Instruction sizes are kept,
 * so it runs before specialise().  Returns the number of literals rewritten;
 * PROTOJS_LITERAL_BUILD=off disables it.
 */
int markObjectLiterals(std::vector<uint8_t>& code, const uint16_t* levels);

/// Opcodes of a run of writes to one object published once
/// (markPutFieldGroups).  Both are 5 bytes, like the OP_put_field they
/// replace: opcode, group index (u16), position in the group (u16).
constexpr uint8_t OP_PROTO_PUT_FIELD_GROUP = 249;
constexpr uint8_t OP_PROTO_PUT_FIELD_GROUP_END = 250;

/**
 * A run of `obj.name = value` statements on one receiver that
 * markPutFieldGroups compiled into one publication.  Everything here is
 * static: the runtime checks it once, at the first write of the run, against
 * the live receiver and frame (see "Write groups" in ProtoInterpreter.cpp).
 */
struct PutFieldGroup {
    /// The atom written at each position, in program order.
    std::vector<uint32_t> atoms;
    /// Argument and local slots that statements after the first use as
    /// operands of arithmetic or comparison: they must hold values on which
    /// those operators cannot run code or throw.
    std::vector<uint16_t> operandArgs;
    std::vector<uint16_t> operandLocals;
    /// Fields of the receiver that statements after the first read.
    struct Read {
        uint32_t atom;
        uint16_t statement;  ///< position of the statement that reads it
        bool operand;        ///< also used as an arithmetic operand
    };
    std::vector<Read> reads;
};

/**
 * Finds runs of two or more consecutive `recv.name = value` statements on
 * the same receiver (`this`, an argument or a local) whose values cannot run
 * code (constants, argument / local / closure reads, arithmetic and
 * comparisons, reads of the receiver's own fields), and rewrites their
 * OP_put_field instructions to OP_PROTO_PUT_FIELD_GROUP / _END, appending the
 * group's description to `groups`.  `nameEligible(atom)` rejects names the
 * runtime gives special meaning to.  Instruction sizes are kept, so no jump
 * moves.  `extraStack` receives the extra operand-stack depth the rewritten
 * runs need (the run keeps each value on the stack until its last write).
 * Returns the number of runs rewritten; PROTOJS_PUTFIELD_GROUPS=off
 * disables it.
 */
int markPutFieldGroups(std::vector<uint8_t>& code,
                       bool (*nameEligible)(void* user, uint32_t atom), void* user,
                       std::vector<PutFieldGroup>& groups, int& extraStack);

}  // namespace protojs

#endif  // PROTOJS_BYTECODE_SPECIALISER_H
