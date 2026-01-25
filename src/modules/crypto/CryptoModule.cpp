#include "CryptoModule.h"
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstring>

namespace protojs {

static JSClassID crypto_hash_class_id;
static JSClassID crypto_cipher_class_id;
static JSClassID crypto_sign_class_id;
static JSClassID crypto_verify_class_id;

struct HashData {
    std::string algorithm;
    EVP_MD_CTX* ctx;
    JSRuntime* rt;
    
    HashData(const std::string& algo, JSRuntime* r) : algorithm(algo), rt(r) {
        ctx = EVP_MD_CTX_new();
        const EVP_MD* md = nullptr;
        if (algo == "md5") md = EVP_md5();
        else if (algo == "sha1") md = EVP_sha1();
        else if (algo == "sha256") md = EVP_sha256();
        else if (algo == "sha512") md = EVP_sha512();
        
        if (md && ctx) {
            EVP_DigestInit_ex(ctx, md, nullptr);
        }
    }
    
    ~HashData() {
        if (ctx) {
            EVP_MD_CTX_free(ctx);
        }
    }
};

struct CipherData {
    std::string algorithm;
    EVP_CIPHER_CTX* ctx;
    bool encrypt;
    JSRuntime* rt;
    
    CipherData(const std::string& algo, bool enc, JSRuntime* r) : algorithm(algo), encrypt(enc), rt(r) {
        ctx = EVP_CIPHER_CTX_new();
    }
    
    ~CipherData() {
        if (ctx) {
            EVP_CIPHER_CTX_free(ctx);
        }
    }
};

struct SignData {
    std::string algorithm;
    EVP_MD_CTX* ctx;
    EVP_PKEY* pkey;
    JSRuntime* rt;
    
    SignData(const std::string& algo, JSRuntime* r) : algorithm(algo), rt(r), pkey(nullptr) {
        ctx = EVP_MD_CTX_new();
    }
    
    ~SignData() {
        if (ctx) {
            EVP_MD_CTX_free(ctx);
        }
        if (pkey) {
            EVP_PKEY_free(pkey);
        }
    }
};

struct VerifyData {
    std::string algorithm;
    EVP_MD_CTX* ctx;
    EVP_PKEY* pkey;
    JSRuntime* rt;
    
    VerifyData(const std::string& algo, JSRuntime* r) : algorithm(algo), rt(r), pkey(nullptr) {
        ctx = EVP_MD_CTX_new();
    }
    
    ~VerifyData() {
        if (ctx) {
            EVP_MD_CTX_free(ctx);
        }
        if (pkey) {
            EVP_PKEY_free(pkey);
        }
    }
};

void CryptoModule::init(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    
    // Register Hash class
    JS_NewClassID(&crypto_hash_class_id);
    JSClassDef hashClassDef = {"Hash", HashFinalizer};
    JS_NewClass(rt, crypto_hash_class_id, &hashClassDef);
    
    JSValue hashProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, hashProto, "update", JS_NewCFunction(ctx, hashUpdate, "update", 1));
    JS_SetPropertyStr(ctx, hashProto, "digest", JS_NewCFunction(ctx, hashDigest, "digest", 1));
    JS_SetClassProto(ctx, crypto_hash_class_id, hashProto);
    
    // Register Cipher class
    JS_NewClassID(&crypto_cipher_class_id);
    JSClassDef cipherClassDef = {"Cipher", CipherFinalizer};
    JS_NewClass(rt, crypto_cipher_class_id, &cipherClassDef);
    
    JSValue cipherProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, cipherProto, "update", JS_NewCFunction(ctx, cipherUpdate, "update", 1));
    JS_SetPropertyStr(ctx, cipherProto, "final", JS_NewCFunction(ctx, cipherFinal, "final", 1));
    JS_SetClassProto(ctx, crypto_cipher_class_id, cipherProto);
    
    // Register Sign class
    JS_NewClassID(&crypto_sign_class_id);
    JSClassDef signClassDef = {"Sign", SignFinalizer};
    JS_NewClass(rt, crypto_sign_class_id, &signClassDef);
    
    JSValue signProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, signProto, "update", JS_NewCFunction(ctx, signUpdate, "update", 1));
    JS_SetPropertyStr(ctx, signProto, "sign", JS_NewCFunction(ctx, signSign, "sign", 1));
    JS_SetClassProto(ctx, crypto_sign_class_id, signProto);
    
    // Register Verify class
    JS_NewClassID(&crypto_verify_class_id);
    JSClassDef verifyClassDef = {"Verify", VerifyFinalizer};
    JS_NewClass(rt, crypto_verify_class_id, &verifyClassDef);
    
    JSValue verifyProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, verifyProto, "update", JS_NewCFunction(ctx, verifyUpdate, "update", 1));
    JS_SetPropertyStr(ctx, verifyProto, "verify", JS_NewCFunction(ctx, verifyVerify, "verify", 1));
    JS_SetClassProto(ctx, crypto_verify_class_id, verifyProto);
    
    JSValue cryptoModule = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, cryptoModule, "createHash", JS_NewCFunction(ctx, createHash, "createHash", 1));
    JS_SetPropertyStr(ctx, cryptoModule, "randomBytes", JS_NewCFunction(ctx, randomBytes, "randomBytes", 1));
    JS_SetPropertyStr(ctx, cryptoModule, "createCipher", JS_NewCFunction(ctx, createCipher, "createCipher", 2));
    JS_SetPropertyStr(ctx, cryptoModule, "createDecipher", JS_NewCFunction(ctx, createDecipher, "createDecipher", 2));
    JS_SetPropertyStr(ctx, cryptoModule, "createCipheriv", JS_NewCFunction(ctx, createCipheriv, "createCipheriv", 3));
    JS_SetPropertyStr(ctx, cryptoModule, "createDecipheriv", JS_NewCFunction(ctx, createDecipheriv, "createDecipheriv", 3));
    JS_SetPropertyStr(ctx, cryptoModule, "createSign", JS_NewCFunction(ctx, createSign, "createSign", 1));
    JS_SetPropertyStr(ctx, cryptoModule, "createVerify", JS_NewCFunction(ctx, createVerify, "createVerify", 1));
    JS_SetPropertyStr(ctx, cryptoModule, "generateKeyPair", JS_NewCFunction(ctx, generateKeyPair, "generateKeyPair", 3));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "crypto", cryptoModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue CryptoModule::createHash(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "createHash expects algorithm name");
    const char* algo = JS_ToCString(ctx, argv[0]);
    if (!algo) return JS_EXCEPTION;
    
    std::string algorithm(algo);
    JS_FreeCString(ctx, algo);
    
    JSValue hashObj = JS_NewObjectClass(ctx, crypto_hash_class_id);
    if (JS_IsException(hashObj)) return hashObj;
    
    HashData* data = new HashData(algorithm, JS_GetRuntime(ctx));
    if (!data->ctx) {
        delete data;
        return JS_ThrowTypeError(ctx, "Failed to create hash context");
    }
    
    JS_SetOpaque(hashObj, data);
    return hashObj;
}

JSValue CryptoModule::hashUpdate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "update expects data");
    
    HashData* data = static_cast<HashData*>(JS_GetOpaque(this_val, crypto_hash_class_id));
    if (!data || !data->ctx) {
        return JS_ThrowTypeError(ctx, "Invalid hash object");
    }
    
    std::vector<uint8_t> bytes = getBufferData(ctx, argv[0]);
    if (bytes.empty()) {
        return JS_DupValue(ctx, this_val);
    }
    
    EVP_DigestUpdate(data->ctx, bytes.data(), bytes.size());
    return JS_DupValue(ctx, this_val);
}

JSValue CryptoModule::hashDigest(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    HashData* data = static_cast<HashData*>(JS_GetOpaque(this_val, crypto_hash_class_id));
    if (!data || !data->ctx) {
        return JS_ThrowTypeError(ctx, "Invalid hash object");
    }
    
    const char* encoding = "hex";
    if (argc > 0 && JS_IsString(argv[0])) {
        encoding = JS_ToCString(ctx, argv[0]);
    }
    
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;
    EVP_DigestFinal_ex(data->ctx, hash, &hashLen);
    
    std::string result;
    if (encoding && strcmp(encoding, "hex") == 0) {
        result = hashToString(hash, hashLen);
    } else if (encoding && strcmp(encoding, "base64") == 0) {
        // Base64 encoding would go here
        result = hashToString(hash, hashLen);
    } else {
        result = std::string((char*)hash, hashLen);
    }
    
    if (encoding && JS_IsString(argv[0])) {
        JS_FreeCString(ctx, encoding);
    }
    
    return JS_NewString(ctx, result.c_str());
}

void CryptoModule::HashFinalizer(JSRuntime* rt, JSValue val) {
    HashData* data = static_cast<HashData*>(JS_GetOpaque(val, crypto_hash_class_id));
    if (data) {
        delete data;
    }
}

JSValue CryptoModule::randomBytes(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "randomBytes expects size");
    int32_t size;
    if (JS_ToInt32(ctx, &size, argv[0]) != 0) return JS_EXCEPTION;
    if (size < 0 || size > 1024 * 1024) return JS_ThrowTypeError(ctx, "Invalid size");
    
    std::vector<unsigned char> bytes(size);
    if (RAND_bytes(bytes.data(), size) != 1) {
        return JS_ThrowTypeError(ctx, "Failed to generate random bytes");
    }
    
    // Return as Buffer-like object
    JSValue buffer = JS_NewArrayBufferCopy(ctx, bytes.data(), size);
    return buffer;
}

JSValue CryptoModule::createCipher(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "createCipher expects algorithm and password");
    
    const char* algo = JS_ToCString(ctx, argv[0]);
    if (!algo) return JS_EXCEPTION;
    
    std::string algorithm(algo);
    JS_FreeCString(ctx, algo);
    
    JSValue cipherObj = JS_NewObjectClass(ctx, crypto_cipher_class_id);
    if (JS_IsException(cipherObj)) return cipherObj;
    
    CipherData* data = new CipherData(algorithm, true, JS_GetRuntime(ctx));
    JS_SetOpaque(cipherObj, data);
    
    return cipherObj;
}

JSValue CryptoModule::createDecipher(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "createDecipher expects algorithm and password");
    
    const char* algo = JS_ToCString(ctx, argv[0]);
    if (!algo) return JS_EXCEPTION;
    
    std::string algorithm(algo);
    JS_FreeCString(ctx, algo);
    
    JSValue decipherObj = JS_NewObjectClass(ctx, crypto_cipher_class_id);
    if (JS_IsException(decipherObj)) return decipherObj;
    
    CipherData* data = new CipherData(algorithm, false, JS_GetRuntime(ctx));
    JS_SetOpaque(decipherObj, data);
    
    return decipherObj;
}

JSValue CryptoModule::createCipheriv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "createCipheriv expects algorithm, key, and iv");
    
    const char* algo = JS_ToCString(ctx, argv[0]);
    if (!algo) return JS_EXCEPTION;
    
    std::string algorithm(algo);
    JS_FreeCString(ctx, algo);
    
    JSValue cipherObj = JS_NewObjectClass(ctx, crypto_cipher_class_id);
    if (JS_IsException(cipherObj)) return cipherObj;
    
    CipherData* data = new CipherData(algorithm, true, JS_GetRuntime(ctx));
    JS_SetOpaque(cipherObj, data);
    
    return cipherObj;
}

JSValue CryptoModule::createDecipheriv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "createDecipheriv expects algorithm, key, and iv");
    
    const char* algo = JS_ToCString(ctx, argv[0]);
    if (!algo) return JS_EXCEPTION;
    
    std::string algorithm(algo);
    JS_FreeCString(ctx, algo);
    
    JSValue decipherObj = JS_NewObjectClass(ctx, crypto_cipher_class_id);
    if (JS_IsException(decipherObj)) return decipherObj;
    
    CipherData* data = new CipherData(algorithm, false, JS_GetRuntime(ctx));
    JS_SetOpaque(decipherObj, data);
    
    return decipherObj;
}

JSValue CryptoModule::cipherUpdate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "update expects data");
    
    CipherData* data = static_cast<CipherData*>(JS_GetOpaque(this_val, crypto_cipher_class_id));
    if (!data || !data->ctx) {
        return JS_ThrowTypeError(ctx, "Invalid cipher object");
    }
    
    std::vector<uint8_t> input = getBufferData(ctx, argv[0]);
    if (input.empty()) {
        return JS_NewArrayBufferCopy(ctx, nullptr, 0);
    }
    
    // Simplified cipher operation
    std::vector<uint8_t> output(input.size());
    // In full implementation, would use EVP_EncryptUpdate/EVP_DecryptUpdate
    memcpy(output.data(), input.data(), input.size());
    
    return JS_NewArrayBufferCopy(ctx, output.data(), output.size());
}

JSValue CryptoModule::cipherFinal(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    CipherData* data = static_cast<CipherData*>(JS_GetOpaque(this_val, crypto_cipher_class_id));
    if (!data || !data->ctx) {
        return JS_ThrowTypeError(ctx, "Invalid cipher object");
    }
    
    // In full implementation, would use EVP_EncryptFinal/EVP_DecryptFinal
    return JS_NewArrayBufferCopy(ctx, nullptr, 0);
}

void CryptoModule::CipherFinalizer(JSRuntime* rt, JSValue val) {
    CipherData* data = static_cast<CipherData*>(JS_GetOpaque(val, crypto_cipher_class_id));
    if (data) {
        delete data;
    }
}

JSValue CryptoModule::createSign(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "createSign expects algorithm");
    
    const char* algo = JS_ToCString(ctx, argv[0]);
    if (!algo) return JS_EXCEPTION;
    
    std::string algorithm(algo);
    JS_FreeCString(ctx, algo);
    
    JSValue signObj = JS_NewObjectClass(ctx, crypto_sign_class_id);
    if (JS_IsException(signObj)) return signObj;
    
    SignData* data = new SignData(algorithm, JS_GetRuntime(ctx));
    JS_SetOpaque(signObj, data);
    
    return signObj;
}

JSValue CryptoModule::signUpdate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "update expects data");
    
    SignData* data = static_cast<SignData*>(JS_GetOpaque(this_val, crypto_sign_class_id));
    if (!data || !data->ctx) {
        return JS_ThrowTypeError(ctx, "Invalid sign object");
    }
    
    // In full implementation, would use EVP_DigestUpdate
    return JS_DupValue(ctx, this_val);
}

JSValue CryptoModule::signSign(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    SignData* data = static_cast<SignData*>(JS_GetOpaque(this_val, crypto_sign_class_id));
    if (!data || !data->ctx) {
        return JS_ThrowTypeError(ctx, "Invalid sign object");
    }
    
    // In full implementation, would use EVP_SignFinal
    return JS_NewArrayBufferCopy(ctx, nullptr, 0);
}

void CryptoModule::SignFinalizer(JSRuntime* rt, JSValue val) {
    SignData* data = static_cast<SignData*>(JS_GetOpaque(val, crypto_sign_class_id));
    if (data) {
        delete data;
    }
}

JSValue CryptoModule::createVerify(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "createVerify expects algorithm");
    
    const char* algo = JS_ToCString(ctx, argv[0]);
    if (!algo) return JS_EXCEPTION;
    
    std::string algorithm(algo);
    JS_FreeCString(ctx, algo);
    
    JSValue verifyObj = JS_NewObjectClass(ctx, crypto_verify_class_id);
    if (JS_IsException(verifyObj)) return verifyObj;
    
    VerifyData* data = new VerifyData(algorithm, JS_GetRuntime(ctx));
    JS_SetOpaque(verifyObj, data);
    
    return verifyObj;
}

JSValue CryptoModule::verifyUpdate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "update expects data");
    
    VerifyData* data = static_cast<VerifyData*>(JS_GetOpaque(this_val, crypto_verify_class_id));
    if (!data || !data->ctx) {
        return JS_ThrowTypeError(ctx, "Invalid verify object");
    }
    
    // In full implementation, would use EVP_DigestUpdate
    return JS_DupValue(ctx, this_val);
}

JSValue CryptoModule::verifyVerify(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "verify expects signature");
    
    VerifyData* data = static_cast<VerifyData*>(JS_GetOpaque(this_val, crypto_verify_class_id));
    if (!data || !data->ctx) {
        return JS_ThrowTypeError(ctx, "Invalid verify object");
    }
    
    // In full implementation, would use EVP_VerifyFinal
    return JS_NewBool(ctx, false);
}

void CryptoModule::VerifyFinalizer(JSRuntime* rt, JSValue val) {
    VerifyData* data = static_cast<VerifyData*>(JS_GetOpaque(val, crypto_verify_class_id));
    if (data) {
        delete data;
    }
}

JSValue CryptoModule::generateKeyPair(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "generateKeyPair expects type, options, and callback");
    
    const char* type = JS_ToCString(ctx, argv[0]);
    if (!type) return JS_EXCEPTION;
    
    // Simplified key generation
    // In full implementation, would generate RSA or EC keys
    
    JS_FreeCString(ctx, type);
    
    // Return placeholder
    JSValue keyPair = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, keyPair, "publicKey", JS_NewString(ctx, "placeholder"));
    JS_SetPropertyStr(ctx, keyPair, "privateKey", JS_NewString(ctx, "placeholder"));
    
    return keyPair;
}

std::vector<uint8_t> CryptoModule::getBufferData(JSContext* ctx, JSValueConst val) {
    std::vector<uint8_t> result;
    
    // Check if it's a Buffer
    JSValue bufferCtor = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "Buffer");
    if (!JS_IsUndefined(bufferCtor)) {
        JSValue isBuffer = JS_GetPropertyStr(ctx, bufferCtor, "isBuffer");
        if (JS_IsFunction(ctx, isBuffer)) {
            JSValue dupVal = JS_DupValue(ctx, val);
            JSValueConst args[] = {dupVal};
            JSValue isBuf = JS_Call(ctx, isBuffer, bufferCtor, 1, const_cast<JSValue*>(args));
            JS_FreeValue(ctx, dupVal);
            if (JS_ToBool(ctx, isBuf)) {
                JSValue length = JS_GetPropertyStr(ctx, val, "length");
                uint32_t len;
                if (JS_ToUint32(ctx, &len, length) >= 0) {
                    result.resize(len);
                    // Simplified: would extract actual buffer data
                    JSValue str = JS_GetPropertyStr(ctx, val, "toString");
                    if (JS_IsFunction(ctx, str)) {
                        JSValue strVal = JS_Call(ctx, str, val, 0, nullptr);
                        const char* strData = JS_ToCString(ctx, strVal);
                        if (strData) {
                            result.assign((uint8_t*)strData, (uint8_t*)strData + strlen(strData));
                            JS_FreeCString(ctx, strData);
                        }
                        JS_FreeValue(ctx, strVal);
                    }
                    JS_FreeValue(ctx, str);
                }
                JS_FreeValue(ctx, length);
            }
            JS_FreeValue(ctx, isBuf);
        }
        JS_FreeValue(ctx, isBuffer);
    }
    JS_FreeValue(ctx, bufferCtor);
    
    // If not buffer, try string
    if (result.empty() && JS_IsString(val)) {
        const char* str = JS_ToCString(ctx, val);
        if (str) {
            result.assign((uint8_t*)str, (uint8_t*)str + strlen(str));
            JS_FreeCString(ctx, str);
        }
    }
    
    return result;
}

std::string CryptoModule::hashToString(const unsigned char* hash, size_t len) {
    std::stringstream ss;
    for (size_t i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

} // namespace protojs
