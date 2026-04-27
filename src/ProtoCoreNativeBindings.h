#pragma once

// ProtoCoreNativeBindings — protoCore-native installation of the
// `protoCore` global module.
//
// Replaces (for the user-visible binding) the QuickJS-side
// src/modules/ProtoCoreModule.cpp registration on the QuickJS global
// object.  Currently exposes:
//
//   protoCore.runInThread(workerName, args)
//     - Looks up `workerName` in the native worker registry (the same
//       registry used by the QuickJS version: cpuChunk, etc.).
//     - Creates a ProtoThread on the shared ProtoSpace and runs the
//       worker on it natively (no per-thread JS runtime, no JS
//       function serialisation).
//     - Returns a ProtoDeferred that resolves with the worker's
//       result when the thread joins.
//
// Set / Multiset / SparseList classes from the QuickJS module are
// deferred; runInThread is the only piece parallel_cpu.js needs.

#include <protoCore.h>

namespace protojs {

class ProtoCoreNativeBindings {
public:
    // Install `protoCore` as a module object on the protoCore-native
    // global.  Returns the (possibly new) global root pointer.
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);

    // Worker registration — shared with the QuickJS-side ProtoCoreModule
    // which already registers cpuChunk.  We do NOT re-register here; the
    // existing static map is reused via a forward-declared accessor in
    // the .cpp.
};

}  // namespace protojs
