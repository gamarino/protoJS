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
// keep() caches the object for ctx's SPACE and pins it in the current
// wrapper's root set. A built-in made while no wrapper is current (at start-up
// some are installed before the wrapper makes itself current) is cached anyway
// -- its identity must not change, since it is already installed as some
// constructor's `prototype` -- and pinned by the first get() that finds a
// wrapper.
//
// One cache per space, shared by every thread of that space. A space is not a
// thread: the Deferred pool (src/DeferredPool.h) runs JavaScript on several
// protoCore threads of the owner's space at once, and an intrinsic has ONE
// identity per realm -- an iterator made inside a Deferred must have the same
// %ArrayIteratorPrototype% as one made on the main thread. The cache used to be
// `static thread_local`, which gave each pool thread its own copy of every
// prototype. Declare it `static` (not thread_local). Different spaces
// (worker_threads workers, each with its own wrapper) still get their own
// entries, and ~JSContextWrapper drops the entries of its space
// (forgetSpace), so a space later allocated at the same address never reads a
// dead space's object.
//
// Concurrency: get() is lock-free (two acquire loads per probed slot). keep()
// takes a mutex and is first-wins: when two threads of one space build the
// same built-in at the same time, both receive the object of the one that
// published first, and the loser's object is simply not used. replace()
// overwrites unconditionally (re-initialisation of a realm's intrinsics).

#include <protoCore.h>

#include <atomic>
#include <cstdint>

namespace protojs {

class PinnedBuiltin {
public:
    // A per-thread front for get() on hot paths (createNewArray and the other
    // constructors of built-in objects). Declare one `static thread_local`
    // Memo next to the PinnedBuiltin it fronts and call get(ctx, memo): a hit
    // costs one relaxed load and two compares. The memo is refreshed when the
    // calling thread's space changes, and whenever any PinnedBuiltin publishes,
    // replaces or forgets an entry (a process-wide generation counter), so a
    // memo never returns an object of a destroyed space or a replaced one.
    struct Memo {
        const proto::ProtoSpace* space = nullptr;
        std::uint64_t generation = 0;
        const proto::ProtoObject* obj = nullptr;
    };

    PinnedBuiltin();
    ~PinnedBuiltin();
    PinnedBuiltin(const PinnedBuiltin&) = delete;
    PinnedBuiltin& operator=(const PinnedBuiltin&) = delete;

    // The cached object for ctx's space, or nullptr.
    const proto::ProtoObject* get(proto::ProtoContext* ctx) const;
    // get() through a per-thread memo (see Memo).
    const proto::ProtoObject* get(proto::ProtoContext* ctx, Memo& memo) const {
        const proto::ProtoSpace* space = ctx ? ctx->space : nullptr;
        const std::uint64_t gen = generation_.load(std::memory_order_acquire);
        if (memo.obj && memo.space == space && memo.generation == gen) return memo.obj;
        return refresh(ctx, memo, gen);
    }
    // Caches obj for ctx's space unless another thread already cached one, pins
    // it if a wrapper is current, and returns the cached object -- obj, or the
    // object that won.
    const proto::ProtoObject* keep(proto::ProtoContext* ctx, const proto::ProtoObject* obj);
    // Caches obj for ctx's space, replacing any previous entry, and returns it.
    const proto::ProtoObject* replace(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

    // Drop every entry of `space`, in every PinnedBuiltin. Called by
    // ~JSContextWrapper when no thread of that space runs any more.
    static void forgetSpace(const proto::ProtoSpace* space);

private:
    const proto::ProtoObject* refresh(proto::ProtoContext* ctx, Memo& memo,
                                      std::uint64_t gen) const;

    // Bumped by every publication, replacement and forgetSpace (see Memo).
    static std::atomic<std::uint64_t> generation_;

    // Enough for the main space and the worker_threads workers alive at once.
    // A space that finds no free slot gets an uncached object (correct for the
    // calling thread, but not shared), and a diagnostic once.
    static constexpr int kSlots = 32;

    struct Slot {
        std::atomic<const proto::ProtoSpace*> space{nullptr};
        std::atomic<const proto::ProtoObject*> obj{nullptr};
        std::atomic<bool> pinned{false};
    };

    const proto::ProtoObject* store(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                                    bool firstWins);
    void pin(Slot& slot) const;

    mutable Slot slots_[kSlots];
    PinnedBuiltin* next_ = nullptr;   // registry of every instance (forgetSpace)
};

}  // namespace protojs
