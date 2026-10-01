/*
 * ProtoCoreTypes — protoCore's spelling of its 64-bit integers.
 *
 * protoCore's API uses proto::proto_long / proto::proto_ulong (literals
 * PROTO_L(x) / PROTO_UL(x), printf conversion PROTO_FMT_U). They ARE long and
 * unsigned long on Linux and macOS, and long long / unsigned long long on
 * Windows, where long is 32 bits. protoJS uses them wherever a value meets
 * protoCore (hashes, sizes, keys, callbacks) or needs 64 bits.
 *
 * protoCore declares them from 2.7.0, which is protoJS's floor (CMakeLists.txt).
 * The installed-package path checks that version; the developer fallback to a
 * sibling build tree checks none, so an older protoCore is refused here by name
 * instead of with a page of errors about proto_long.
 */
#ifndef PROTOJS_PROTOCORETYPES_H
#define PROTOJS_PROTOCORETYPES_H

#include "protoCore.h"

#ifndef PROTO_FMT_U
#error "protoJS needs protoCore 2.7.0 or later (proto::proto_long and PROTO_UL are missing)"
#endif

#endif // PROTOJS_PROTOCORETYPES_H
