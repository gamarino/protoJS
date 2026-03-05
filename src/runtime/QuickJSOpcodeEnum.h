#ifndef PROTOJS_QUICKJS_OPCODE_ENUM_H
#define PROTOJS_QUICKJS_OPCODE_ENUM_H

/**
 * Opcode enum matching quickjs.c (with SHORT_OPCODES=1) so we can dispatch
 * on bytecode bytes. Must be kept in sync with quickjs-opcode.h include order.
 */
#define SHORT_OPCODES 1
#ifndef FMT
#define FMT(f)
#endif
enum ProtoJSOpcode {
#define DEF(id, size, n_pop, n_push, f) OP_ ## id,
#define def(id, size, n_pop, n_push, f) OP_ ## id,
#include "quickjs-opcode.h"
#undef def
#undef DEF
#undef FMT
};

#endif /* PROTOJS_QUICKJS_OPCODE_ENUM_H */
