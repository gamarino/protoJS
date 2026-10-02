#pragma once

// ThreadIdentity -- the identity-bearing interpreter state a thread must share
// with the other threads of its space.
//
// The interpreter keeps a handful of objects in thread-locals whose IDENTITY is
// part of the language: the objects that represent `null`, `undefined` and an
// uninitialised (TDZ) binding, the marker every closure cell is parented on,
// %BigInt.prototype%, %RegExp.prototype%, and the prototypes `protoCore.Set`,
// `Multiset` and `SparseList` recognise `new` by. They were thread-locals
// because, until the Deferred pool, one thread ran one space. A Deferred's
// function runs on a pool thread of the OWNER's space and shares objects with
// the owner thread without copying, so a pool thread must use the owner's
// objects: with its own `undefined`, every `x === undefined` on a value made by
// the other thread is false; with its own cell marker, a closure made on the
// main thread reads its captured cells as plain objects.
//
// captureThreadIdentity() runs on the owner thread and records the owner's
// objects (making the lazily-created ones first, so the pool never makes its
// own); adoptThreadIdentity() runs on a pool thread before each task and
// installs them. Every object recorded here is created at start-up or kept
// alive by the global object, so the raw pointers stay valid for the life of
// the wrapper.
//
// Built-ins cached in a PinnedBuiltin are already per space
// (runtime/PinnedBuiltin.h) and need no adoption.

#include <protoCore.h>

namespace protojs {

struct ThreadIdentity {
    const proto::ProtoObject* nullSentinel = nullptr;
    const proto::ProtoObject* undefinedSentinel = nullptr;
    const proto::ProtoObject* tdzSentinel = nullptr;
    const proto::ProtoObject* cellMarker = nullptr;
    const proto::ProtoObject* bigIntPrototype = nullptr;
    bool bigIntMethodsInstalled = false;
    const proto::ProtoObject* regexpPrototype = nullptr;
    const proto::ProtoObject* pcSetPrototype = nullptr;
    const proto::ProtoObject* pcMultisetPrototype = nullptr;
    const proto::ProtoObject* pcSparseListPrototype = nullptr;
};

/** Owner thread: record this thread's identity objects (creating the lazy ones). */
ThreadIdentity captureThreadIdentity(proto::ProtoContext* ctx);

/** Pool thread: install `identity` as this thread's. */
void adoptThreadIdentity(const ThreadIdentity& identity);

// Per-module hooks, implemented next to the thread-locals they read and write.
void captureInterpreterIdentity(proto::ProtoContext* ctx, ThreadIdentity& out);
void adoptInterpreterIdentity(const ThreadIdentity& in);
void captureBigIntIdentity(ThreadIdentity& out);
void adoptBigIntIdentity(const ThreadIdentity& in);
void captureRegExpIdentity(ThreadIdentity& out);
void adoptRegExpIdentity(const ThreadIdentity& in);
void captureProtoCoreBindingsIdentity(ThreadIdentity& out);
void adoptProtoCoreBindingsIdentity(const ThreadIdentity& in);

}  // namespace protojs
