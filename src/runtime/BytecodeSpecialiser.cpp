#include "BytecodeSpecialiser.h"
#include "QuickJSOpcodeEnum.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace protojs {

// ─────────────────────────────────────────────────────────────────
// New fused opcodes.  Allocated from the byte range 244..255 — the
// space above QuickJS's `OP_COUNT` (244 DEF()s) and below the 256-
// entry dispatch table.  Keep the assignments in sync with
// ProtoInterpreter.cpp's dispatch_table[] population.
// ─────────────────────────────────────────────────────────────────

/// `OP_proto_acc_loc8_loc8 dst src`  (3 bytes)
/// Fused form of:
///   OP_get_loc8 dst
///   OP_get_loc8 src
///   OP_add
///   OP_put_loc8 dst
/// Semantics: `local[dst] = local[dst] + local[src]`.  No stack
/// effect — the value never reaches the operand stack.  SmallInt
/// fast path inlined; falls back to OP_add otherwise.
constexpr uint8_t OP_PROTO_ACC_LOC8_LOC8 = 244;

/// `OP_proto_lt_loc8_loc8_jfalse a b target` (7 bytes: 1 + 1 + 1 + 4)
/// Fused form of:
///   OP_get_loc8 a
///   OP_get_loc8 b
///   OP_lt
///   OP_if_false target          (5 bytes, target is signed 32-bit
///                                relative offset)
/// Semantics: if `local[a] < local[b]` continue; else jump to
/// `pc + 7 + target`.  SmallInt fast path inlined.
constexpr uint8_t OP_PROTO_LT_LOC8_LOC8_JFALSE = 245;

/// `OP_PROTO_LT_LOC_VAR_JFALSE locIdx varIdx_u16 offset_s32` (8 bytes)
/// Fuses the *closure-or-global* form of the loop test:
///     get_loc_check locIdx ; get_var varIdx ; lt ; if_false[8] target
/// `varIdx` is the closure-symbol / global-var index (u16, the format
/// QuickJS emits for `OP_get_var`).  Stored offset is 32-bit signed
/// even when the source used `if_false8` — widened at match time so
/// the dispatched handler stays uniform.
constexpr uint8_t OP_PROTO_LT_LOC_VAR_JFALSE = 246;

constexpr int FUSED_ACC_LEN = 3;
constexpr int FUSED_LT_JF_LEN = 7;
constexpr int FUSED_LT_LOC_VAR_JF_LEN = 8;

// ─────────────────────────────────────────────────────────────────
// QuickJS opcode size table.  Hand-rolled from quickjs-opcode.h
// keeping only the opcodes the peephole walker has to skip past.
// Anything not listed uses kDefaultSize and the walker bails (no
// rewrite for that function).  Padded conservatively.
// ─────────────────────────────────────────────────────────────────

namespace {

// Full 256-entry opcode→size table built from quickjs-opcode.h via the
// same DEF() macro trick the loader uses (ProtoBytecodeLoader.cpp).
// Each QuickJS DEF opcode contributes its declared `size`; def() temp
// opcodes get 0 (they never appear in final bytecode and any encounter
// is a corruption signal).  Our fused opcodes are spliced in by index
// after the include expands.
const uint8_t* getOpcodeSizes() {
    static const uint8_t* sizes = []() {
        // Step 1: pull QuickJS's DEF-table into entries [0..OP_COUNT).
        static uint8_t table[256];
        std::memset(table, 0, sizeof(table));
        {
            static const uint8_t qjs[] = {
                #define FMT(f)
                #define DEF(id, size, n_pop, n_push, f) (uint8_t)(size),
                #define def(id, size, n_pop, n_push, f)
                #include "quickjs-opcode.h"
                #undef def
                #undef DEF
                #undef FMT
                0  // sentinel
            };
            const size_t qjs_count = (sizeof(qjs) / sizeof(qjs[0])) - 1;
            const size_t n = qjs_count < 256 ? qjs_count : 256;
            std::memcpy(table, qjs, n);
        }
        // Step 2: add our fused opcodes.  Their byte values are
        // outside QuickJS's 0..OP_COUNT-1 range (see top of this file
        // for the rationale).
        table[OP_PROTO_ACC_LOC8_LOC8]       = FUSED_ACC_LEN;
        table[OP_PROTO_LT_LOC8_LOC8_JFALSE] = FUSED_LT_JF_LEN;
        table[OP_PROTO_LT_LOC_VAR_JFALSE]   = FUSED_LT_LOC_VAR_JF_LEN;
        return table;
    }();
    return sizes;
}

// ─────────────────────────────────────────────────────────────────
// Pattern matching helpers — walk the bytecode in opcode-size
// strides, return matched-pattern length (0 if no match).
// ─────────────────────────────────────────────────────────────────

// Helper: decode a "load local N" prefix.  QuickJS emits one of
// {get_loc, get_loc8, get_loc0..get_loc3, get_loc_check} depending on
// local index and whether the var needs TDZ tracking.  `let` /
// `const` declarations use the `_check` forms; we accept them too
// because the fused handlers perform the TDZ check inline (a single
// pointer compare per local) and fall back to the slow path on a
// sentinel hit.  Skipping these would leave EVERY `for (let i=...)`
// loop unspecialised — exactly the shape numeric_loop /
// function_calls / and most user code emits.
int decodeGetLoc(const uint8_t* buf, int pc, int len, uint8_t& outIdx) {
    if (pc >= len) return 0;
    uint8_t op = buf[pc];
    if (op == OP_get_loc0) { outIdx = 0; return 1; }
    if (op == OP_get_loc1) { outIdx = 1; return 1; }
    if (op == OP_get_loc2) { outIdx = 2; return 1; }
    if (op == OP_get_loc3) { outIdx = 3; return 1; }
    if (op == OP_get_loc8 && pc + 2 <= len) { outIdx = buf[pc + 1]; return 2; }
    if (op == OP_get_loc && pc + 3 <= len) {
        uint16_t idx = (uint16_t)buf[pc + 1] | ((uint16_t)buf[pc + 2] << 8);
        if (idx <= 0xff) { outIdx = (uint8_t)idx; return 3; }
        return 0;
    }
    // TDZ-checked form `get_loc_check N` (3 bytes).  The fused
    // handler runs the same TDZ sentinel compare so semantics stay
    // identical; for steady-state loops the check resolves to a
    // single cheap pointer compare after the first iteration.
    if (op == OP_get_loc_check && pc + 3 <= len) {
        uint16_t idx = (uint16_t)buf[pc + 1] | ((uint16_t)buf[pc + 2] << 8);
        if (idx <= 0xff) { outIdx = (uint8_t)idx; return 3; }
        return 0;
    }
    return 0;
}

int decodePutLocSame(const uint8_t* buf, int pc, int len, uint8_t expected) {
    if (pc >= len) return 0;
    uint8_t op = buf[pc];
    if (op == OP_put_loc0 && expected == 0) return 1;
    if (op == OP_put_loc1 && expected == 1) return 1;
    if (op == OP_put_loc2 && expected == 2) return 1;
    if (op == OP_put_loc3 && expected == 3) return 1;
    if (op == OP_put_loc8 && pc + 2 <= len && buf[pc + 1] == expected) return 2;
    if (op == OP_put_loc && pc + 3 <= len) {
        uint16_t idx = (uint16_t)buf[pc + 1] | ((uint16_t)buf[pc + 2] << 8);
        if (idx == expected) return 3;
    }
    // TDZ-checked form `put_loc_check N` (3 bytes); same rationale
    // as the get_loc_check entry in decodeGetLoc above.
    if (op == OP_put_loc_check && pc + 3 <= len) {
        uint16_t idx = (uint16_t)buf[pc + 1] | ((uint16_t)buf[pc + 2] << 8);
        if (idx == expected) return 3;
    }
    return 0;
}

// Pattern P1a: `s += i` (the unoptimised form QuickJS emits before its
// own peephole): `get_loc dst; get_loc src; add; put_loc dst`.  We
// accept every loc form (loc8, locN, loc).  Returns the consumed byte
// span (e.g. 7 if all four ops were loc8/loc8/add/loc8).
int matchAccPattern(const uint8_t* buf, int pc, int len,
                    uint8_t& outDst, uint8_t& outSrc) {
    uint8_t dst, src;
    int n1 = decodeGetLoc(buf, pc, len, dst);
    if (!n1) return 0;
    int n2 = decodeGetLoc(buf, pc + n1, len, src);
    if (!n2) return 0;
    if (pc + n1 + n2 + 1 > len || buf[pc + n1 + n2] != OP_add) return 0;
    int n4 = decodePutLocSame(buf, pc + n1 + n2 + 1, len, dst);
    if (!n4) return 0;
    outDst = dst;
    outSrc = src;
    return n1 + n2 + 1 + n4;
}

// Pattern P1b: QuickJS's own-peephole short form
//   `get_loc<src>; add_loc dst`
// expressing `dst += src` in 3 or 4 bytes.  Matches the form the
// `var s, i; ... s += i` loop generates after `Compress`.
int matchAccPatternAddLoc(const uint8_t* buf, int pc, int len,
                          uint8_t& outDst, uint8_t& outSrc) {
    uint8_t src;
    int n1 = decodeGetLoc(buf, pc, len, src);
    if (!n1) return 0;
    if (pc + n1 + 2 > len || buf[pc + n1] != OP_add_loc) return 0;
    outDst = buf[pc + n1 + 1];
    outSrc = src;
    return n1 + 2;
}

// Pattern P2: `i < n` followed by `if_false TARGET` (or if_false8).
// Returns the total matched byte span (≥7 — fused form is 7 bytes
// so a smaller match would grow the buffer and is rejected for the
// compact form; the NOP-pad form pads either way).
//
// Outputs: the two local indices and the **original `if_false` stored
// offset value** (raw bytes — the pass-2 walker resolves it to a real
// target via the remap table).  We always normalise to a 32-bit signed
// offset on output regardless of whether the source had if_false or
// if_false8; the if_false8 case widens the stored byte to int32_t at
// match time.
//
// The boolean `outIsShortJf` distinguishes if_false8 (1-byte offset)
// from if_false (4-byte offset) at the source — the compact rewriter
// uses it to compute the original target pc correctly.
// Pattern P3: `i < n` where `n` is closure / global (NOT a local).
//   bytes: get_loc<a>; get_var <varIdx>; lt; if_false[8] T
//   QuickJS emits OP_get_var (byte 0x38, 3-byte form: op + u16 varIdx)
//   for module-level `let` / `const` / `var` declarations captured by
//   inner functions — the `for (let i=0; i<INNER; i++)` shape with
//   INNER as a top-level const.  We accept either the regular
//   `OP_get_var` or the TDZ-tracked `OP_get_var_undef` source (the
//   handler does the same TDZ check inline either way).
//
// Returns the matched-source byte span; outputs the loc index, the
// 16-bit var-ref / closure index, the original if_false offset
// (sign-extended to int32 when the source used if_false8), and a
// flag for the short-jump form so the compact rewriter can recover
// the original target pc.
int matchLtLocVarJfPattern(const uint8_t* buf, int pc, int len,
                            uint8_t& outLoc, uint16_t& outVarIdx,
                            int32_t& outOriginalStored,
                            bool& outIsShortJf) {
    uint8_t loc;
    int n1 = decodeGetLoc(buf, pc, len, loc);
    if (!n1) return 0;
    int varPos = pc + n1;
    if (varPos + 3 > len) return 0;
    uint8_t varOp = buf[varPos];
    // Accept OP_get_var only; OP_get_var_check is rare and would need
    // the runtime to surface a ReferenceError on a missing global —
    // safer to leave it on the slow path.  OP_get_var0..3 short
    // forms don't exist in QuickJS for var_refs at runtime, only at
    // compile-time, so the 3-byte form is the only match.
    if (varOp != OP_get_var) return 0;
    uint16_t varIdx = (uint16_t)buf[varPos + 1] | ((uint16_t)buf[varPos + 2] << 8);
    int afterVar = varPos + 3;
    if (afterVar + 1 > len || buf[afterVar] != OP_lt) return 0;
    int jfPos = afterVar + 1;
    if (jfPos + 5 <= len && buf[jfPos] == OP_if_false) {
        uint32_t u = (uint32_t)buf[jfPos + 1]
                   | ((uint32_t)buf[jfPos + 2] << 8)
                   | ((uint32_t)buf[jfPos + 3] << 16)
                   | ((uint32_t)buf[jfPos + 4] << 24);
        outOriginalStored = (int32_t)u;
        outIsShortJf = false;
        outLoc = loc; outVarIdx = varIdx;
        return n1 + 3 + 1 + 5;
    }
    if (jfPos + 2 <= len && buf[jfPos] == OP_if_false8) {
        outOriginalStored = (int32_t)(int8_t)buf[jfPos + 1];
        outIsShortJf = true;
        outLoc = loc; outVarIdx = varIdx;
        return n1 + 3 + 1 + 2;
    }
    return 0;
}

int matchLtCmpJfPattern(const uint8_t* buf, int pc, int len,
                        uint8_t& outA, uint8_t& outB,
                        int32_t& outOriginalStored,
                        bool& outIsShortJf) {
    uint8_t a, b;
    int n1 = decodeGetLoc(buf, pc, len, a);
    if (!n1) return 0;
    int n2 = decodeGetLoc(buf, pc + n1, len, b);
    if (!n2) return 0;
    if (pc + n1 + n2 + 1 > len || buf[pc + n1 + n2] != OP_lt) return 0;
    int jfPos = pc + n1 + n2 + 1;
    if (jfPos + 5 <= len && buf[jfPos] == OP_if_false) {
        uint32_t u = (uint32_t)buf[jfPos + 1]
                   | ((uint32_t)buf[jfPos + 2] << 8)
                   | ((uint32_t)buf[jfPos + 3] << 16)
                   | ((uint32_t)buf[jfPos + 4] << 24);
        outOriginalStored = (int32_t)u;
        outIsShortJf = false;
        outA = a; outB = b;
        return n1 + n2 + 1 + 5;
    }
    if (jfPos + 2 <= len && buf[jfPos] == OP_if_false8) {
        outOriginalStored = (int32_t)(int8_t)buf[jfPos + 1];
        outIsShortJf = true;
        outA = a; outB = b;
        return n1 + n2 + 1 + 2;
    }
    return 0;
}

inline void writeLE32(uint8_t* p, int32_t v) {
    uint32_t u = (uint32_t)v;
    p[0] = (uint8_t)(u       & 0xff);
    p[1] = (uint8_t)((u>>8 ) & 0xff);
    p[2] = (uint8_t)((u>>16) & 0xff);
    p[3] = (uint8_t)((u>>24) & 0xff);
}

inline int32_t readLE32(const uint8_t* p) {
    uint32_t u =  (uint32_t)p[0]
               | ((uint32_t)p[1] << 8)
               | ((uint32_t)p[2] << 16)
               | ((uint32_t)p[3] << 24);
    return (int32_t)u;
}

// ─────────────────────────────────────────────────────────────────
// Form A — NOP-pad rewrite.  Bytecode length stays identical; every
// existing jump offset in the rest of the buffer remains valid.
// The candidate slot has the fused opcode first, then `OP_nop` for
// the remaining bytes.  Each NOP costs one dispatch but is otherwise
// free — and there is zero risk of breaking a jump target landing
// inside the fused region (it lands on a NOP and walks forward).
// ─────────────────────────────────────────────────────────────────
// Attempt to fuse at `pc` using both the "naive" accumulator pattern
// (P1a) and the QuickJS-peephole-optimised one (P1b).  Returns the
// matched source-byte span (0 = no match) and fills outDst / outSrc.
int tryMatchAcc(const uint8_t* buf, int pc, int len,
                uint8_t& outDst, uint8_t& outSrc) {
    int n = matchAccPattern(buf, pc, len, outDst, outSrc);
    if (n > 0) return n;
    return matchAccPatternAddLoc(buf, pc, len, outDst, outSrc);
}

std::vector<uint8_t> specialiseNopPad(const uint8_t* buf, int len) {
    std::vector<uint8_t> out(buf, buf + len);
    int hitsAcc = 0, hitsLtJf = 0;
    if (std::getenv("PROTOJS_SPECIALISER_DUMP")) {
        fprintf(stderr, "[specialiser] dump (%d bytes):", len);
        for (int i = 0; i < len && i < 80; ++i) fprintf(stderr, " %02x", buf[i]);
        fprintf(stderr, "\n");
    }

    int pc = 0;
    while (pc < len) {
        uint8_t op = out[pc];

        // ── ACC pattern ──
        uint8_t dst, src;
        int accSpan = tryMatchAcc(out.data(), pc, len, dst, src);
        // Fused form is 3 bytes; the NOP-pad scheme only works when
        // the source span is at least that.  In practice the smallest
        // QuickJS form is `get_locN + add_loc M` = 3 bytes (loc 0-3)
        // or 4 bytes (loc8) so the >= test never rejects.
        if (accSpan >= FUSED_ACC_LEN) {
            hitsAcc++;
            out[pc + 0] = OP_PROTO_ACC_LOC8_LOC8;
            out[pc + 1] = dst;
            out[pc + 2] = src;
            for (int k = FUSED_ACC_LEN; k < accSpan; ++k) out[pc + k] = OP_nop;
            pc += accSpan;
            continue;
        }

        // ── LT-JF pattern ──
        // P3: loc < global-var (closure / module-level const).
        // Matched FIRST because its source span overlaps with the
        // loc-loc form's prefix (`get_loc` is common); if the second
        // operand is a `get_var` we want this fused opcode, not a
        // mis-match against P2.
        {
            uint8_t locA; uint16_t varB;
            int32_t storedOffsetV; bool isShortJfV;
            int spanV = matchLtLocVarJfPattern(out.data(), pc, len,
                                                locA, varB,
                                                storedOffsetV, isShortJfV);
            if (spanV >= FUSED_LT_LOC_VAR_JF_LEN) {
                hitsLtJf++;
                int jfSize = isShortJfV ? 2 : 5;
                int origIfFalsePc = pc + spanV - jfSize;
                int32_t diff = (int32_t)(
                    origIfFalsePc + 1 + storedOffsetV
                    - (pc + FUSED_LT_LOC_VAR_JF_LEN));
                out[pc + 0] = OP_PROTO_LT_LOC_VAR_JFALSE;
                out[pc + 1] = locA;
                out[pc + 2] = (uint8_t)(varB & 0xFF);
                out[pc + 3] = (uint8_t)((varB >> 8) & 0xFF);
                writeLE32(&out[pc + 4], diff);
                for (int k = FUSED_LT_LOC_VAR_JF_LEN; k < spanV; ++k)
                    out[pc + k] = OP_nop;
                pc += spanV;
                continue;
            }
        }

        uint8_t a, b;
        int32_t storedOffset;
        bool isShortJf;
        int ltSpan = matchLtCmpJfPattern(out.data(), pc, len,
                                          a, b, storedOffset, isShortJf);
        if (ltSpan >= FUSED_LT_JF_LEN) {
            hitsLtJf++;
            // In the NOP-pad form the new opcode sits where the
            // original first byte was; targets in the rest of the
            // buffer continue to point at the SAME pc values.  So
            // we just need to convert the original stored offset
            // into the equivalent value our fused handler expects.
            //
            //   Original if_false at origIfFalsePc = pc + (ltSpan - jfSize)
            //   Original semantics: target = origIfFalsePc + 1 + storedOffset
            //                              (or +1 byte for if_false8)
            //   Our handler: target = (pc + FUSED_LT_JF_LEN) + diff
            //   ⇒ diff = origIfFalsePc + 1 + storedOffset - (pc + 7)
            //
            // For if_false (5B): origIfFalsePc = pc + ltSpan - 5;
            //   diff = (pc + ltSpan - 5) + 1 + storedOffset - (pc + 7)
            //        = storedOffset + ltSpan - 11
            //   (e.g. ltSpan=10 → diff = storedOffset - 1, matches the
            //    original derivation in the comment-block we replaced.)
            //
            // For if_false8 (2B): origIfFalsePc = pc + ltSpan - 2;
            //   diff = (pc + ltSpan - 2) + 1 + storedOffset - (pc + 7)
            //        = storedOffset + ltSpan - 8
            int jfSize = isShortJf ? 2 : 5;
            int origIfFalsePc = pc + ltSpan - jfSize;
            int32_t diff = (int32_t)(
                origIfFalsePc + 1 + storedOffset - (pc + FUSED_LT_JF_LEN));
            out[pc + 0] = OP_PROTO_LT_LOC8_LOC8_JFALSE;
            out[pc + 1] = a;
            out[pc + 2] = b;
            writeLE32(&out[pc + 3], diff);
            for (int k = FUSED_LT_JF_LEN; k < ltSpan; ++k) out[pc + k] = OP_nop;
            pc += ltSpan;
            continue;
        }

        // Skip past this opcode based on its declared size.
        const uint8_t* sizes = getOpcodeSizes();
        uint8_t sz = sizes[op];
        if (sz == 0) {
            // Unknown opcode — abandon further specialisation in this
            // function rather than risk misalignment.
            if (std::getenv("PROTOJS_SPECIALISER_DIAG")) {
                fprintf(stderr, "[specialiser] nop-pad aborted at pc=%d op=0x%02x len=%d hits acc=%d ltjf=%d\n",
                        pc, op, len, hitsAcc, hitsLtJf);
            }
            break;
        }
        pc += sz;
    }
    if (std::getenv("PROTOJS_SPECIALISER_DIAG") && (hitsAcc + hitsLtJf) > 0) {
        fprintf(stderr, "[specialiser] nop-pad: %d ACC + %d LT_JF rewrites in %d-byte function\n",
                hitsAcc, hitsLtJf, len);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────
// Form B — Compact rewrite + jump-target remap.  Emit a shorter
// buffer where each matched sequence collapses to its fused form;
// build a translation table `old_pc → new_pc`; then walk the new
// buffer once more rewriting every relative-jump operand
// (`if_false`/`if_true`/`goto`/`goto8`/`goto16`/`if_false8`/
// `if_true8`) so it points at the equivalent new offset.
//
// This is the form CPython 3.11+ uses for its "quickening", and
// the protoPython sprint-11 sketch the user asked us to port.
// Trade-off vs NopPad: tighter dispatch (no per-iter NOPs to retire)
// at the cost of needing every relative-jump opcode in the rewriter.
// ─────────────────────────────────────────────────────────────────
std::vector<uint8_t> specialiseCompact(const uint8_t* buf, int len) {
    const uint8_t* sizes = getOpcodeSizes();

    // Pass 1: emit fused bytecode, recording the source-pc / dest-pc
    // mapping.  remap[srcPc] = dstPc for every legal instruction
    // boundary; -1 elsewhere.
    std::vector<uint8_t> out;
    out.reserve(len);
    std::vector<int> remap(len + 1, -1);  // +1 for end-of-buffer label

    int pc = 0;
    while (pc < len) {
        remap[pc] = (int)out.size();
        uint8_t op = buf[pc];

        uint8_t dst, src;
        int accSpan = tryMatchAcc(buf, pc, len, dst, src);
        if (accSpan >= FUSED_ACC_LEN) {
            out.push_back(OP_PROTO_ACC_LOC8_LOC8);
            out.push_back(dst);
            out.push_back(src);
            // Map the interior bytes of the original span to the SAME
            // new dst pc — they were sub-instruction boundaries that
            // no legal jump targeted, but defining the mapping keeps
            // the invertibility we rely on in Pass 2.
            for (int k = 1; k < accSpan; ++k) remap[pc + k] = (int)out.size() - 3;
            pc += accSpan;
            continue;
        }

        // P3 (compact form): loc < var ; if_false[8] T
        // Matched BEFORE the loc-loc form for the same reason as in
        // the NOP-pad pass — the prefix overlap.
        {
            uint8_t locA; uint16_t varB;
            int32_t storedOffsetV; bool isShortJfV;
            int spanV = matchLtLocVarJfPattern(buf, pc, len,
                                                locA, varB,
                                                storedOffsetV, isShortJfV);
            if (spanV >= FUSED_LT_LOC_VAR_JF_LEN) {
                out.push_back(OP_PROTO_LT_LOC_VAR_JFALSE);
                out.push_back(locA);
                out.push_back((uint8_t)(varB & 0xFF));
                out.push_back((uint8_t)((varB >> 8) & 0xFF));
                const size_t off = out.size();
                out.resize(off + 4);
                // Pack storedOffset (low 24 bits, signed) + ltSpan
                // (bits 24..30, 7 bits) + isShortJf (bit 31).  Same
                // scheme as the loc-loc variant; Pass 2 dispatches on
                // the opcode to pick the right unpacking.
                uint32_t packed =
                    ((uint32_t)(storedOffsetV & 0xFFFFFF))
                    | (((uint32_t)(uint8_t)spanV & 0x7F) << 24)
                    | (isShortJfV ? 0x80000000u : 0);
                writeLE32(&out[off], (int32_t)packed);
                for (int k = 1; k < spanV; ++k)
                    remap[pc + k] = (int)out.size() - FUSED_LT_LOC_VAR_JF_LEN;
                pc += spanV;
                continue;
            }
        }

        uint8_t a, b;
        int32_t storedOffset;
        bool isShortJf;
        int ltSpan = matchLtCmpJfPattern(buf, pc, len, a, b, storedOffset, isShortJf);
        if (ltSpan >= FUSED_LT_JF_LEN) {
            // Stash a placeholder for the relative offset; Pass 2 fills
            // it in once `remap[]` is populated.  The stash carries
            // enough info to recover the original target: we keep BOTH
            // the original stored offset value AND a flag (encoded as
            // the high bit of one of the loc bytes? – no, we just keep
            // it in side-table-form via origIfFalsePc reconstruction
            // from invRemap[newPc] in Pass 2; the stored 32-bit slot
            // here holds the original `storedOffset` widened, with the
            // jfSize recoverable from `(span - 1 byte for the loc8
            // form, etc.)` — but for simplicity we encode jfSize as
            // either 0 (5-byte if_false) or 1 (2-byte if_false8)
            // packed into the MSB of the placeholder.  Pass 2 unpacks.
            out.push_back(OP_PROTO_LT_LOC8_LOC8_JFALSE);
            out.push_back(a);
            out.push_back(b);
            const size_t off = out.size();
            out.resize(off + 4);
            // Pack storedOffset (low 24 bits, signed) + jfSize marker
            // (bit 31) + ltSpan (bits 24..30) so Pass 2 can recover
            // the original target.  Signed 24-bit covers ±8 MiB —
            // way more than any if_false target in practice.
            uint32_t packed =
                ((uint32_t)(storedOffset & 0xFFFFFF))
                | (((uint32_t)(uint8_t)ltSpan & 0x7F) << 24)
                | (isShortJf ? 0x80000000u : 0);
            writeLE32(&out[off], (int32_t)packed);
            for (int k = 1; k < ltSpan; ++k) remap[pc + k] = (int)out.size() - 7;
            pc += ltSpan;
            continue;
        }

        // Copy this opcode + operands verbatim.
        uint8_t sz = sizes[op];
        if (sz == 0) {
            // Unknown opcode in the stream — bail.  Return the input
            // untouched so the caller's bytecode runs unchanged.
            return std::vector<uint8_t>(buf, buf + len);
        }
        if (pc + sz > len) {
            // Truncated stream — same bail.
            return std::vector<uint8_t>(buf, buf + len);
        }
        for (int k = 0; k < sz; ++k) out.push_back(buf[pc + k]);
        pc += sz;
    }
    remap[len] = (int)out.size();  // end label

    // Pass 2: walk the rewritten buffer and patch every jump operand.
    // For each jump opcode in `out`, we know:
    //   - the original-source-pc of THIS jump instruction (it's the
    //     unique src whose remap[src] == this dst position)
    //   - the original-source-pc of its target = origSrc + origSize
    //     + storedOffset (the if_false / if_true / goto encoding)
    //   - the new-dest-pc of its target = remap[origTargetPc]
    //   - the new operand should be `newTargetPc - (this dst + size)`
    //
    // To recover the original src pc cheaply we keep an inverse
    // mapping built during Pass 1.
    std::vector<int> invRemap(out.size() + 1, -1);
    for (int s = 0; s <= len; ++s) {
        if (remap[s] >= 0 && invRemap[remap[s]] < 0) invRemap[remap[s]] = s;
    }

    int newPc = 0;
    while (newPc < (int)out.size()) {
        uint8_t op = out[newPc];
        uint8_t sz = sizes[op];
        if (sz == 0 || newPc + sz > (int)out.size()) break;

        // For every QuickJS relative-jump opcode (goto / if_false /
        // if_true and their 8/16-bit variants), the offset is stored
        // **relative to the operand start**: `target_pc = operand_start
        // + storedOffset`.  goto reads `diff` then `pc += diff`
        // directly; if_false / if_true do `pc += 4; pc += diff - 4`,
        // which is the same net `pc <- operand_start + diff`.  Both
        // forms therefore round-trip if we resolve the original target
        // via `origSrcPc + operandOffset + storedOffset` and re-emit
        // via `newOffset = newTargetPc - (newPc + operandOffset)`.
        // (NB: this is the protoCpp-style "operand-relative" idiom.
        // Don't confuse it with the "end-of-instruction-relative" form
        // some other ISAs use.)
        auto patchRel32 = [&](int operandOffset) {
            int origSrcPc = invRemap[newPc];
            if (origSrcPc < 0) return;
            int32_t storedOffset = readLE32(buf + origSrcPc + operandOffset);
            int origTargetPc = origSrcPc + operandOffset + storedOffset;
            if (origTargetPc < 0 || origTargetPc > len) return;
            int newTargetPc = remap[origTargetPc];
            if (newTargetPc < 0) return;
            int32_t newOffset = newTargetPc - (newPc + operandOffset);
            writeLE32(&out[newPc + operandOffset], newOffset);
        };
        auto patchRel8 = [&](int operandOffset) {
            int origSrcPc = invRemap[newPc];
            if (origSrcPc < 0) return;
            int8_t storedOffset = (int8_t)buf[origSrcPc + operandOffset];
            int origTargetPc = origSrcPc + operandOffset + storedOffset;
            if (origTargetPc < 0 || origTargetPc > len) return;
            int newTargetPc = remap[origTargetPc];
            if (newTargetPc < 0) return;
            int delta = newTargetPc - (newPc + operandOffset);
            if (delta < -128 || delta > 127) {
                // Out of int8_t range — the fused buffer brought the
                // target out of reach of the original short-form
                // jump.  We could widen here to the matching 32-bit
                // variant, but that grows the buffer and breaks our
                // monotone remap invariant; punt cleanly back to the
                // NopPad form (no width change, no remap, no risk).
                std::vector<uint8_t> fallback = specialiseNopPad(buf, len);
                out = std::move(fallback);
                newPc = (int)out.size();
                return;
            }
            out[newPc + operandOffset] = (uint8_t)delta;
        };
        auto patchRel16 = [&](int operandOffset) {
            int origSrcPc = invRemap[newPc];
            if (origSrcPc < 0) return;
            int16_t storedOffset = (int16_t)(uint16_t)(
                  (uint16_t)buf[origSrcPc + operandOffset]
                | ((uint16_t)buf[origSrcPc + operandOffset + 1] << 8));
            int origTargetPc = origSrcPc + operandOffset + storedOffset;
            if (origTargetPc < 0 || origTargetPc > len) return;
            int newTargetPc = remap[origTargetPc];
            if (newTargetPc < 0) return;
            int delta = newTargetPc - (newPc + operandOffset);
            if (delta < -32768 || delta > 32767) {
                std::vector<uint8_t> fallback = specialiseNopPad(buf, len);
                out = std::move(fallback);
                newPc = (int)out.size();
                return;
            }
            uint16_t u = (uint16_t)(int16_t)delta;
            out[newPc + operandOffset]     = (uint8_t)(u & 0xff);
            out[newPc + operandOffset + 1] = (uint8_t)((u >> 8) & 0xff);
        };

        switch (op) {
            case OP_if_false:
            case OP_if_true:
            case OP_goto:
                patchRel32(1);
                break;
            case OP_if_false8:
            case OP_if_true8:
            case OP_goto8:
                patchRel8(1);
                break;
            case OP_goto16:
                patchRel16(1);
                break;
            case OP_PROTO_LT_LOC8_LOC8_JFALSE: {
                // Our fused jump uses a signed 32-bit offset interpreted
                // as `pc += diff` from the END of our 7-byte instruction
                // (see L_OP_proto_lt_loc8_loc8_jfalse for the dispatch).
                //
                // Pass 1 stashed a PACKED 32-bit blob here, not the
                // original stored offset directly:
                //   bits 0..23  : sign-extended 24-bit storedOffset
                //   bits 24..30 : original ltSpan (so we can locate
                //                 the if_false byte within the matched
                //                 source span)
                //   bit  31     : 1 ⇒ if_false8 (1-byte offset), else
                //                 5-byte if_false
                // Unpack, recover the original target, look it up in
                // `remap`, and write the final relative offset.
                int origSrcPc = invRemap[newPc];
                if (origSrcPc < 0) break;
                uint32_t packed = (uint32_t)readLE32(&out[newPc + 3]);
                int32_t storedOffset = (int32_t)(packed & 0xFFFFFF);
                if (storedOffset & 0x800000) storedOffset |= ~0xFFFFFF;  // sign-extend
                int ltSpan = (int)((packed >> 24) & 0x7F);
                bool isShortJf = (packed & 0x80000000u) != 0;
                int jfSize = isShortJf ? 2 : 5;
                int origIfFalsePc = origSrcPc + ltSpan - jfSize;
                int origTargetPc = origIfFalsePc + 1 + storedOffset;
                if (origTargetPc < 0 || origTargetPc > len) break;
                int newTargetPc = remap[origTargetPc];
                if (newTargetPc < 0) break;
                int32_t newOffset = newTargetPc - (newPc + FUSED_LT_JF_LEN);
                writeLE32(&out[newPc + 3], newOffset);
                break;
            }
            case OP_PROTO_LT_LOC_VAR_JFALSE: {
                // Same packed-blob unpack as the loc-loc variant
                // above; only the fused-opcode length differs.  Offset
                // bytes live at newPc + 4 (after op + loc + u16 var).
                int origSrcPc = invRemap[newPc];
                if (origSrcPc < 0) break;
                uint32_t packed = (uint32_t)readLE32(&out[newPc + 4]);
                int32_t storedOffset = (int32_t)(packed & 0xFFFFFF);
                if (storedOffset & 0x800000) storedOffset |= ~0xFFFFFF;
                int ltSpan = (int)((packed >> 24) & 0x7F);
                bool isShortJf = (packed & 0x80000000u) != 0;
                int jfSize = isShortJf ? 2 : 5;
                int origIfFalsePc = origSrcPc + ltSpan - jfSize;
                int origTargetPc = origIfFalsePc + 1 + storedOffset;
                if (origTargetPc < 0 || origTargetPc > len) break;
                int newTargetPc = remap[origTargetPc];
                if (newTargetPc < 0) break;
                int32_t newOffset = newTargetPc - (newPc + FUSED_LT_LOC_VAR_JF_LEN);
                writeLE32(&out[newPc + 4], newOffset);
                break;
            }
            default:
                break;
        }

        newPc += sz;
    }

    return out;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────
// Public entry points
// ─────────────────────────────────────────────────────────────────

SpecialiseMode getSpecialiseMode() {
    static SpecialiseMode cached = []() {
        const char* v = std::getenv("PROTOJS_SPECIALISER");
        // Default is `compact` — the form that produces the biggest
        // wins on TDZ-checked accumulator loops once `decodeGetLoc`
        // accepts the `get_loc_check` / `put_loc_check` variants
        // (commit landing this change).  Set PROTOJS_SPECIALISER=off
        // to disable; `nop` for the alternate NOP-pad form.
        if (!v || !v[0]) return SpecialiseMode::Compact;
        if (!std::strcmp(v, "off"))      return SpecialiseMode::Off;
        if (!std::strcmp(v, "nop"))      return SpecialiseMode::NopPad;
        if (!std::strcmp(v, "compact"))  return SpecialiseMode::Compact;
        fprintf(stderr,
                "protojs: unknown PROTOJS_SPECIALISER value '%s' "
                "(want off|nop|compact) — defaulting to off\n",
                v);
        return SpecialiseMode::Off;
    }();
    return cached;
}

std::vector<uint8_t> specialise(const uint8_t* buf, int len,
                                SpecialiseMode mode) {
    if (!buf || len <= 0) return {};
    switch (mode) {
        case SpecialiseMode::Off:
            return std::vector<uint8_t>(buf, buf + len);
        case SpecialiseMode::NopPad:
            return specialiseNopPad(buf, len);
        case SpecialiseMode::Compact:
            return specialiseCompact(buf, len);
    }
    return std::vector<uint8_t>(buf, buf + len);
}

}  // namespace protojs
