#pragma once

// OwnAttributes -- enumeration of an object's own protoCore attributes.
//
// protoJS enumerates own attributes through ProtoObject::processOwnAttributes,
// never by materialising ProtoObject::getOwnAttributes: the walk allocates no
// cell, hands each attribute's name to the callback directly (the sparse list
// is keyed by the name's address reinterpreted as an integer), and leaves
// protoCore free to change how it stores attributes.
//
// The contract of the walk (protoCore.h, processOwnAttributes):
//   - The name is the canonical key the attribute was set with: an interned
//     symbol or an inline string. Neither is ever collected, so a name may be
//     held in a C++ local across allocations and JavaScript calls.
//   - Attributes holding PROTO_NONE are reported; removed ones are not.
//   - A mutable receiver is walked as it stood when the walk started; the
//     callback may mutate it. How that snapshot is kept reachable while the
//     callback runs depends on the protoCore protoJS is built against (below).
//   - The order is unspecified (protoJS documents that its own-key order
//     deviates from insertion order for named attributes).
//
// Keeping the walked snapshot reachable -- this depends on the protoCore
// release:
//   - protoCore 3.3.1 and earlier (every release protoJS CI builds against:
//     v3.3.1 / 5a8cd7c7, and the floor job's 3.1.0 / 267b9fc9): the snapshot
//     is anchored in the context's single ProtoContext::pendingRoot slot,
//     saved and restored around the walk. Any other user of that slot on the
//     same context displaces the anchor for its duration: a nested
//     processOwnAttributes walk and the allocating builder newList(n, ...)
//     (in 3.3.x also the ProtoLinkedList builders). A collection in that
//     window can free the attribute tree under the outer walk. The callback
//     therefore must not enumerate another object, must not call those
//     builders, and must not run JavaScript, which could do either.
//   - protoCore with numaes/protoCore#4 (ProtoContext::pendingRoot removed):
//     the snapshot of a mutable receiver is pinned in the space's root set for
//     the duration of the walk, and the pin nests. Nested walks and allocating
//     builders inside the callback are safe there.
//
// Until protoJS's protoCore floor includes #4, protoJS keeps the 3.3.1
// restriction: where the work done per attribute can run JavaScript (a getter,
// a reviver, GetIterator) or enumerate another object, the caller collects the
// names, or the (name, value) pairs, with ownAttributeNames /
// ownAttributeEntries and does that work after the walk.
//
// The values come from the one version of the receiver the walk resolved, not
// from re-reading the live object key by key: another thread's run of writes
// to the object is published as one version, and an enumeration must observe
// all of it or none of it (tests/integration/deferred/
// put_field_group_atomicity.js). A value collected this way stays reachable
// through the receiver unless another thread replaces it before the caller
// stores it somewhere reachable: the walk's snapshot is no longer anchored once
// the walk returns, as for any value a builtin holds in a C++ local.

#include <protoCore.h>

#include <type_traits>
#include <utility>
#include <vector>

namespace protojs {

/// Calls `fn(name, value)` once per own attribute of `obj`, in protoCore's
/// (unspecified) order. `fn` may allocate cells, but with protoCore 3.3.1 and
/// earlier it must not run JavaScript, enumerate other objects or call the
/// newList(n, ...) / ProtoLinkedList builders (see above). Exceptions thrown by
/// `fn` propagate: protoCore releases its anchor of the snapshot (the
/// pendingRoot slot, or the root-set pin with #4) while unwinding.
template <typename Fn>
inline void forEachOwnAttribute(proto::ProtoContext* ctx, const proto::ProtoObject* obj, Fn&& fn) {
    if (!ctx || !obj) return;
    using Callable = std::remove_reference_t<Fn>;
    obj->processOwnAttributes(ctx, const_cast<void*>(static_cast<const void*>(&fn)),
        [](proto::ProtoContext*, void* self, const proto::ProtoString* name,
           const proto::ProtoObject* value) {
            (*static_cast<Callable*>(self))(name, value);
        });
}

/// Appends the names of `obj`'s own attributes to `out`, in protoCore's order.
inline void ownAttributeNames(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                              std::vector<const proto::ProtoString*>& out) {
    forEachOwnAttribute(ctx, obj, [&out](const proto::ProtoString* name, const proto::ProtoObject*) {
        if (name) out.push_back(name);
    });
}

/// Appends the (name, value) pairs of `obj`'s own attributes to `out`, in
/// protoCore's order, all read from one version of `obj`.
inline void ownAttributeEntries(
    proto::ProtoContext* ctx, const proto::ProtoObject* obj,
    std::vector<std::pair<const proto::ProtoString*, const proto::ProtoObject*>>& out) {
    forEachOwnAttribute(ctx, obj, [&out](const proto::ProtoString* name, const proto::ProtoObject* value) {
        if (name) out.emplace_back(name, value);
    });
}

} // namespace protojs
