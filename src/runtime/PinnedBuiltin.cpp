#include "PinnedBuiltin.h"

#include "../JSContext.h"

#include <iostream>
#include <mutex>

namespace protojs {

namespace {

proto::ProtoRootSet* currentRootSet() {
    JSContextWrapper* w = JSContextWrapper::current();
    return w ? w->getRootSet() : nullptr;
}

// One mutex for every writer of every instance, and for the instance list.
// Writers are rare: one per built-in per space.
std::mutex& registryMutex() {
    static std::mutex m;
    return m;
}

PinnedBuiltin*& registryHead() {
    static PinnedBuiltin* head = nullptr;
    return head;
}

}  // namespace

PinnedBuiltin::PinnedBuiltin() {
    std::lock_guard<std::mutex> lock(registryMutex());
    next_ = registryHead();
    registryHead() = this;
}

PinnedBuiltin::~PinnedBuiltin() {
    std::lock_guard<std::mutex> lock(registryMutex());
    for (PinnedBuiltin** p = &registryHead(); *p; p = &(*p)->next_) {
        if (*p == this) { *p = next_; break; }
    }
}

void PinnedBuiltin::pin(Slot& slot) const {
    if (slot.pinned.load(std::memory_order_acquire)) return;
    const proto::ProtoObject* obj = slot.obj.load(std::memory_order_acquire);
    if (!obj) return;
    proto::ProtoRootSet* rs = currentRootSet();
    if (!rs) return;
    // Exactly one thread adds the pin; the others see `pinned` already set.
    bool expected = false;
    if (slot.pinned.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        rs->add(obj);  // for the life of the root set; never removed
}

const proto::ProtoObject* PinnedBuiltin::get(proto::ProtoContext* ctx) const {
    if (!ctx || !ctx->space) return nullptr;
    const proto::ProtoSpace* space = ctx->space;
    for (Slot& slot : slots_) {
        const proto::ProtoSpace* s = slot.space.load(std::memory_order_acquire);
        if (s != space) continue;
        const proto::ProtoObject* obj = slot.obj.load(std::memory_order_acquire);
        if (!obj) return nullptr;
        pin(slot);
        return obj;
    }
    return nullptr;
}

const proto::ProtoObject* PinnedBuiltin::store(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* obj,
                                               bool firstWins) {
    if (!ctx || !ctx->space || !obj || obj == PROTO_NONE) return obj;
    const proto::ProtoSpace* space = ctx->space;
    Slot* target = nullptr;
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        Slot* freeSlot = nullptr;
        for (Slot& slot : slots_) {
            const proto::ProtoSpace* s = slot.space.load(std::memory_order_relaxed);
            if (s == space) { target = &slot; break; }
            if (!s && !freeSlot) freeSlot = &slot;
        }
        if (target) {
            const proto::ProtoObject* current = target->obj.load(std::memory_order_relaxed);
            if (firstWins && current) {
                obj = current;
            } else if (current != obj) {
                target->obj.store(obj, std::memory_order_release);
                target->pinned.store(false, std::memory_order_release);
            }
        } else if (freeSlot) {
            // Publish the object before the space: a reader that matches the
            // space must find the object.
            freeSlot->pinned.store(false, std::memory_order_relaxed);
            freeSlot->obj.store(obj, std::memory_order_release);
            freeSlot->space.store(space, std::memory_order_release);
            target = freeSlot;
        } else {
            static std::once_flag warned;
            std::call_once(warned, [] {
                std::cerr << "protojs: PinnedBuiltin: more live spaces than cache slots; "
                             "a built-in is not cached for one of them" << std::endl;
            });
            return obj;
        }
    }
    pin(*target);
    return obj;
}

const proto::ProtoObject* PinnedBuiltin::keep(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* obj) {
    return store(ctx, obj, /*firstWins=*/true);
}

const proto::ProtoObject* PinnedBuiltin::replace(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* obj) {
    return store(ctx, obj, /*firstWins=*/false);
}

void PinnedBuiltin::forgetSpace(const proto::ProtoSpace* space) {
    if (!space) return;
    std::lock_guard<std::mutex> lock(registryMutex());
    for (PinnedBuiltin* b = registryHead(); b; b = b->next_) {
        for (Slot& slot : b->slots_) {
            if (slot.space.load(std::memory_order_relaxed) != space) continue;
            // Unpublish the space first, so that no reader matches a slot whose
            // object is being cleared. No thread of `space` runs any more, so
            // no reader can be looking for it anyway.
            slot.space.store(nullptr, std::memory_order_release);
            slot.obj.store(nullptr, std::memory_order_release);
            slot.pinned.store(false, std::memory_order_release);
        }
    }
}

}  // namespace protojs
