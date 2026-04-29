#include "CryptoModule.h"
#include "../../ProtoNativeModule.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
#include "../../runtime/ProtoInterpreter.h"
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

namespace {

// ---- ExternalPointer-backed Hash state ---------------------------------
//
// The Hash instance carries `__hash_ctx__` — a ProtoExternalPointer to
// the OpenSSL EVP_MD_CTX.  Lifetime: freed by the C++ finalizer
// registered with `fromExternalPointer`.  The GC sees the wrapper as a
// regular Cell and traces it through processReferences; the embedded
// raw pointer never moves.

struct HashState {
    EVP_MD_CTX* ctx = nullptr;
    bool finalized = false;
};

void freeHashState(void* p) {
    auto* h = static_cast<HashState*>(p);
    if (!h) return;
    if (h->ctx) EVP_MD_CTX_free(h->ctx);
    delete h;
}

HashState* getHashState(proto::ProtoContext* ctx,
                         const proto::ProtoObject* self) {
    if (!ctx || !self) return nullptr;
    const proto::ProtoString* k =
        ctx->fromUTF8String("__hash_ctx__")->asString(ctx);
    if (!k) return nullptr;
    const proto::ProtoObject* attr = self->getAttribute(ctx, k, false);
    if (!attr || attr == PROTO_NONE) return nullptr;
    const proto::ProtoExternalPointer* ext = attr->asExternalPointer(ctx);
    return ext ? static_cast<HashState*>(ext->getPointer(ctx)) : nullptr;
}

const EVP_MD* mdFor(const std::string& algo) {
    if (algo == "md5")    return EVP_md5();
    if (algo == "sha1")   return EVP_sha1();
    if (algo == "sha256") return EVP_sha256();
    if (algo == "sha512") return EVP_sha512();
    return nullptr;
}

std::string hexOf(const unsigned char* data, size_t len) {
    std::stringstream ss;
    for (size_t i = 0; i < len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(data[i]);
    }
    return ss.str();
}

bool argString(proto::ProtoContext* ctx, const proto::ProtoList* args,
                int idx, std::string& out) {
    if (!ctx || !args) return false;
    if (idx >= static_cast<int>(args->getSize(ctx))) return false;
    const proto::ProtoObject* a = args->getAt(ctx, idx);
    if (!a || !a->isString(ctx)) return false;
    a->asString(ctx)->toUTF8String(ctx, out);
    return true;
}

// Best-effort byte extraction: accepts ProtoString.  ArrayBuffer /
// Buffer support would require a Buffer migration first — currently
// stubbed (returns the toString form).
std::vector<uint8_t> extractBytes(proto::ProtoContext* ctx,
                                    const proto::ProtoObject* val) {
    std::vector<uint8_t> out;
    if (!ctx || !val || val == PROTO_NONE) return out;
    if (val->isString(ctx)) {
        std::string s;
        val->asString(ctx)->toUTF8String(ctx, s);
        out.assign(s.begin(), s.end());
    }
    return out;
}

// ---- Hash class --------------------------------------------------------

const proto::ProtoObject* hashUpdateImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    HashState* h = getHashState(ctx, self);
    if (!h || h->finalized || !h->ctx) return self ? self : PROTO_NONE;
    if (!args || args->getSize(ctx) == 0) return self;
    auto bytes = extractBytes(ctx, args->getAt(ctx, 0));
    if (!bytes.empty()) {
        EVP_DigestUpdate(h->ctx, bytes.data(), bytes.size());
    }
    return self;  // chainable
}

const proto::ProtoObject* hashDigestImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    HashState* h = getHashState(ctx, self);
    if (!h || h->finalized || !h->ctx) {
        return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    }
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int len = 0;
    EVP_DigestFinal_ex(h->ctx, digest, &len);
    h->finalized = true;

    std::string encoding = "hex";
    argString(ctx, args, 0, encoding);
    if (encoding == "hex") {
        return ctx->fromUTF8String(hexOf(digest, len).c_str());
    }
    // Fallback: raw bytes as latin1 string (matches the previous module
    // when no encoding was given).
    return ctx->fromUTF8String(
        std::string(reinterpret_cast<const char*>(digest), len).c_str());
}

const proto::ProtoObject* createHash(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*);

const proto::ProtoObject* hashConstructor(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    // Instances are produced by `crypto.createHash`; bare
    // `new crypto.Hash()` is not part of the documented API but we
    // keep the constructor wired to the `__construct__` hook so
    // `new` works without crashing — returns the empty newObj.
    return PROTO_NONE;
}

const proto::ProtoObject* createHash(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string algo;
    if (!argString(ctx, args, 0, algo)) return PROTO_NONE;
    const EVP_MD* md = mdFor(algo);
    if (!md) return PROTO_NONE;

    auto* state = new HashState{};
    state->ctx = EVP_MD_CTX_new();
    if (!state->ctx || EVP_DigestInit_ex(state->ctx, md, nullptr) != 1) {
        delete state;
        return PROTO_NONE;
    }
    const proto::ProtoObject* extPtr =
        ctx->fromExternalPointer(state, freeHashState);
    if (!extPtr) {
        EVP_MD_CTX_free(state->ctx);
        delete state;
        return PROTO_NONE;
    }

    // Build the instance with the hash methods on the prototype chain.
    static const NativeEntry hashProtoEntries[] = {
        {"update", hashUpdateImpl},
        {"digest", hashDigestImpl},
        NATIVE_MODULE_END
    };
    static const proto::ProtoObject* hashProto = nullptr;
    if (!hashProto) {
        hashProto = ProtoNativeModule::buildModule(ctx, hashProtoEntries, 2);
    }
    const proto::ProtoObject* inst = hashProto
        ? hashProto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* k =
        ctx->fromUTF8String("__hash_ctx__")->asString(ctx);
    if (k) inst->setAttribute(ctx, k, extPtr);
    return inst;
}

// ---- randomBytes -------------------------------------------------------

const proto::ProtoObject* randomBytesImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* a = args->getAt(ctx, 0);
    if (!a || !a->isInteger(ctx)) return PROTO_NONE;
    long long n = a->asLong(ctx);
    if (n < 0 || n > 1024 * 1024) return PROTO_NONE;
    std::vector<unsigned char> buf(static_cast<size_t>(n));
    if (RAND_bytes(buf.data(), static_cast<int>(n)) != 1) return PROTO_NONE;
    // Returned as a hex-encoded string; richer Buffer support comes
    // when the Buffer module migrates.
    return ctx->fromUTF8String(hexOf(buf.data(), buf.size()).c_str());
}

// ---- Cipher state -------------------------------------------------------

struct CipherState {
    EVP_CIPHER_CTX* ctx  = nullptr;
    bool            enc  = true;
    bool            done = false;
};

void freeCipherState(void* p) {
    auto* c = static_cast<CipherState*>(p);
    if (!c) return;
    if (c->ctx) EVP_CIPHER_CTX_free(c->ctx);
    delete c;
}

CipherState* getCipherState(proto::ProtoContext* ctx,
                             const proto::ProtoObject* self) {
    if (!ctx || !self) return nullptr;
    const proto::ProtoString* k =
        ctx->fromUTF8String("__cipher_ctx__")->asString(ctx);
    if (!k) return nullptr;
    const proto::ProtoObject* attr = self->getAttribute(ctx, k, false);
    if (!attr || attr == PROTO_NONE) return nullptr;
    const proto::ProtoExternalPointer* ext = attr->asExternalPointer(ctx);
    return ext ? static_cast<CipherState*>(ext->getPointer(ctx)) : nullptr;
}

const EVP_CIPHER* cipherFor(const std::string& algo) {
    if (algo == "aes-128-cbc") return EVP_aes_128_cbc();
    if (algo == "aes-192-cbc") return EVP_aes_192_cbc();
    if (algo == "aes-256-cbc") return EVP_aes_256_cbc();
    if (algo == "aes-128-ecb") return EVP_aes_128_ecb();
    if (algo == "aes-256-ecb") return EVP_aes_256_ecb();
    return nullptr;
}

// Decode a hex string to raw bytes.
static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        unsigned int b = 0;
        sscanf(hex.c_str() + i, "%02x", &b);
        out.push_back(static_cast<uint8_t>(b));
    }
    return out;
}

// cipher.update(data) — feed data, return hex output.
// For encrypt: data is a plaintext string (UTF-8 bytes).
// For decrypt: data is a hex string (decoded to cipher bytes first).
const proto::ProtoObject* cipherUpdateImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    CipherState* c = getCipherState(ctx, self);
    if (!c || c->done || !c->ctx) return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    const proto::ProtoObject* arg = args ? args->getAt(ctx, 0) : nullptr;
    if (!arg || arg == PROTO_NONE || !arg->isString(ctx)) return ctx->fromUTF8String("");
    std::string inStr;
    arg->asString(ctx)->toUTF8String(ctx, inStr);
    std::vector<uint8_t> inBytes;
    if (c->enc) {
        inBytes.assign(inStr.begin(), inStr.end());  // plaintext -> raw bytes
    } else {
        inBytes = hexToBytes(inStr);                  // hex ciphertext -> raw bytes
    }
    if (inBytes.empty()) return ctx->fromUTF8String("");
    std::vector<unsigned char> out(inBytes.size() + EVP_MAX_BLOCK_LENGTH);
    int outLen = 0;
    int rc = EVP_CipherUpdate(c->ctx, out.data(), &outLen,
                              inBytes.data(), static_cast<int>(inBytes.size()));
    if (rc != 1) return ctx->fromUTF8String("");
    if (c->enc) {
        // encrypt output -> hex string
        return ctx->fromUTF8String(hexOf(out.data(), static_cast<size_t>(outLen)).c_str());
    } else {
        // decrypt output -> plaintext string
        return ctx->fromUTF8String(
            std::string(reinterpret_cast<char*>(out.data()), static_cast<size_t>(outLen)).c_str());
    }
}

// cipher.final() — flush padding, return remaining output.
const proto::ProtoObject* cipherFinalImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    CipherState* c = getCipherState(ctx, self);
    if (!c || c->done || !c->ctx) return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    unsigned char out[EVP_MAX_BLOCK_LENGTH * 2] = {};
    int outLen = 0;
    int rc = EVP_CipherFinal_ex(c->ctx, out, &outLen);
    c->done = true;
    if (rc != 1) {
        signalNativeException(makeNativeError(ctx, "Error",
            "crypto cipher final: decryption failed (bad key, IV, or corrupt data)"));
        return PROTO_NONE;
    }
    if (c->enc) {
        return ctx->fromUTF8String(hexOf(out, static_cast<size_t>(outLen)).c_str());
    } else {
        return ctx->fromUTF8String(
            std::string(reinterpret_cast<char*>(out), static_cast<size_t>(outLen)).c_str());
    }
}

const proto::ProtoObject* getCipherProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"update", cipherUpdateImpl},
        {"final",  cipherFinalImpl},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 2);
    return proto;
}

const proto::ProtoObject* buildCipherInstance(
    proto::ProtoContext* ctx, EVP_CIPHER_CTX* evpCtx, bool enc) {
    auto* state = new CipherState{evpCtx, enc, false};
    const proto::ProtoObject* extPtr =
        ctx->fromExternalPointer(state, freeCipherState);
    if (!extPtr) {
        EVP_CIPHER_CTX_free(evpCtx);
        delete state;
        return PROTO_NONE;
    }
    const proto::ProtoObject* cproto = getCipherProto(ctx);
    const proto::ProtoObject* inst   = cproto
        ? cproto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* k =
        ctx->fromUTF8String("__cipher_ctx__")->asString(ctx);
    if (k) inst->setAttribute(ctx, k, extPtr);
    return inst;
}

// createCipheriv(algorithm, key, iv) / createDecipheriv — explicit key+IV
static const proto::ProtoObject* makeCipherIv(
    proto::ProtoContext* ctx, const proto::ProtoList* args, bool enc) {
    std::string algo, keyStr, ivStr;
    if (!argString(ctx, args, 0, algo)) return PROTO_NONE;
    if (!argString(ctx, args, 1, keyStr)) return PROTO_NONE;
    argString(ctx, args, 2, ivStr);

    const EVP_CIPHER* cipher = cipherFor(algo);
    if (!cipher) {
        std::string msg = "crypto.createCipheriv: unsupported algorithm: " + algo;
        signalNativeException(makeNativeError(ctx, "Error", msg.c_str()));
        return PROTO_NONE;
    }
    EVP_CIPHER_CTX* evpCtx = EVP_CIPHER_CTX_new();
    if (!evpCtx) return PROTO_NONE;
    const auto* key = reinterpret_cast<const unsigned char*>(keyStr.data());
    const unsigned char* iv = ivStr.empty() ? nullptr
        : reinterpret_cast<const unsigned char*>(ivStr.data());
    if (EVP_CipherInit_ex(evpCtx, cipher, nullptr, key, iv, enc ? 1 : 0) != 1) {
        EVP_CIPHER_CTX_free(evpCtx);
        return PROTO_NONE;
    }
    return buildCipherInstance(ctx, evpCtx, enc);
}

const proto::ProtoObject* createCipherivEncImpl(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return makeCipherIv(ctx, args, true);
}
const proto::ProtoObject* createDecipherivImpl(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return makeCipherIv(ctx, args, false);
}

// createCipher(algorithm, password) / createDecipher — key+IV derived via EVP_BytesToKey
static const proto::ProtoObject* makeCipherDerived(
    proto::ProtoContext* ctx, const proto::ProtoList* args, bool enc) {
    std::string algo, pass;
    if (!argString(ctx, args, 0, algo)) return PROTO_NONE;
    if (!argString(ctx, args, 1, pass)) return PROTO_NONE;

    const EVP_CIPHER* cipher = cipherFor(algo);
    if (!cipher) {
        std::string msg = "crypto.createCipher: unsupported algorithm: " + algo;
        signalNativeException(makeNativeError(ctx, "Error", msg.c_str()));
        return PROTO_NONE;
    }
    unsigned char key[EVP_MAX_KEY_LENGTH] = {};
    unsigned char iv[EVP_MAX_IV_LENGTH]   = {};
    EVP_BytesToKey(cipher, EVP_md5(), nullptr,
                   reinterpret_cast<const unsigned char*>(pass.data()),
                   static_cast<int>(pass.size()), 1, key, iv);

    EVP_CIPHER_CTX* evpCtx = EVP_CIPHER_CTX_new();
    if (!evpCtx) return PROTO_NONE;
    if (EVP_CipherInit_ex(evpCtx, cipher, nullptr, key, iv, enc ? 1 : 0) != 1) {
        EVP_CIPHER_CTX_free(evpCtx);
        return PROTO_NONE;
    }
    return buildCipherInstance(ctx, evpCtx, enc);
}

const proto::ProtoObject* createCipherImpl(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return makeCipherDerived(ctx, args, true);
}
const proto::ProtoObject* createDecipherImpl(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return makeCipherDerived(ctx, args, false);
}

// ---- Sign / Verify state -----------------------------------------------

struct SignState {
    EVP_MD_CTX*          mdCtx   = nullptr;
    bool                 done    = false;
    std::vector<uint8_t> dataBuf;  // data buffered until key is known at sign()/verify()
};

void freeSignState(void* p) {
    auto* s = static_cast<SignState*>(p);
    if (!s) return;
    if (s->mdCtx) EVP_MD_CTX_free(s->mdCtx);
    delete s;
}

SignState* getSignState(proto::ProtoContext* ctx,
                        const proto::ProtoObject* self) {
    if (!ctx || !self) return nullptr;
    const proto::ProtoString* k =
        ctx->fromUTF8String("__sign_ctx__")->asString(ctx);
    if (!k) return nullptr;
    const proto::ProtoObject* attr = self->getAttribute(ctx, k, false);
    if (!attr || attr == PROTO_NONE) return nullptr;
    const proto::ProtoExternalPointer* ext = attr->asExternalPointer(ctx);
    return ext ? static_cast<SignState*>(ext->getPointer(ctx)) : nullptr;
}

const EVP_MD* mdForSign(const std::string& algo) {
    if (algo == "RSA-SHA1"   || algo == "sha1WithRSAEncryption")   return EVP_sha1();
    if (algo == "RSA-SHA256" || algo == "sha256WithRSAEncryption") return EVP_sha256();
    if (algo == "RSA-SHA512" || algo == "sha512WithRSAEncryption") return EVP_sha512();
    return nullptr;
}

// sign.update(data) — buffer data for later feeding into the keyed operation
const proto::ProtoObject* signUpdateImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    SignState* s = getSignState(ctx, self);
    if (!s || s->done) return self ? self : PROTO_NONE;
    const proto::ProtoObject* arg = args ? args->getAt(ctx, 0) : nullptr;
    if (!arg || !arg->isString(ctx)) return self;
    std::string str;
    arg->asString(ctx)->toUTF8String(ctx, str);
    s->dataBuf.insert(s->dataBuf.end(), str.begin(), str.end());
    return self;
}

// sign.sign(privateKeyPem) -> hex signature string
const proto::ProtoObject* signFinalImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    SignState* s = getSignState(ctx, self);
    if (!s || s->done || !s->mdCtx) return ctx ? ctx->fromUTF8String("") : PROTO_NONE;

    std::string pemStr;
    if (!argString(ctx, args, 0, pemStr)) {
        signalNativeException(makeNativeError(ctx, "Error",
            "sign.sign: privateKey PEM string required"));
        return PROTO_NONE;
    }
    BIO* bio = BIO_new_mem_buf(pemStr.data(), static_cast<int>(pemStr.size()));
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) {
        signalNativeException(makeNativeError(ctx, "Error",
            "sign.sign: failed to parse private key PEM"));
        return PROTO_NONE;
    }

    // Retrieve algorithm from the context before reinit.
    const EVP_MD* md = EVP_MD_CTX_get0_md(s->mdCtx);
    if (!md) {
        EVP_PKEY_free(pkey);
        signalNativeException(makeNativeError(ctx, "Error",
            "sign.sign: could not retrieve digest algorithm from context"));
        return PROTO_NONE;
    }

    // Must create a new context for DigestSign (cannot re-initialize an existing one safely)
    EVP_MD_CTX* signCtx = EVP_MD_CTX_new();
    if (!signCtx) {
        EVP_PKEY_free(pkey);
        return PROTO_NONE;
    }
    if (EVP_DigestSignInit(signCtx, nullptr, md, nullptr, pkey) != 1) {
        EVP_MD_CTX_free(signCtx);
        EVP_PKEY_free(pkey);
        signalNativeException(makeNativeError(ctx, "Error",
            "sign.sign: EVP_DigestSignInit failed"));
        return PROTO_NONE;
    }
    if (!s->dataBuf.empty() &&
        EVP_DigestSignUpdate(signCtx, s->dataBuf.data(), s->dataBuf.size()) != 1) {
        EVP_MD_CTX_free(signCtx);
        EVP_PKEY_free(pkey);
        s->done = true;
        std::vector<uint8_t>().swap(s->dataBuf);  // release memory
        signalNativeException(makeNativeError(ctx, "Error",
            "sign.sign: EVP_DigestSignUpdate failed"));
        return PROTO_NONE;
    }
    size_t sigLen = 0;
    if (EVP_DigestSignFinal(signCtx, nullptr, &sigLen) != 1 || sigLen == 0) {
        EVP_MD_CTX_free(signCtx);
        EVP_PKEY_free(pkey);
        s->done = true;
        std::vector<uint8_t>().swap(s->dataBuf);  // release memory
        signalNativeException(makeNativeError(ctx, "Error",
            "sign.sign: EVP_DigestSignFinal (size probe) failed"));
        return PROTO_NONE;
    }
    std::vector<unsigned char> sig(sigLen);
    int rc = EVP_DigestSignFinal(signCtx, sig.data(), &sigLen);
    EVP_MD_CTX_free(signCtx);
    EVP_PKEY_free(pkey);
    s->done = true;
    std::vector<uint8_t>().swap(s->dataBuf);  // release memory

    if (rc != 1) {
        signalNativeException(makeNativeError(ctx, "Error",
            "sign.sign: EVP_DigestSignFinal failed"));
        return PROTO_NONE;
    }
    return ctx->fromUTF8String(hexOf(sig.data(), sigLen).c_str());
}

// verify.update(data) — buffer data for later feeding into the keyed operation
const proto::ProtoObject* verifyUpdateImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    // Same as signUpdateImpl — buffers data
    SignState* s = getSignState(ctx, self);
    if (!s || s->done) return self ? self : PROTO_NONE;
    const proto::ProtoObject* arg = args ? args->getAt(ctx, 0) : nullptr;
    if (!arg || !arg->isString(ctx)) return self;
    std::string str;
    arg->asString(ctx)->toUTF8String(ctx, str);
    s->dataBuf.insert(s->dataBuf.end(), str.begin(), str.end());
    return self;
}

// verify.verify(publicKeyPem, hexSig) -> bool
const proto::ProtoObject* verifyFinalImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    SignState* s = getSignState(ctx, self);
    if (!s || s->done || !s->mdCtx) return PROTO_FALSE;

    std::string pemStr, hexSig;
    if (!argString(ctx, args, 0, pemStr) || !argString(ctx, args, 1, hexSig))
        return PROTO_FALSE;

    BIO* bio = BIO_new_mem_buf(pemStr.data(), static_cast<int>(pemStr.size()));
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return PROTO_FALSE;

    const EVP_MD* md = EVP_MD_CTX_get0_md(s->mdCtx);
    if (!md) {
        EVP_PKEY_free(pkey);
        return PROTO_FALSE;
    }

    EVP_MD_CTX* verCtx = EVP_MD_CTX_new();
    if (!verCtx) {
        EVP_PKEY_free(pkey);
        return PROTO_FALSE;
    }
    if (EVP_DigestVerifyInit(verCtx, nullptr, md, nullptr, pkey) != 1) {
        EVP_MD_CTX_free(verCtx);
        EVP_PKEY_free(pkey);
        return PROTO_FALSE;
    }
    if (!s->dataBuf.empty() &&
        EVP_DigestVerifyUpdate(verCtx, s->dataBuf.data(), s->dataBuf.size()) != 1) {
        EVP_MD_CTX_free(verCtx);
        EVP_PKEY_free(pkey);
        s->done = true;
        std::vector<uint8_t>().swap(s->dataBuf);  // release memory
        return PROTO_FALSE;
    }

    // Decode hex signature
    std::vector<unsigned char> sig;
    sig.reserve(hexSig.size() / 2);
    for (size_t i = 0; i + 1 < hexSig.size(); i += 2) {
        unsigned int byte = 0;
        sscanf(hexSig.c_str() + i, "%02x", &byte);
        sig.push_back(static_cast<unsigned char>(byte));
    }

    int rc = EVP_DigestVerifyFinal(verCtx, sig.data(), sig.size());
    EVP_MD_CTX_free(verCtx);
    EVP_PKEY_free(pkey);
    s->done = true;
    std::vector<uint8_t>().swap(s->dataBuf);  // release memory
    return (rc == 1) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* getSignerProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* sproto = nullptr;
    if (sproto) return sproto;
    static const NativeEntry entries[] = {
        {"update", signUpdateImpl},
        {"sign",   signFinalImpl},
        NATIVE_MODULE_END
    };
    sproto = ProtoNativeModule::buildModule(ctx, entries, 2);
    return sproto;
}

const proto::ProtoObject* getVerifierProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* vproto = nullptr;
    if (vproto) return vproto;
    static const NativeEntry entries[] = {
        {"update", verifyUpdateImpl},
        {"verify", verifyFinalImpl},
        NATIVE_MODULE_END
    };
    vproto = ProtoNativeModule::buildModule(ctx, entries, 2);
    return vproto;
}

const proto::ProtoObject* buildSignInstance(
    proto::ProtoContext* ctx, const std::string& algo, bool signing) {
    const EVP_MD* md = mdForSign(algo);
    if (!md) {
        std::string msg = std::string(signing ? "createSign" : "createVerify")
            + ": unsupported algorithm: " + algo;
        signalNativeException(makeNativeError(ctx, "Error", msg.c_str()));
        return PROTO_NONE;
    }
    // Create a digest context primed with the algorithm, for later retrieval via
    // EVP_MD_CTX_get0_md() at sign()/verify() time.
    EVP_MD_CTX* mdCtx = EVP_MD_CTX_new();
    if (!mdCtx) return PROTO_NONE;
    if (EVP_DigestInit_ex(mdCtx, md, nullptr) != 1) {
        EVP_MD_CTX_free(mdCtx);
        return PROTO_NONE;
    }

    auto* state = new SignState{mdCtx, false, {}};
    const proto::ProtoObject* extPtr =
        ctx->fromExternalPointer(state, freeSignState);
    if (!extPtr) {
        EVP_MD_CTX_free(mdCtx);
        delete state;
        return PROTO_NONE;
    }

    const proto::ProtoObject* sProto = signing ? getSignerProto(ctx) : getVerifierProto(ctx);
    const proto::ProtoObject* inst   = sProto
        ? sProto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* k =
        ctx->fromUTF8String("__sign_ctx__")->asString(ctx);
    if (k) inst->setAttribute(ctx, k, extPtr);
    return inst;
}

const proto::ProtoObject* createSignImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string algo;
    argString(ctx, args, 0, algo);
    if (algo.empty()) algo = "RSA-SHA256";
    return buildSignInstance(ctx, algo, true);
}

const proto::ProtoObject* createVerifyImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string algo;
    argString(ctx, args, 0, algo);
    if (algo.empty()) algo = "RSA-SHA256";
    return buildSignInstance(ctx, algo, false);
}

const proto::ProtoObject* generateKeyPairImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;

    std::string keyType;
    argString(ctx, args, 0, keyType);
    if (keyType.empty()) keyType = "rsa";
    if (keyType != "rsa") {
        signalNativeException(makeNativeError(ctx, "Error",
            "generateKeyPair: only 'rsa' key type is supported"));
        return PROTO_NONE;
    }

    long long bits = 2048;
    const proto::ProtoObject* optArg = (args && args->getSize(ctx) > 1)
        ? args->getAt(ctx, 1) : nullptr;
    if (optArg && optArg != PROTO_NONE) {
        const proto::ProtoString* mlKey =
            ctx->fromUTF8String("modulusLength")->asString(ctx);
        if (mlKey) {
            const proto::ProtoObject* mlVal =
                optArg->getAttribute(ctx, mlKey, false);
            if (mlVal && mlVal->isInteger(ctx))
                bits = mlVal->asLong(ctx);
        }
    }
    if (bits < 512 || bits > 8192) bits = 2048;

    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!pctx) return PROTO_NONE;
    if (EVP_PKEY_keygen_init(pctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, static_cast<int>(bits)) <= 0) {
        EVP_PKEY_CTX_free(pctx);
        return PROTO_NONE;
    }
    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0 || !pkey) {
        EVP_PKEY_CTX_free(pctx);
        return PROTO_NONE;
    }
    EVP_PKEY_CTX_free(pctx);

    // Serialize public key to PEM (PKCS#8 SubjectPublicKeyInfo)
    BIO* pubBio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(pubBio, pkey);
    char* pubData = nullptr;
    long pubLen = BIO_get_mem_data(pubBio, &pubData);
    std::string pubPem(pubData, static_cast<size_t>(pubLen));
    BIO_free(pubBio);

    // Serialize private key to PEM (PKCS#8 PrivateKeyInfo)
    BIO* privBio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(privBio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    char* privData = nullptr;
    long privLen = BIO_get_mem_data(privBio, &privData);
    std::string privPem(privData, static_cast<size_t>(privLen));
    BIO_free(privBio);

    EVP_PKEY_free(pkey);

    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* pubK =
        ctx->fromUTF8String("publicKey")->asString(ctx);
    const proto::ProtoString* privK =
        ctx->fromUTF8String("privateKey")->asString(ctx);
    if (pubK)  obj->setAttribute(ctx, pubK,  ctx->fromUTF8String(pubPem.c_str()));
    if (privK) obj->setAttribute(ctx, privK, ctx->fromUTF8String(privPem.c_str()));
    return obj;
}

}  // namespace

const proto::ProtoObject* CryptoModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    static const NativeEntry entries[] = {
        {"createHash",       createHash},
        {"randomBytes",      randomBytesImpl},
        {"createCipher",     createCipherImpl},
        {"createDecipher",   createDecipherImpl},
        {"createCipheriv",   createCipherivEncImpl},
        {"createDecipheriv", createDecipherivImpl},
        {"createSign",       createSignImpl},
        {"createVerify",     createVerifyImpl},
        {"generateKeyPair",  generateKeyPairImpl},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, entries, 9);
    if (!mod) return globalObj;
    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "crypto", mod);
}

} // namespace protojs
