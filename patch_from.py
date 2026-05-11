import sys

def modify_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    start_str = "static const proto::ProtoObject* arrayFrom("
    end_str = "// Array.of static method"
    
    start_idx = content.find(start_str)
    end_idx = content.find(end_str)
    
    if start_idx == -1 or end_idx == -1:
        print("Not found")
        return

    replacement = """static const proto::ProtoObject* arrayFrom(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return createNewArray(ctx, nullptr);
    const proto::ProtoObject* src = args->getAt(ctx, 0);
    if (!src || src == PROTO_NONE) return createNewArray(ctx, nullptr);
    
    const proto::ProtoObject* mapFn = args->getSize(ctx) > 1 ? args->getAt(ctx, 1) : nullptr;
    const proto::ProtoObject* thisArg = args->getSize(ctx) > 2 ? args->getAt(ctx, 2) : nullptr;
    if (mapFn == PROTO_NONE) mapFn = nullptr;

    const proto::ProtoObject* result;
    const proto::ProtoString* constructKey = ctx->fromUTF8String("__construct__")->asString(ctx);
    const proto::ProtoObject* constructFn = (constructKey && self && self != PROTO_NONE) ? self->getAttribute(ctx, constructKey, false) : nullptr;
    bool isCtor = constructFn && constructFn != PROTO_NONE && constructFn->isMethod(ctx);

    const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(ctx);
    const proto::ProtoObject* iteratorFn = (symIterKey && src->isString(ctx) == false) ? src->getAttribute(ctx, symIterKey, true) : nullptr;
    
    if (iteratorFn && iteratorFn != PROTO_NONE && !src->isString(ctx)) {
        if (isCtor) {
            const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
            const proto::ProtoObject* proto = self->getAttribute(ctx, protoKey, true);
            result = (proto && proto != PROTO_NONE) ? proto->newChild(ctx, true) : ctx->newObject(true);
            proto::ProtoMethod fn = constructFn->asMethod(ctx);
            const proto::ProtoObject* res = fn(ctx, result, nullptr, ctx->newList(), nullptr);
            if (res && res != PROTO_NONE && !res->isInteger(ctx) && !res->isDouble(ctx) && !res->asString(ctx) && res != PROTO_TRUE && res != PROTO_FALSE)
                result = res;
        } else {
            result = createNewArray(ctx, nullptr);
        }
        
        const proto::ProtoList* noArgs = ctx->newList();
        const proto::ProtoObject* iterObj = callJSFunction(ctx, iteratorFn, src, noArgs);
        if (hasCallException() || !iterObj || iterObj == PROTO_NONE) return PROTO_NONE;
        
        const proto::ProtoString* nextKey = JSSymbols::next(ctx);
        const proto::ProtoObject* nextFn = nextKey ? iterObj->getAttribute(ctx, nextKey, true) : nullptr;
        if (!nextFn || nextFn == PROTO_NONE) return PROTO_NONE;
        
        const proto::ProtoString* valueK = JSSymbols::value(ctx);
        const proto::ProtoString* doneK = JSSymbols::done(ctx);
        
        long long k = 0;
        while (true) {
            const proto::ProtoObject* step = callJSFunction(ctx, nextFn, iterObj, noArgs);
            if (hasCallException() || !step || step == PROTO_NONE) return PROTO_NONE;
            
            const proto::ProtoObject* doneVal = doneK ? step->getAttribute(ctx, doneK, false) : nullptr;
            if (isTruthy(ctx, doneVal)) break;
            
            const proto::ProtoObject* nextValue = valueK ? step->getAttribute(ctx, valueK, false) : PROTO_NONE;
            
            if (mapFn) {
                const proto::ProtoList* cbArgs = ctx->newList();
                cbArgs = cbArgs->appendLast(ctx, nextValue ? nextValue : PROTO_NONE);
                cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(k));
                nextValue = callJSFunction(ctx, mapFn, thisArg, cbArgs);
                if (hasCallException()) return PROTO_NONE;
            }
            
            arrSet(ctx, result, k, nextValue);
            k++;
        }
        result = arrSetLen(ctx, result, k);
        return result;
    }
    
    unsigned long n = arrLen(ctx, src);
    if (isCtor) {
        const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
        const proto::ProtoObject* proto = self->getAttribute(ctx, protoKey, true);
        result = (proto && proto != PROTO_NONE) ? proto->newChild(ctx, true) : ctx->newObject(true);
        const proto::ProtoList* ctorArgs = ctx->newList();
        ctorArgs = ctorArgs->appendLast(ctx, ctx->fromInteger(static_cast<long long>(n)));
        proto::ProtoMethod fn = constructFn->asMethod(ctx);
        const proto::ProtoObject* res = fn(ctx, result, nullptr, ctorArgs, nullptr);
        if (res && res != PROTO_NONE && !res->isInteger(ctx) && !res->isDouble(ctx) && !res->asString(ctx) && res != PROTO_TRUE && res != PROTO_FALSE)
            result = res;
    } else {
        result = createNewArray(ctx, nullptr);
    }
    
    for (unsigned long i = 0; i < n; i++) {
        const proto::ProtoObject* val = arrGet(ctx, src, i);
        if (mapFn) {
            const proto::ProtoList* cbArgs = ctx->newList();
            cbArgs = cbArgs->appendLast(ctx, val ? val : PROTO_NONE);
            cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(i));
            val = callJSFunction(ctx, mapFn, thisArg, cbArgs);
            if (hasCallException()) return PROTO_NONE;
        }
        arrSet(ctx, result, i, val);
    }
    result = arrSetLen(ctx, result, n);
    return result;
}

// ---------------------------------------------------------------------------
"""

    content = content[:start_idx] + replacement + content[end_idx-74:]
    with open(filepath, 'w') as f:
        f.write(content)

modify_file("src/ArrayPrototype.cpp")
