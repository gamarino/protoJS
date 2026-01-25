#ifndef PROTOJS_CRYPTOMODULE_H
#define PROTOJS_CRYPTOMODULE_H
#include "quickjs.h"
#include <string>
#include <vector>

namespace protojs {

class CryptoModule {
public:
    static void init(JSContext* ctx);

private:
    // Hash functions
    static JSValue createHash(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue hashUpdate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue hashDigest(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void HashFinalizer(JSRuntime* rt, JSValue val);
    
    // Random
    static JSValue randomBytes(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Cipher functions
    static JSValue createCipher(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue createDecipher(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue createCipheriv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue createDecipheriv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue cipherUpdate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue cipherFinal(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void CipherFinalizer(JSRuntime* rt, JSValue val);
    
    // Sign/Verify functions
    static JSValue createSign(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue createVerify(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue signUpdate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue signSign(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue verifyUpdate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue verifyVerify(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void SignFinalizer(JSRuntime* rt, JSValue val);
    static void VerifyFinalizer(JSRuntime* rt, JSValue val);
    
    // Key generation
    static JSValue generateKeyPair(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Helper functions
    static std::vector<uint8_t> getBufferData(JSContext* ctx, JSValueConst val);
    static std::string hashToString(const unsigned char* hash, size_t len);
};

} // namespace protojs

#endif
