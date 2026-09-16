#pragma once

// ProtoCoreNativeBindings — protoCore-native installation of the
// `protoCore` global module.
//
// Replaces the QuickJS-side src/modules/ProtoCoreModule.cpp registration,
// which installed its object on the QuickJS global object that scripts
// running on the protoCore interpreter never see.  Exposes:
//
//   protoCore.Set / Multiset / SparseList
//     - Constructors backed by protoCore's persistent collections.  The
//       collection is held in a private attribute of the instance and
//       republished with a compare-and-swap on every mutation, so two
//       threads mutating the same instance cannot lose an update.
//       `size()` is a method, not a property.
//
//   protoCore.Tuple(array)
//     - Builds the elements in protoCore list storage and returns them as
//       a regular JavaScript Array.
//
//   protoCore.ImmutableObject / MutableObject / makeImmutable / makeMutable
//     - ProtoObject::clone with the requested mutability.
//
//   protoCore.isImmutable(value)
//     - Placeholder: protoCore's public API has no mutability query, so
//       primitives report true and objects always report false.
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
// This is the only `protoCore` binding; the QuickJS-side module is no
// longer built.

#include <protoCore.h>

namespace protojs {

class ProtoCoreNativeBindings {
public:
    // Install `protoCore` as a module object on the protoCore-native
    // global.  Returns the (possibly new) global root pointer.
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);

    // The native worker registry (cpuChunk, ...) lives in the .cpp.
};

}  // namespace protojs
