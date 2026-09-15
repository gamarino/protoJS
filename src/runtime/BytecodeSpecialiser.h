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

}  // namespace protojs

#endif  // PROTOJS_BYTECODE_SPECIALISER_H
