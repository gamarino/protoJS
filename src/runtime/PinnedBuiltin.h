#pragma once

// PinnedBuiltin -- a built-in object made on first use and kept alive for the
// life of the wrapper that made it.
//
// Several built-ins are made lazily and cached in a static: the iterator
// prototypes (%ArrayIteratorPrototype%, %SetIteratorPrototype%, ...), the
// Deferred prototype, the prototypes of native-module objects (http, net,
// crypto, ...). Nothing in the heap references such a prototype except the
// objects made from it, and a C++ static is not a root. Once every such object
// had been collected, the prototype was collected too, and the static handed
// the freed cell to the next object made: under a small heap ceiling
// `new Set([1]).values()` came back without `next` or crashed.
//
// keep() caches the object for the current thread's space and pins it in the
// current wrapper's root set. A built-in made while no wrapper is current (at
// start-up some are installed before the wrapper makes itself current) is
// cached anyway -- its identity must not change, since it is already
// installed as some constructor's `prototype` -- and pinned by the first get()
// that finds a wrapper. A cache made for another space is never read.

// Declare it `static thread_local`: each thread runs its own space.

#include <protoCore.h>

namespace protojs {

class PinnedBuiltin {
public:
    // The cached object for ctx's space and wrapper, or nullptr.
    const proto::ProtoObject* get(proto::ProtoContext* ctx) const;
    // Caches obj, pins it if a wrapper is current, and returns it.
    const proto::ProtoObject* keep(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

private:
    void pin() const;

    const proto::ProtoObject* obj_ = nullptr;
    const proto::ProtoSpace* space_ = nullptr;
    mutable bool pinned_ = false;
};

}  // namespace protojs
