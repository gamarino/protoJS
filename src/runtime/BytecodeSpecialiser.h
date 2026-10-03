#ifndef PROTOJS_BYTECODE_SPECIALISER_H
#define PROTOJS_BYTECODE_SPECIALISER_H

/**
 * BytecodeSpecialiser — post-codegen peephole pass that fuses common
 * accumulator-loop sequences emitted by QuickJS into single-dispatch
 * "super-instructions" with inline SmallInt fast paths.
 *
 * Parallel of protoPython's peephole specialiser (protoPython commit
 * 48d81bbd), adapted to QuickJS's variable-length bytecode.  Where
 * protoPython could NOP-pad in place because every instruction is a
 * fixed (op, arg) pair, QuickJS opcodes range from 1 to 13 bytes — so a
 * NOP-pad rewrite is one valid option, and a compact rewrite that REMAPS
 * jump targets through a translation table is the other.  Both are
 * implemented here; pick via `PROTOJS_SPECIALISER=off|nop|compact`
 * (default: `compact`; see specialiseModeFromEnv in the .cpp).
 *
 * Patterns recognised
 * -------------------
 *
 *   P1 (accumulator):    `s += i` where both are locals (loc8)
 *       get_loc8 s; get_loc8 i; add; put_loc8 s   (7 bytes)
 *         → OP_proto_acc_loc8_loc8 s i             (3 bytes)
 *
 *   P2 (comparison-jump): `if (i < n) ... else jump T`  (loc8 both)
 *       get_loc8 i; get_loc8 n; lt; if_false T    (10 bytes)
 *         → OP_proto_lt_loc8_loc8_jfalse i n T     (7 bytes)
 *
 * Together these cover the inner-loop shape of every `for (let i=0;
 * i<n; i++) s += i` style benchmark, and (per the protoPython A/B)
 * are responsible for the bulk of the win.
 *
 * Why two implementations
 * -----------------------
 *
 *  - **NOP-pad ("nop" mode)** — replace the matched bytes with the
 *    fused opcode followed by OP_nop bytes that fill the remaining
 *    slots.  Bytecode length unchanged; every jump in the rest of
 *    the bytecode still points at the same byte offset, so no remap
 *    is needed.  Simpler and safer; pays a few NOP dispatches per
 *    iteration (~1 ns each — usually negligible against the savings,
 *    but visible on micro-benches).
 *
 *  - **Compact + remap ("compact" mode)** — emit a shorter rewritten
 *    bytecode buffer (the fused opcode is smaller than the sequence
 *    it replaces).  Build a remap table `old_pc → new_pc`, then walk
 *    the new buffer once more rewriting every relative jump offset
 *    (`if_false`, `if_true`, `goto`, etc.) so its target lands at
 *    the equivalent new position.  Tighter dispatch (better icache),
 *    no wasted NOPs, but more complex and requires a complete opcode
 *    size table to walk the variable-length instruction stream.
 *
 * Public API
 * ----------
 *
 *   specialise(buf, len, mode) returns a possibly-rewritten byte
 *   sequence.  If no pattern matches or mode is `Off`, the input is
 *   returned untouched (vector copy is constant-time, the result is
 *   either reusable as-is or short-lived).
 */

#include <cstdint>
#include <vector>

namespace protojs {

enum class SpecialiseMode {
    Off = 0,        ///< No-op pass-through.
    NopPad = 1,     ///< Rewrite in place, NOP-pad trailing bytes.
    Compact = 2,    ///< Rewrite shorter, remap jump targets.
};

/** Resolve the active mode from the env var `PROTOJS_SPECIALISER`.
 *  Returns Off if unset or invalid. */
SpecialiseMode getSpecialiseMode();

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
