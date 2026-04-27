#include "CryptoModule.h"
#include "../../ProtoNativeModule.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
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

// ---- Cipher / Sign / Verify (placeholder constructors) ----------------
//
// Match the original implementation: the constructors return objects
// with `update` / `final|sign|verify` methods that are no-ops or
// return empty.  Real OpenSSL wiring would require Buffer first.

const proto::ProtoObject* placeholderUpdate(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    return self ? self : PROTO_NONE;
}

const proto::ProtoObject* placeholderEmptyString(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
}

const proto::ProtoObject* placeholderFalse(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    return PROTO_FALSE;
}

const proto::ProtoObject* makeStubInstance(
        proto::ProtoContext* ctx,
        const NativeEntry* entries, size_t count) {
    const proto::ProtoObject* proto =
        ProtoNativeModule::buildModule(ctx, entries, count);
    return proto ? proto->newChild(ctx, /*mutable=*/true)
                 : ctx->newObject(/*mutable=*/true);
}

const proto::ProtoObject* createCipherImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    static const NativeEntry e[] = {
        {"update", placeholderUpdate},
        {"final",  placeholderEmptyString},
        NATIVE_MODULE_END
    };
    return makeStubInstance(ctx, e, 2);
}

const proto::ProtoObject* createSignImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    static const NativeEntry e[] = {
        {"update", placeholderUpdate},
        {"sign",   placeholderEmptyString},
        NATIVE_MODULE_END
    };
    return makeStubInstance(ctx, e, 2);
}

const proto::ProtoObject* createVerifyImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    static const NativeEntry e[] = {
        {"update", placeholderUpdate},
        {"verify", placeholderFalse},
        NATIVE_MODULE_END
    };
    return makeStubInstance(ctx, e, 2);
}

const proto::ProtoObject* generateKeyPairImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* pubK =
        ctx->fromUTF8String("publicKey")->asString(ctx);
    const proto::ProtoString* privK =
        ctx->fromUTF8String("privateKey")->asString(ctx);
    if (pubK)  obj->setAttribute(ctx, pubK,  ctx->fromUTF8String("placeholder"));
    if (privK) obj->setAttribute(ctx, privK, ctx->fromUTF8String("placeholder"));
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
        {"createDecipher",   createCipherImpl},   // same shape
        {"createCipheriv",   createCipherImpl},
        {"createDecipheriv", createCipherImpl},
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
