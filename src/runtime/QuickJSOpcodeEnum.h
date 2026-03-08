#ifndef PROTOJS_QUICKJS_OPCODE_ENUM_H
#define PROTOJS_QUICKJS_OPCODE_ENUM_H

/**
 * Opcode enum matching quickjs.c OPCodeEnum (with SHORT_OPCODES=1).
 *
 * IMPORTANT: Only DEF() opcodes (uppercase) are included in the main
 * sequential range.  def() opcodes (lowercase, phase-1 temporary) are
 * excluded from the count so that the byte values of the final-bytecode
 * opcodes match exactly those emitted by QuickJS's js_create_function().
 *
 * The def() opcodes are assigned a separate range starting from
 * OP_TEMP_START = OP_nop + 1, identical to QuickJS's OPCodeEnum layout.
 * They must never appear in the final bytecode (resolved in phase 2/3).
 */
#define SHORT_OPCODES 1
#ifndef FMT
#define FMT(f)
#endif

enum ProtoJSOpcode {
    /* Main opcodes (DEF only) — byte value == enum value. */
#define DEF(id, size, n_pop, n_push, f) OP_ ## id,
#define def(id, size, n_pop, n_push, f)
#include "quickjs-opcode.h"
#undef def
#undef DEF
#undef FMT
    OP_COUNT, /* number of real opcodes */

    /* Temporary (phase-1) opcodes: start right after OP_nop, overlapping
     * with the short-opcode range.  These are never present in final bytecode
     * produced by js_create_function(). */
    OP_TEMP_START = OP_nop + 1,
    OP___dummy_padding = OP_TEMP_START - 1,
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f)
#define def(id, size, n_pop, n_push, f) OP_ ## id,
#include "quickjs-opcode.h"
#undef def
#undef DEF
#undef FMT
    OP_TEMP_END,
};

#endif /* PROTOJS_QUICKJS_OPCODE_ENUM_H */
