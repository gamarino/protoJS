import sys

def modify_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    target = """static const proto::ProtoObject* arrayOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
    for (unsigned long i = 0; i < argc; i++)
        arrSet(ctx, result, i, args->getAt(ctx, static_cast<int>(i)));
    return arrSetLen(ctx, result, argc);
}"""

    replacement = """static const proto::ProtoObject* arrayOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* result;
    const proto::ProtoString* constructKey = ctx->fromUTF8String("__construct__")->asString(ctx);
    const proto::ProtoObject* constructFn = (constructKey && self && self != PROTO_NONE) ? self->getAttribute(ctx, constructKey, false) : nullptr;
    bool isCtor = constructFn && constructFn != PROTO_NONE && constructFn->isMethod(ctx);
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;

    if (isCtor) {
        const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
        const proto::ProtoObject* proto = self->getAttribute(ctx, protoKey, true);
        result = (proto && proto != PROTO_NONE) ? proto->newChild(ctx, true) : ctx->newObject(true);
        const proto::ProtoList* ctorArgs = ctx->newList();
        ctorArgs = ctorArgs->appendLast(ctx, ctx->fromInteger(static_cast<long long>(argc)));
        proto::ProtoMethod fn = constructFn->asMethod(ctx);
        const proto::ProtoObject* res = fn(ctx, result, nullptr, ctorArgs, nullptr);
        if (res && res != PROTO_NONE && !res->isInteger(ctx) && !res->isDouble(ctx) && !res->asString(ctx) && res != PROTO_TRUE && res != PROTO_FALSE)
            result = res;
    } else {
        result = createNewArray(ctx, nullptr);
    }
    
    for (unsigned long i = 0; i < argc; i++)
        arrSet(ctx, result, i, args->getAt(ctx, static_cast<int>(i)));
    return arrSetLen(ctx, result, argc);
}"""

    if target in content:
        content = content.replace(target, replacement)
        with open(filepath, 'w') as f:
            f.write(content)
        print("Patched arrayOf successfully.")
    else:
        print("Target not found. Please check the code.")

modify_file("src/ArrayPrototype.cpp")
