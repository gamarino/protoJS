#include "CryptoModule.h"
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <vector>

namespace protojs {

void CryptoModule::init(JSContext* ctx) {
    JSValue cryptoModule = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, cryptoModule, "createHash", JS_NewCFunction(ctx, createHash, "createHash", 1));
    JS_SetPropertyStr(ctx, cryptoModule, "randomBytes", JS_NewCFunction(ctx, randomBytes, "randomBytes", 1));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "crypto", cryptoModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue CryptoModule::createHash(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "createHash expects algorithm name");
    const char* algo = JS_ToCString(ctx, argv[0]);
    if (!algo) return JS_EXCEPTION;
    
    // Simplified - would create Hash object
    JSValue hashObj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, hashObj, "update", JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_DupValue(ctx, this_val);
    }, "update", 1));
    JS_SetPropertyStr(ctx, hashObj, "digest", JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        // Simplified hash
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Final(hash, &sha256);
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return JS_NewString(ctx, ss.str().c_str());
    }, "digest", 1));
    
    JS_FreeCString(ctx, algo);
    return hashObj;
}

JSValue CryptoModule::randomBytes(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "randomBytes expects size");
    int32_t size;
    if (JS_ToInt32(ctx, &size, argv[0]) != 0) return JS_EXCEPTION;
    if (size < 0 || size > 1024) return JS_ThrowTypeError(ctx, "Invalid size");
    
    std::vector<unsigned char> bytes(size);
    if (RAND_bytes(bytes.data(), size) != 1) {
        return JS_ThrowTypeError(ctx, "Failed to generate random bytes");
    }
    
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < size; i++) {
        JS_SetPropertyUint32(ctx, arr, i, JS_NewInt32(ctx, bytes[i]));
    }
    return arr;
}

} // namespace protojs
