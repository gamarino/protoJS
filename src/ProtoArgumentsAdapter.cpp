#include "ProtoArgumentsAdapter.h"
#include "JSSymbols.h"

namespace protojs {

using namespace proto;

namespace {

static const ProtoSparseList* getValuesSparse(
    ProtoContext* ctx,
    const ProtoObject* argsObj)
{
    if (!argsObj) {
        return nullptr;
    }

    const ProtoString* valuesKey =
        JSSymbols::values(ctx);
    const ProtoObject* existing =
        argsObj->getAttribute(ctx, valuesKey);

    if (existing && existing != PROTO_NONE) {
        return existing->asSparseList(ctx);
    }

    const ProtoSparseList* empty = ctx->newSparseList();
    const ProtoObject* asObj = empty->asObject(ctx);
    const ProtoObject* updated =
        argsObj->setAttribute(ctx, valuesKey, asObj);
    (void)updated; // Caller is responsible for capturing updated root.
    return empty;
}

static unsigned long getStoredLength(
    ProtoContext* ctx,
    const ProtoObject* argsObj)
{
    if (!argsObj) {
        return 0;
    }
    const ProtoString* lengthKey =
        JSSymbols::length(ctx);
    const ProtoObject* lenObj =
        argsObj->getAttribute(ctx, lengthKey);
    if (!lenObj || lenObj == PROTO_NONE) {
        return 0;
    }
    return static_cast<unsigned long>(lenObj->asLong(ctx));
}

static const ProtoObject* storeLength(
    ProtoContext* ctx,
    const ProtoObject* argsObj,
    unsigned long newLen)
{
    const ProtoString* lengthKey =
        JSSymbols::length(ctx);
    const ProtoObject* lenObj =
        ctx->fromLong(static_cast<long long>(newLen));
    return argsObj->setAttribute(ctx, lengthKey, lenObj);
}

} // namespace

const ProtoObject* ProtoArgumentsAdapter::createArguments(ProtoContext* ctx)
{
    const ProtoObject* obj = ctx->newObject(true);

    const ProtoSparseList* values = ctx->newSparseList();
    const ProtoObject* valuesObj = values->asObject(ctx);
    const ProtoString* valuesKey =
        JSSymbols::values(ctx);
    obj = obj->setAttribute(ctx, valuesKey, valuesObj);

    obj = storeLength(ctx, obj, 0);
    return obj;
}

const ProtoObject* ProtoArgumentsAdapter::get(
    ProtoContext* ctx,
    const ProtoObject* argsObj,
    unsigned long index)
{
    if (!argsObj) {
        return PROTO_NONE;
    }

    const ProtoSparseList* values = getValuesSparse(ctx, argsObj);
    if (!values) {
        return PROTO_NONE;
    }

    if (!values->has(ctx, index)) {
        return PROTO_NONE;
    }
    const ProtoObject* value = values->getAt(ctx, index);
    return value ? value : PROTO_NONE;
}

const ProtoObject* ProtoArgumentsAdapter::set(
    ProtoContext* ctx,
    const ProtoObject* argsObj,
    unsigned long index,
    const ProtoObject* value)
{
    if (!argsObj) {
        argsObj = createArguments(ctx);
    }

    const ProtoSparseList* values = getValuesSparse(ctx, argsObj);
    if (!values) {
        values = ctx->newSparseList();
    }

    const ProtoSparseList* updatedValues =
        values->setAt(ctx, index, value ? value : PROTO_NONE);
    const ProtoObject* valuesObj = updatedValues->asObject(ctx);

    const ProtoString* valuesKey =
        JSSymbols::values(ctx);
    const ProtoObject* updated =
        argsObj->setAttribute(ctx, valuesKey, valuesObj);

    unsigned long currentLen = getStoredLength(ctx, updated);
    if (index + 1 > currentLen) {
        updated = storeLength(ctx, updated, index + 1);
    }

    return updated;
}

unsigned long ProtoArgumentsAdapter::length(
    ProtoContext* ctx,
    const ProtoObject* argsObj)
{
    return getStoredLength(ctx, argsObj);
}

const ProtoObject* ProtoArgumentsAdapter::setLength(
    ProtoContext* ctx,
    const ProtoObject* argsObj,
    unsigned long newLen)
{
    if (!argsObj) {
        const ProtoObject* created = createArguments(ctx);
        return storeLength(ctx, created, newLen);
    }
    return storeLength(ctx, argsObj, newLen);
}

} // namespace protojs

