#include "BytecodeSpecialiser.h"
#include "QuickJSOpcodeEnum.h"
#include "../debugger/DebugHooks.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <utility>

namespace protojs {

// ─────────────────────────────────────────────────────────────────
// New fused opcodes.  Allocated from the byte range 244..255 — the
// space above QuickJS's `OP_COUNT` (244 DEF()s) and below the 256-
// entry dispatch table.  Keep the assignments in sync with
// ProtoInterpreter.cpp's dispatch_table[] population.
// ─────────────────────────────────────────────────────────────────

/// The three fused opcodes are guarded headers placed in front of the
/// instructions they stand for, which stay in the bytecode (see
/// specialiseGuarded).  `skip` is the length of those instructions; the fast
/// path jumps over them, every other case falls through into them.
///
/// `OP_proto_acc_loc8_loc8 dst src skip`  (4 bytes) in front of
///   get_loc dst; get_loc src; add; put_loc dst   or   get_loc src; add_loc dst
/// Fast path: both locals Numbers: `local[dst] = local[dst] + local[src]`.
constexpr uint8_t OP_PROTO_ACC_LOC8_LOC8 = 244;

/// `OP_proto_lt_loc8_loc8_jfalse a b skip offset32` (8 bytes) in front of
///   get_loc a; get_loc b; lt; if_false[8] target
/// Fast path: both locals Numbers; jumps to `end + offset` when
/// `local[a] < local[b]` is false (`end` is the address after the header),
/// otherwise skips the original instructions.
constexpr uint8_t OP_PROTO_LT_LOC8_LOC8_JFALSE = 245;

/// `OP_proto_lt_loc_var_jfalse loc var_u16 skip offset32` (9 bytes) in front of
///   get_loc loc; get_var var; lt; if_false[8] target
/// `var` is the closure-variable index of OP_get_var.  Fast path: the local
/// is a SmallInteger and the global's property (the value OP_get_var reads
/// first) a Number.
constexpr uint8_t OP_PROTO_LT_LOC_VAR_JFALSE = 246;

/// Object literals built in one immutable step (markObjectLiterals):
/// `OP_proto_object_imm` (1 byte) pushes an IMMUTABLE child of
/// Object.prototype in place of OP_object; the literal's define_field
/// opcodes extend it by structural sharing; the last of them is rewritten to
/// `OP_proto_define_field_last atom` (5 bytes), which defines the field and
/// then makes the object mutable with one clone.
constexpr uint8_t OP_PROTO_OBJECT_IMM = 247;
constexpr uint8_t OP_PROTO_DEFINE_FIELD_LAST = 248;

constexpr int FUSED_ACC_LEN = kFusedAccLength;
constexpr int FUSED_LT_JF_LEN = kFusedLtLength;
constexpr int FUSED_LT_LOC_VAR_JF_LEN = kFusedLtVarLength;

// ─────────────────────────────────────────────────────────────────
// Instruction sizes: QuickJS's final opcodes from quickjs-opcode.h and
// protoJS's opcodes 244..250.  The loader and the disassembler use this
// table too (bytecodeOpcodeSizes), so there is one table to keep current.
// A size of 0 marks a byte that is not a final opcode; a walker that meets
// one stops.
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
        table[OP_PROTO_OBJECT_IMM]          = 1;
        table[OP_PROTO_DEFINE_FIELD_LAST]   = 5;
        table[OP_PROTO_PUT_FIELD_GROUP]     = 5;
        table[OP_PROTO_PUT_FIELD_GROUP_END] = 5;
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

// Attempt to fuse at `pc` using both the "naive" accumulator pattern
// (P1a) and the QuickJS-peephole-optimised one (P1b).  Returns the
// matched source-byte span (0 = no match) and fills outDst / outSrc.
int tryMatchAcc(const uint8_t* buf, int pc, int len,
                uint8_t& outDst, uint8_t& outSrc) {
    int n = matchAccPattern(buf, pc, len, outDst, outSrc);
    if (n > 0) return n;
    return matchAccPatternAddLoc(buf, pc, len, outDst, outSrc);
}

// Absolute target of the jump instruction at `pc`, or -1 if it is not a
// jump.  Covers every QuickJS opcode that carries a code offset: the
// conditional and unconditional jumps, `catch` (the handler of a try
// range), `gosub` (a finally block) and the five `with_*` opcodes, whose
// label follows a 4-byte atom.
int jumpTarget(const uint8_t* buf, int pc) {
    auto rel32 = [&](int off) {
        int32_t d = (int32_t)((uint32_t)buf[pc + off] | ((uint32_t)buf[pc + off + 1] << 8)
                    | ((uint32_t)buf[pc + off + 2] << 16) | ((uint32_t)buf[pc + off + 3] << 24));
        return pc + off + d;
    };
    switch (buf[pc]) {
        case OP_if_false: case OP_if_true: case OP_goto: case OP_catch: case OP_gosub:
            return rel32(1);
        case OP_if_false8: case OP_if_true8: case OP_goto8:
            return pc + 1 + (int8_t)buf[pc + 1];
        case OP_goto16:
            return pc + 1 + (int16_t)(uint16_t)(buf[pc + 1] | (buf[pc + 2] << 8));
        case OP_with_get_var: case OP_with_put_var: case OP_with_delete_var:
        case OP_with_make_ref: case OP_with_get_ref:
            return rel32(5);
        default:
            return -1;
    }
}

// Writes `target` into the offset operand of the jump instruction at
// `newPc` of `out` (the same encoding jumpTarget reads).  Returns false
// when the new offset does not fit the instruction's operand width.
bool writeJumpTarget(std::vector<uint8_t>& out, int newPc, int target) {
    auto put32 = [&](int off) {
        writeLE32(&out[newPc + off], (int32_t)(target - (newPc + off)));
        return true;
    };
    switch (out[newPc]) {
        case OP_if_false: case OP_if_true: case OP_goto: case OP_catch: case OP_gosub:
            return put32(1);
        case OP_if_false8: case OP_if_true8: case OP_goto8: {
            const int d = target - (newPc + 1);
            if (d < -128 || d > 127) return false;
            out[newPc + 1] = (uint8_t)(int8_t)d;
            return true;
        }
        case OP_goto16: {
            const int d = target - (newPc + 1);
            if (d < -32768 || d > 32767) return false;
            const uint16_t u = (uint16_t)(int16_t)d;
            out[newPc + 1] = (uint8_t)(u & 0xff);
            out[newPc + 2] = (uint8_t)(u >> 8);
            return true;
        }
        case OP_with_get_var: case OP_with_put_var: case OP_with_delete_var:
        case OP_with_make_ref: case OP_with_get_ref:
            return put32(5);
        default:
            return true;
    }
}

// ─────────────────────────────────────────────────────────────────
// The rewrite: guarded super-instructions in front of the code they
// replace.
//
// Each matched sequence keeps its original instructions; a fused header
// is inserted in front of them:
//
//     <header: fused opcode, operands, skip, [jump offset]>
//     <the original instructions, unchanged>
//
// The header's handler runs the fast path only when it can prove the
// result equals what the original instructions would compute: both
// operands Numbers (read through closure cells), and for P3 the
// global read the same `get_var` performs.  It then skips the original
// instructions (`skip` bytes) or takes the jump.  In every other case
// (BigInt, objects with valueOf / Symbol.toPrimitive, strings,
// the TDZ sentinel, an undeclared global) it does
// nothing and execution falls through into the original instructions,
// which give the exact semantics by construction.  The fused handlers
// therefore have no slow path of their own to keep in sync with `add`,
// `lt`, `get_var` and the TDZ checks.
//
// Jumps: the buffer grows, so every code offset is re-encoded through an
// old-to-new offset table: the jumps, `catch` handlers, `gosub` targets
// and the `with_*` labels (jumpTarget).  A jump to the first instruction
// of a sequence lands on its header.  A sequence with a jump target inside
// it is not fused (the code is still correct if it were, since the
// original instructions are kept, but the header would then be entered
// only part of the times the sequence runs).  If a short jump can no
// longer reach its target, the function is left unspecialised.
// ─────────────────────────────────────────────────────────────────
std::vector<uint8_t> specialiseGuarded(const uint8_t* buf, int len) {
    const uint8_t* sizes = getOpcodeSizes();
    const std::vector<uint8_t> unchanged(buf, buf + len);

    // Pass 0: validate the stream and mark every jump target.
    std::vector<uint8_t> isTarget(static_cast<size_t>(len) + 1, 0);
    for (int pc = 0; pc < len; ) {
        const uint8_t sz = sizes[buf[pc]];
        if (sz == 0 || pc + sz > len) return unchanged;
        const int t = jumpTarget(buf, pc);
        if (t != -1) {
            if (t < 0 || t > len) return unchanged;
            isTarget[t] = 1;
        }
        pc += sz;
    }
    auto interiorIsTarget = [&](int pc, int span) {
        for (int k = pc + 1; k < pc + span; ++k)
            if (isTarget[k]) return true;
        return false;
    };

    // Pass 1: emit headers and copy every instruction, recording the new
    // offset of each original instruction boundary.
    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(len) + len / 4 + 16);
    std::vector<int> remap(static_cast<size_t>(len) + 1, -1);
    struct Copied { int newPc; int origPc; };
    std::vector<Copied> copied;
    // A header whose jump offset Pass 2 fills: where it is, where its
    // offset operand is, and the original target.
    struct Header { int newPc; int offsetAt; int length; int origTarget; };
    std::vector<Header> headers;
    int fused = 0;

    auto copySpan = [&](int from, int span) {
        for (int pc = from; pc < from + span; ) {
            const uint8_t sz = sizes[buf[pc]];
            if (pc != from) remap[pc] = (int)out.size();
            copied.push_back({(int)out.size(), pc});
            out.insert(out.end(), buf + pc, buf + pc + sz);
            pc += sz;
        }
    };
    // The original target of the if_false / if_false8 ending a matched
    // comparison span.
    auto ifFalseTarget = [&](int pc, int span, bool isShort) {
        return jumpTarget(buf, pc + span - (isShort ? 2 : 5));
    };

    int pc = 0;
    while (pc < len) {
        remap[pc] = (int)out.size();

        uint8_t dst, src;
        const int accSpan = tryMatchAcc(buf, pc, len, dst, src);
        if (accSpan > 0 && accSpan <= 0xff && !interiorIsTarget(pc, accSpan)) {
            out.push_back(OP_PROTO_ACC_LOC8_LOC8);
            out.push_back(dst);
            out.push_back(src);
            out.push_back((uint8_t)accSpan);
            copySpan(pc, accSpan);
            ++fused;
            pc += accSpan;
            continue;
        }

        // P3 is tried before P2: both start with a local read.
        {
            uint8_t locA; uint16_t varB; int32_t stored; bool isShort;
            const int span = matchLtLocVarJfPattern(buf, pc, len, locA, varB, stored, isShort);
            if (span > 0 && span <= 0xff && !interiorIsTarget(pc, span)) {
                const int hdr = (int)out.size();
                out.push_back(OP_PROTO_LT_LOC_VAR_JFALSE);
                out.push_back(locA);
                out.push_back((uint8_t)(varB & 0xff));
                out.push_back((uint8_t)(varB >> 8));
                out.push_back((uint8_t)span);
                out.resize(out.size() + 4);
                headers.push_back({hdr, hdr + 5, FUSED_LT_LOC_VAR_JF_LEN,
                                   ifFalseTarget(pc, span, isShort)});
                copySpan(pc, span);
                ++fused;
                pc += span;
                continue;
            }
        }
        {
            uint8_t a, b; int32_t stored; bool isShort;
            const int span = matchLtCmpJfPattern(buf, pc, len, a, b, stored, isShort);
            if (span > 0 && span <= 0xff && !interiorIsTarget(pc, span)) {
                const int hdr = (int)out.size();
                out.push_back(OP_PROTO_LT_LOC8_LOC8_JFALSE);
                out.push_back(a);
                out.push_back(b);
                out.push_back((uint8_t)span);
                out.resize(out.size() + 4);
                headers.push_back({hdr, hdr + 4, FUSED_LT_JF_LEN,
                                   ifFalseTarget(pc, span, isShort)});
                copySpan(pc, span);
                ++fused;
                pc += span;
                continue;
            }
        }

        copySpan(pc, sizes[buf[pc]]);
        pc += sizes[buf[pc]];
    }
    remap[len] = (int)out.size();
    if (fused == 0) return unchanged;

    // Pass 2: re-encode every code offset.
    for (const Copied& c : copied) {
        const int t = jumpTarget(buf, c.origPc);
        if (t == -1) continue;
        if (t < 0 || t > len || remap[t] < 0) return unchanged;
        if (!writeJumpTarget(out, c.newPc, remap[t])) return unchanged;
    }
    for (const Header& h : headers) {
        if (h.origTarget < 0 || h.origTarget > len || remap[h.origTarget] < 0) return unchanged;
        writeLE32(&out[h.offsetAt], (int32_t)(remap[h.origTarget] - (h.newPc + h.length)));
    }

    if (std::getenv("PROTOJS_SPECIALISER_DIAG"))
        fprintf(stderr, "[specialiser] %d fused sequence(s) in a %d-byte function (now %zu bytes)\n",
                fused, len, out.size());
    return out;
}

}  // namespace


// ─────────────────────────────────────────────────────────────────
// Object literals in one immutable step
// ─────────────────────────────────────────────────────────────────
//
// QuickJS compiles `{a: x, b: y}` to
//     object; <x>; define_field a; <y>; define_field b; ...
// OP_object creates a MUTABLE object, so each define_field publishes a new
// snapshot of it into protoCore's mutable table: about 12 cells per field
// that become garbage at once.  No code can observe the object before the
// literal is complete (a value expression only works above it on the
// operand stack), so the fields can instead be added to an IMMUTABLE object,
// which costs only the attribute tree's path copy, and the result made
// mutable once, after the last field.
//
// The pass finds, for each OP_object, the define_field opcodes whose target
// is that object, using the stack level before each instruction (computed
// by QuickJS over the function's control-flow graph):
//   - the object occupies slot d, where d is the level before OP_object;
//   - an instruction at level L that pops p values reads slots L-p .. L-1,
//     so it touches the object when L - p <= d;
//   - a define_field at level d + 2 is a field of this literal;
//   - the first other instruction that touches the object ends the literal
//     part (it is the consumer, or set_proto / copy_data_properties /
//     define_method / ..., which then run on the already-mutable object).
// OP_object becomes OP_proto_object_imm and the last define_field before
// the end becomes OP_proto_define_field_last.  The rewrite keeps every
// instruction's size, so no jump needs remapping.  A literal is left alone
// when the region contains an unreachable byte, a catch, or a jump into or
// out of it, or when it has no define_field.
namespace {

enum QjsFmt {
#define FMT(f) QF_##f,
#define DEF(id, size, n_pop, n_push, f)
#define def(id, size, n_pop, n_push, f)
#include "quickjs-opcode.h"
#undef def
#undef DEF
    QF_count
};

struct QjsOpInfo { uint8_t nPop; uint8_t fmt; };

const QjsOpInfo* qjsOpInfo() {
    static const QjsOpInfo table[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) { (uint8_t)(n_pop), (uint8_t)QF_##f },
#define def(id, size, n_pop, n_push, f)
#include "quickjs-opcode.h"
#undef def
#undef DEF
        { 0, 0 }
    };
    return table;
}

// Values the instruction at `pc` pops (QuickJS's rule in compute_stack_size).
int qjsPops(const uint8_t* buf, int pc) {
    const uint8_t op = buf[pc];
    if (op >= OP_COUNT) return -1;
    const QjsOpInfo& oi = qjsOpInfo()[op];
    int n = oi.nPop;
    if (oi.fmt == QF_npop || oi.fmt == QF_npop_u16)
        n += buf[pc + 1] | (buf[pc + 2] << 8);
    else if (oi.fmt == QF_npopx)
        n += op - OP_call0;
    return n;
}

bool literalBuildEnabled() {
    static const bool enabled = []() {
        const char* v = std::getenv("PROTOJS_LITERAL_BUILD");
        return !(v && !std::strcmp(v, "off"));
    }();
    return enabled;
}

}  // namespace

int markObjectLiterals(std::vector<uint8_t>& code, const uint16_t* levels) {
    if (!levels || code.empty() || !literalBuildEnabled()) return 0;
    const uint8_t* sizes = getOpcodeSizes();
    const uint8_t* buf = code.data();
    const int len = (int)code.size();

    // Instruction starts and jump edges, for the region checks.
    std::vector<int> starts;
    std::vector<std::pair<int, int>> jumps;  // (source pc, target pc)
    for (int pc = 0; pc < len; ) {
        const uint8_t op = buf[pc];
        const int sz = sizes[op];
        if (sz <= 0 || op >= OP_COUNT) return 0;  // unknown byte: leave the function alone
        starts.push_back(pc);
        const int t = jumpTarget(buf, pc);
        if (t >= 0) jumps.emplace_back(pc, t);
        pc += sz;
    }

    int rewritten = 0;
    for (size_t si = 0; si < starts.size(); ++si) {
        const int objPc = starts[si];
        if (buf[objPc] != OP_object || levels[objPc] == 0xffff) continue;
        const int d = levels[objPc];
        int lastDefine = -1;
        bool ok = true;
        for (size_t sj = si + 1; sj < starts.size(); ++sj) {
            const int pc = starts[sj];
            const int L = levels[pc];
            if (L == 0xffff) { ok = false; break; }
            const int p = qjsPops(buf, pc);
            if (p < 0) { ok = false; break; }
            if (buf[pc] == OP_catch) { ok = false; break; }
            if (L - p > d) continue;               // works above the object
            if (buf[pc] == OP_define_field && L == d + 2) {
                lastDefine = pc;                   // a field of this literal
                continue;
            }
            break;                                 // first other use: end
        }
        if (!ok || lastDefine < 0) continue;
        for (const auto& j : jumps) {
            const bool srcIn = j.first > objPc && j.first < lastDefine;
            const bool dstIn = j.second > objPc && j.second <= lastDefine;
            if (srcIn != dstIn) { ok = false; break; }
        }
        if (!ok) continue;
        code[objPc] = OP_PROTO_OBJECT_IMM;
        code[lastDefine] = OP_PROTO_DEFINE_FIELD_LAST;
        ++rewritten;
    }
    return rewritten;
}

// ─────────────────────────────────────────────────────────────────
// Write groups (markPutFieldGroups)
//
// `o.a = x; o.b = y + 1; o.c = 0` is three OP_put_field, three publications
// of a new version of `o` into protoCore's mutable table.  When nothing
// between the writes can run code or throw, no one can observe the object
// between them, so the run can be published as ONE version
// (ProtoObject::setAttributes): the values are computed in order as before,
// each write but the last leaves its value on the operand stack instead of
// writing it, and the last write publishes them all.  The runtime keeps the
// exact per-write path whenever the receiver or the values could make a
// difference (a setter, a frozen object, a Proxy, an operand that is an
// object...): see "Write groups" in ProtoInterpreter.cpp.
//
// A statement is
//     <receiver> <value> put_field name
// where <receiver> loads `this`, an argument or a local, and <value> uses
// only: constants; argument, local and closure-variable reads; the
// receiver again, alone or followed by get_field / get_field2 (a read of
// one of its fields: `o.x += 1`); unary and binary arithmetic, comparisons,
// `!` and typeof.  Calls, `new`, property reads on any other object, TDZ
// checks on other bindings, `await`, `yield`, `with` and every other opcode
// end the run.  The first statement of a run is executed before the run is
// checked, so its value may use anything that parses; the checks cover the
// statements after it.  No instruction inside a run may be a jump target.
// ─────────────────────────────────────────────────────────────────
namespace {

bool putFieldGroupsEnabled() {
    static const bool enabled = []() {
        const char* v = std::getenv("PROTOJS_PUTFIELD_GROUPS");
        return !(v && !std::strcmp(v, "off"));
    }();
    return enabled;
}

uint16_t rdU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rdU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// A slot load: `kind` 0 = none, 1 = local, 2 = argument, 3 = this (a local
// in QuickJS, but kept apart for clarity), 4 = closure variable.
struct SlotLoad { int kind; uint16_t idx; bool checked; };

SlotLoad decodeSlotLoad(const uint8_t* b, int pc) {
    const uint8_t op = b[pc];
    if (op >= OP_get_loc0 && op <= OP_get_loc3) return {1, (uint16_t)(op - OP_get_loc0), false};
    if (op == OP_get_loc8) return {1, b[pc + 1], false};
    if (op == OP_get_loc) return {1, rdU16(b + pc + 1), false};
    if (op == OP_get_loc_check) return {1, rdU16(b + pc + 1), true};
    if (op >= OP_get_arg0 && op <= OP_get_arg3) return {2, (uint16_t)(op - OP_get_arg0), false};
    if (op == OP_get_arg) return {2, rdU16(b + pc + 1), false};
    if (op == OP_push_this) return {3, 0, false};
    if (op >= OP_get_var_ref0 && op <= OP_get_var_ref3) return {4, (uint16_t)(op - OP_get_var_ref0), false};
    if (op == OP_get_var_ref) return {4, rdU16(b + pc + 1), false};
    return {0, 0, false};
}

bool sameReceiver(const SlotLoad& a, const SlotLoad& b) {
    return a.kind == b.kind && a.idx == b.idx;
}

bool isConstantPush(uint8_t op) {
    switch (op) {
        case OP_push_i32: case OP_undefined: case OP_null: case OP_push_false:
        case OP_push_true: case OP_push_minus1: case OP_push_0: case OP_push_1:
        case OP_push_2: case OP_push_3: case OP_push_4: case OP_push_5:
        case OP_push_6: case OP_push_7: case OP_push_i8: case OP_push_i16:
        case OP_push_empty_string: case OP_push_atom_value:
            return true;
        default:
            return false;
    }
}

// Operators whose operands must be inert values (they may run valueOf /
// toString / @@toPrimitive, or throw, on anything else).
bool isCoercingUnary(uint8_t op) {
    return op == OP_neg || op == OP_plus || op == OP_inc || op == OP_dec || op == OP_not;
}
bool isCoercingBinary(uint8_t op) {
    switch (op) {
        case OP_add: case OP_sub: case OP_mul: case OP_div: case OP_mod: case OP_pow:
        case OP_shl: case OP_sar: case OP_shr: case OP_and: case OP_or: case OP_xor:
        case OP_lt: case OP_lte: case OP_gt: case OP_gte: case OP_eq: case OP_neq:
            return true;
        default:
            return false;
    }
}

// One parsed statement.
struct GroupStatement {
    int start = 0;            // pc of the receiver load
    int putPc = 0;            // pc of its OP_put_field
    int end = 0;              // pc after it
    SlotLoad receiver{};
    uint32_t atom = 0;
    std::vector<uint16_t> operandArgs, operandLocals;
    std::vector<PutFieldGroup::Read> reads;
};

// Parses the statement whose receiver load is at `pc`.  `strict` applies the
// operand rules (the first statement of a run runs before the check, so it
// does not need them).  Returns false when the code at `pc` is not one.
bool parseGroupStatement(const uint8_t* b, int len, int pc, const uint8_t* sizes,
                         bool strict, GroupStatement& st) {
    const SlotLoad recv = decodeSlotLoad(b, pc);
    if (recv.kind == 0 || recv.kind == 4) return false;
    st = GroupStatement{};
    st.start = pc;
    st.receiver = recv;

    // The abstract operand stack above the statement's base.
    enum Tag : uint8_t { R, CONST, VALUE, SLOT_ARG, SLOT_LOC, CLOSURE, FIELD };
    struct Entry { Tag tag; uint16_t idx; int read; };
    Entry stack[16];
    int sp = 0;
    stack[sp++] = {R, 0, -1};

    auto operand = [&](const Entry& e) -> bool {
        switch (e.tag) {
            case CONST: case VALUE: return true;
            case SLOT_ARG:
                if (strict) st.operandArgs.push_back(e.idx);
                return true;
            case SLOT_LOC:
                if (strict) st.operandLocals.push_back(e.idx);
                return true;
            case FIELD:
                if (strict && e.read >= 0) st.reads[(size_t)e.read].operand = true;
                return true;
            default:  // the receiver itself, a closure variable: unknown objects
                return !strict;
        }
    };

    pc += sizes[b[pc]];
    for (int steps = 0; steps < 64 && pc < len; ++steps) {
        const uint8_t op = b[pc];
        const int sz = sizes[op];
        if (sz <= 0 || op >= OP_COUNT) return false;
        if (op == OP_put_field) {
            if (sp != 2 || stack[0].tag != R) return false;
            st.putPc = pc;
            st.atom = rdU32(b + pc + 1);
            st.end = pc + sz;
            return true;
        }
        if (sp >= 15) return false;
        const SlotLoad ld = decodeSlotLoad(b, pc);
        if (ld.kind != 0) {
            if (sameReceiver(ld, recv)) {
                stack[sp++] = {R, 0, -1};
            } else if (ld.checked || ld.kind == 3) {
                return false;  // a TDZ check on another binding can throw
            } else if (ld.kind == 4) {
                stack[sp++] = {CLOSURE, ld.idx, -1};
            } else {
                stack[sp++] = {ld.kind == 2 ? SLOT_ARG : SLOT_LOC, ld.idx, -1};
            }
        } else if (isConstantPush(op)) {
            stack[sp++] = {CONST, 0, -1};
        } else if (op == OP_get_field || op == OP_get_field2) {
            if (stack[sp - 1].tag != R) return false;  // a read of another object
            int readIdx = -1;
            if (strict) {
                readIdx = (int)st.reads.size();
                st.reads.push_back({rdU32(b + pc + 1), 0, false});
            }
            if (op == OP_get_field) --sp;
            stack[sp++] = {FIELD, 0, readIdx};
        } else if (isCoercingUnary(op) || op == OP_lnot || op == OP_typeof) {
            if (sp < 2) return false;
            if (isCoercingUnary(op) && !operand(stack[sp - 1])) return false;
            stack[sp - 1] = {VALUE, 0, -1};
        } else if (isCoercingBinary(op) || op == OP_strict_eq || op == OP_strict_neq) {
            if (sp < 3) return false;
            if (isCoercingBinary(op) && (!operand(stack[sp - 2]) || !operand(stack[sp - 1])))
                return false;
            --sp;
            stack[sp - 1] = {VALUE, 0, -1};
        } else {
            return false;
        }
        pc += sz;
    }
    return false;
}

void dumpGroup(const PutFieldGroup& g, int start) {
    std::fprintf(stderr, "[putfield-groups] run at pc %d: %zu writes, %zu operand args, "
                 "%zu operand locals, %zu reads\n", start, g.atoms.size(),
                 g.operandArgs.size(), g.operandLocals.size(), g.reads.size());
}

}  // namespace

int markPutFieldGroups(std::vector<uint8_t>& code,
                       bool (*nameEligible)(void* user, uint32_t atom), void* user,
                       std::vector<PutFieldGroup>& groups, int& extraStack) {
    extraStack = 0;
    if (code.empty() || !putFieldGroupsEnabled()) return 0;
    const uint8_t* sizes = getOpcodeSizes();
    const uint8_t* b = code.data();
    const int len = (int)code.size();
    const bool dump = std::getenv("PROTOJS_PUTFIELD_GROUPS_DUMP") != nullptr;

    // Instruction starts and jump targets.  An unknown byte leaves the
    // function alone, as markObjectLiterals does.
    std::vector<int> starts;
    std::vector<uint8_t> isTarget((size_t)len + 1, 0);
    for (int pc = 0; pc < len; ) {
        const uint8_t op = b[pc];
        const int sz = sizes[op];
        if (sz <= 0) return 0;
        starts.push_back(pc);
        const int t = jumpTarget(b, pc);
        if (t >= 0 && t <= len) isTarget[(size_t)t] = 1;
        pc += sz;
    }
    // Every instruction start strictly inside [from, to) must not be a target.
    auto noTargetInside = [&](int from, int to) {
        for (int pc = from + 1; pc < to; ++pc)
            if (isTarget[(size_t)pc]) return false;
        return true;
    };

    int rewritten = 0;
    size_t si = 0;
    while (si < starts.size()) {
        GroupStatement first;
        const int pc0 = starts[si];
        if (!parseGroupStatement(b, len, pc0, sizes, /*strict=*/false, first)
            || !nameEligible(user, first.atom)
            || !noTargetInside(first.start, first.end)) {
            ++si;
            continue;
        }
        PutFieldGroup g;
        std::vector<int> putPcs{first.putPc};
        g.atoms.push_back(first.atom);
        int end = first.end;
        while (g.atoms.size() < 64 && end < len) {
            GroupStatement st;
            if (!parseGroupStatement(b, len, end, sizes, /*strict=*/true, st)) break;
            if (!sameReceiver(st.receiver, first.receiver) || !nameEligible(user, st.atom)) break;
            // The statement's start is inside the run, so it must not be a
            // jump target either.
            if (isTarget[(size_t)end] || !noTargetInside(st.start, st.end)) break;
            // A read of a name the run already wrote would see the old
            // value: end the run before it.
            bool readsWritten = false;
            for (const auto& r : st.reads)
                for (uint32_t a : g.atoms)
                    if (r.atom == a) readsWritten = true;
            if (readsWritten) break;
            const uint16_t position = (uint16_t)g.atoms.size();
            for (auto r : st.reads) { r.statement = position; g.reads.push_back(r); }
            for (uint16_t a : st.operandArgs) g.operandArgs.push_back(a);
            for (uint16_t l : st.operandLocals) g.operandLocals.push_back(l);
            g.atoms.push_back(st.atom);
            putPcs.push_back(st.putPc);
            end = st.end;
        }
        if (g.atoms.size() < 2 || groups.size() >= 0xffff) {
            ++si;
            continue;
        }
        const uint16_t gid = (uint16_t)groups.size();
        for (size_t k = 0; k < putPcs.size(); ++k) {
            uint8_t* ins = code.data() + putPcs[k];
            ins[0] = (k + 1 == putPcs.size()) ? OP_PROTO_PUT_FIELD_GROUP_END : OP_PROTO_PUT_FIELD_GROUP;
            ins[1] = (uint8_t)(gid & 0xff);
            ins[2] = (uint8_t)(gid >> 8);
            ins[3] = (uint8_t)(k & 0xff);
            ins[4] = (uint8_t)(k >> 8);
        }
        if (dump) dumpGroup(g, pc0);
        extraStack = std::max(extraStack, (int)g.atoms.size() - 1);
        groups.push_back(std::move(g));
        ++rewritten;
        while (si < starts.size() && starts[si] < end) ++si;
    }
    return rewritten;
}

// ─────────────────────────────────────────────────────────────────
// Public entry points
// ─────────────────────────────────────────────────────────────────

SpecialiseMode getSpecialiseMode() {
    // Under the debugger the bytecode keeps QuickJS's byte offsets, which
    // the line tables describe (debugger/DebugHooks.h).
    if (debughooks::enabled()) return SpecialiseMode::Off;
    static SpecialiseMode cached = []() {
        const char* v = std::getenv("PROTOJS_SPECIALISER");
        // On by default.  "compact" and "nop", the names of the two
        // layouts the pass had before the guarded one, still mean on.
        if (!v || !v[0]) return SpecialiseMode::On;
        if (!std::strcmp(v, "off")) return SpecialiseMode::Off;
        if (!std::strcmp(v, "on") || !std::strcmp(v, "compact") || !std::strcmp(v, "nop"))
            return SpecialiseMode::On;
        fprintf(stderr,
                "protojs: unknown PROTOJS_SPECIALISER value '%s' "
                "(want on|off) -- the specialiser is off\n",
                v);
        return SpecialiseMode::Off;
    }();
    return cached;
}

const uint8_t* bytecodeOpcodeSizes() { return getOpcodeSizes(); }

std::vector<uint8_t> specialise(const uint8_t* buf, int len,
                                SpecialiseMode mode) {
    if (!buf || len <= 0) return {};
    switch (mode) {
        case SpecialiseMode::Off:
            return std::vector<uint8_t>(buf, buf + len);
        case SpecialiseMode::On:
            return specialiseGuarded(buf, len);
    }
    return std::vector<uint8_t>(buf, buf + len);
}

}  // namespace protojs
