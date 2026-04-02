#include "ProtoArrayAdapter.h"
#include "JSSymbols.h"

namespace protojs {

using namespace proto;

namespace {

/**
 * Internal helper to retrieve the underlying ProtoSparseList for an array
 * object. If the object is not yet a sparse list, it is converted lazily.
 *
 * Representation strategy:
 * - Arrays are backed by a ProtoSparseList, which naturally supports holes
 *   and large indices.
 * - The sparse list is stored as a field named "elements" on the array object.
 * - The logical length is stored as a numeric field named "length".
 */
static const ProtoSparseList* getSparse(
    ProtoContext* ctx,
    const ProtoObject* arrayObj)
{
    if (!arrayObj) {
        return nullptr;
    }

    const ProtoString* elementsKey =
        JSSymbols::elements(ctx);
    const ProtoObject* existing =
        arrayObj->getAttribute(ctx, elementsKey);

    if (existing && existing != PROTO_NONE) {
        // Assume it is already a sparse list.
        return existing->asSparseList(ctx);
    }

    // Lazily create an empty sparse list and attach it.
    const ProtoSparseList* empty =
        ctx->newSparseList();
    const ProtoObject* asObj = empty->asObject(ctx);
    const ProtoObject* updated =
        arrayObj->setAttribute(ctx, elementsKey, asObj);
    (void)updated; // Caller is responsible for capturing updated root.
    return empty;
}

static unsigned long getStoredLength(
    ProtoContext* ctx,
    const ProtoObject* arrayObj)
{
    if (!arrayObj) {
        return 0;
    }
    const ProtoString* lengthKey =
        JSSymbols::length(ctx);
    const ProtoObject* lenObj =
        arrayObj->getAttribute(ctx, lengthKey);
    if (!lenObj || lenObj == PROTO_NONE) {
        return 0;
    }
    return static_cast<unsigned long>(lenObj->asLong(ctx));
}

static const ProtoObject* storeLength(
    ProtoContext* ctx,
    const ProtoObject* arrayObj,
    unsigned long newLen)
{
    const ProtoString* lengthKey =
        JSSymbols::length(ctx);
    const ProtoObject* lenObj =
        ctx->fromLong(static_cast<long long>(newLen));
    return arrayObj->setAttribute(ctx, lengthKey, lenObj);
}

} // namespace

const ProtoObject* ProtoArrayAdapter::createArray(ProtoContext* ctx)
{
    // Represent the array as a plain ProtoObject with two attributes:
    // - "elements": ProtoSparseList backing store
    // - "length": numeric length
    const ProtoObject* obj = ctx->newObject(true);

    const ProtoSparseList* sparse = ctx->newSparseList();
    const ProtoObject* sparseObj = sparse->asObject(ctx);
    const ProtoString* elementsKey =
        JSSymbols::elements(ctx);
    obj = obj->setAttribute(ctx, elementsKey, sparseObj);

    obj = storeLength(ctx, obj, 0);
    return obj;
}

const ProtoObject* ProtoArrayAdapter::get(
    ProtoContext* ctx,
    const ProtoObject* arrayObj,
    unsigned long index)
{
    if (!arrayObj) {
        return PROTO_NONE;
    }

    const ProtoSparseList* sparse = getSparse(ctx, arrayObj);
    if (!sparse) {
        return PROTO_NONE;
    }

    // If the sparse list does not have the index, return PROTO_NONE to
    // represent a hole.
    if (!sparse->has(ctx, index)) {
        return PROTO_NONE;
    }
    const ProtoObject* value = sparse->getAt(ctx, index);
    return value ? value : PROTO_NONE;
}

const ProtoObject* ProtoArrayAdapter::set(
    ProtoContext* ctx,
    const ProtoObject* arrayObj,
    unsigned long index,
    const ProtoObject* value)
{
    if (!arrayObj) {
        arrayObj = createArray(ctx);
    }

    const ProtoSparseList* sparse = getSparse(ctx, arrayObj);
    if (!sparse) {
        sparse = ctx->newSparseList();
    }

    const ProtoSparseList* updatedSparse =
        sparse->setAt(ctx, index, value ? value : PROTO_NONE);
    const ProtoObject* sparseObj = updatedSparse->asObject(ctx);

    const ProtoString* elementsKey =
        JSSymbols::elements(ctx);
    const ProtoObject* updated =
        arrayObj->setAttribute(ctx, elementsKey, sparseObj);

    // Update length if needed.
    unsigned long currentLen = getStoredLength(ctx, updated);
    if (index + 1 > currentLen) {
        updated = storeLength(ctx, updated, index + 1);
    }

    return updated;
}

unsigned long ProtoArrayAdapter::length(
    ProtoContext* ctx,
    const ProtoObject* arrayObj)
{
    return getStoredLength(ctx, arrayObj);
}

const ProtoObject* ProtoArrayAdapter::setLength(
    ProtoContext* ctx,
    const ProtoObject* arrayObj,
    unsigned long newLen)
{
    // For now we keep the backing store unchanged and only update the
    // logical length. Elements with index >= newLen may still exist in the
    // sparse backing list, but they are considered inaccessible from the JS
    // side because QuickJS enforces the length invariant before using the
    // adapter.
    if (!arrayObj) {
        const ProtoObject* created = createArray(ctx);
        return storeLength(ctx, created, newLen);
    }
    return storeLength(ctx, arrayObj, newLen);
}

} // namespace protojs

