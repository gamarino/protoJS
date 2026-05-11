// ---------------------------------------------------------------------------
// coercePropNameToKey — convert any JS value to a property name ProtoString*
// per ECMAScript ToPropertyKey (supports Symbols natively).
// ---------------------------------------------------------------------------
static const proto::ProtoString* coercePropNameToKey(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* nameObj)
{
    std::string out;
    const proto::ProtoObject* current = nameObj;

    // ToPropertyKey(argument):
    // 1. Let key be ? ToPrimitive(argument, hint String).
    if (current && !current->isString(ctx) && !current->isInteger(ctx) && 
        !current->isDouble(ctx) && !current->isFloat(ctx) && 
        !current->isBoolean(ctx) && current != getNullSentinel() && 
        current != getUndefinedSentinel() && current != PROTO_NONE) 
    {
        // Object: call toString() (per ToPrimitive hint String)
        const proto::ProtoString* tsKey = ctx->fromUTF8String("toString")->asString(ctx);
        const proto::ProtoObject* tsFn = tsKey ? current->getAttribute(ctx, tsKey, true) : nullptr;
        if (tsFn && tsFn != PROTO_NONE) {
            const proto::ProtoObject* res = callJSFunction(ctx, tsFn, current, ctx->newList());
            if (!hasCallException() && res && res != PROTO_NONE) {
                current = res;
            }
        }
    }

    if (!current || current == PROTO_NONE || current == getUndefinedSentinel()) {
        out = "undefined";
    } else if (current->isString(ctx)) {
        return current->asString(ctx);
    } else if (current->isInteger(ctx)) {
        out = std::to_string(current->asLong(ctx));
    } else if (current->isDouble(ctx) || current->isFloat(ctx)) {
        double d = current->asDouble(ctx);
        if (std::isnan(d)) out = "NaN";
        else if (std::isinf(d)) out = d < 0 ? "-Infinity" : "Infinity";
        else if (d == 0.0) out = "0";
        else {
            double absD = std::abs(d);
            char buf[128];
            if (absD >= 1e21 || (absD > 0 && absD < 1e-6)) {
                snprintf(buf, sizeof(buf), "%.15g", d);
                out = buf;
                for (auto &c : out) if (c == 'E') c = 'e';
                size_t ePos = out.find('e');
                if (ePos != std::string::npos) {
                    std::string base = out.substr(0, ePos);
                    std::string exp  = out.substr(ePos + 1);
                    if (!exp.empty() && exp[0] == '+') exp.erase(0, 1);
                    bool neg = false;
                    if (!exp.empty() && exp[0] == '-') { neg = true; exp.erase(0, 1); }
                    while (exp.size() > 1 && exp[0] == '0') exp.erase(0, 1);
                    out = base + "e" + (neg ? "-" : "+") + exp;
                }
            } else {
                snprintf(buf, sizeof(buf), "%.15g", d);
                out = buf;
                if (out.find('e') != std::string::npos || out.find('E') != std::string::npos) {
                    snprintf(buf, sizeof(buf), "%.20f", d);
                    out = buf;
                    if (out.find('.') != std::string::npos) {
                        while (out.back() == '0') out.pop_back();
                        if (out.back() == '.') out.pop_back();
                    }
                }
            }
        }
    } else if (current->isBoolean(ctx)) {
        out = current == PROTO_TRUE ? "true" : "false";
    } else if (current == getNullSentinel()) {
        out = "null";
    } else {
        out = "[object Object]";
    }

    return ctx->fromUTF8String(out.c_str())->asString(ctx);
}
