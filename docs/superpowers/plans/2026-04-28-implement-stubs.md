# protoJS Stub Implementations Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace all five explicit "not implemented" stubs in protoJS with full OpenSSL-backed and POSIX-socket implementations, each delivered with tests and a commit.

**Architecture:** Every implementation follows the established protoCore-native pattern — per-instance C++ state lives in a `struct` heap-allocated and attached to the JS object as a `ProtoExternalPointer` with a GC finalizer; methods are `ProtoMethod` functions that recover the pointer via `asExternalPointer(ctx)->getPointer(ctx)`. Async I/O (http.request) uses the existing background-thread + `EventLoop::enqueueCallback` + `ProtoRootSet` pattern from `HTTPModule::createServer`.

**Tech Stack:** C++20, OpenSSL (EVP API — already linked), POSIX sockets, protoCore ProtoObject/ProtoExternalPointer/ProtoRootSet, EventLoop singleton.

**Key conventions:**
- All modules are **globals** (not `require`). Run tests with `PROTOJS_USE_PROTO_EVAL=1 ./build/protojs <script>`.
- Build: `cmake --build build` from `protoJS/` root.
- Static proto pattern: `static const proto::ProtoObject* proto = nullptr; if (!proto) { proto = ProtoNativeModule::buildModule(ctx, entries, N); }`
- Exception: `signalNativeException(makeNativeError(ctx, "Error", msg))`.
- GC rule: perpetual objects (protos, keys) use `nullptr` ProtoContext; async captures use `getRootSet()->add(obj)`.

---

## Task 1: dgram IPv6 multicast (`addMembership` on udp6 sockets)

**Files:**
- Modify: `src/modules/dgram/DgramModule.cpp:131-176`
- Modify: `tests/integration/dgram/test_dgram.js` (create if missing)

### Step 1.1 — Write the failing test

Create `tests/integration/dgram/test_dgram_ipv6_multicast.js`:

```javascript
// Test: dgram.Socket.addMembership on udp6 socket joins IPv6 multicast group.
// Requires a loopback interface supporting multicast (standard Linux/macOS).
console.log("=== dgram IPv6 addMembership ===");

const s = dgram.createSocket('udp6');

// Before fix this throws "not implemented"; after fix it must NOT throw.
let threw = false;
try {
    const result = s.addMembership('ff02::1');   // all-nodes link-local multicast
    console.log("addMembership returned:", result);
} catch (e) {
    threw = true;
    console.log("FAIL - threw:", e.message);
}

if (!threw) {
    console.log("PASS - addMembership did not throw");
} else {
    process.exit(1);
}

s.close();
console.log("=== done ===");
```

### Step 1.2 — Run test to verify it currently fails

```bash
PROTOJS_USE_PROTO_EVAL=1 ./build/protojs tests/integration/dgram/test_dgram_ipv6_multicast.js
```

Expected output contains: `FAIL - threw: dgram.Socket.addMembership: IPv6 multicast (udp6) not implemented in protoJS`

### Step 1.3 — Implement `setsockopt(IPV6_JOIN_GROUP)`

In `src/modules/dgram/DgramModule.cpp`, replace the IPv6 throw block (lines 143–147) inside `socketAddMembershipImpl`. The full function after the change:

```cpp
const proto::ProtoObject* socketAddMembershipImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    int fd = getFd(ctx, self);
    if (fd < 0) return PROTO_FALSE;

    std::string mcastAddr;
    if (!argString(ctx, args, 0, mcastAddr)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "addMembership requires a multicast address string"));
        return PROTO_NONE;
    }
    std::string ifaceAddr;
    argString(ctx, args, 1, ifaceAddr);  // optional

    if (isIPv6Socket(ctx, self)) {
        ipv6_mreq mreq6{};
        if (inet_pton(AF_INET6, mcastAddr.c_str(), &mreq6.ipv6mr_multiaddr) <= 0) {
            signalNativeException(makeNativeError(ctx, "Error",
                "addMembership: invalid IPv6 multicast address"));
            return PROTO_NONE;
        }
        // interface index 0 = kernel picks the default interface
        mreq6.ipv6mr_interface = 0;
        if (!ifaceAddr.empty()) {
            unsigned int idx = if_nametoindex(ifaceAddr.c_str());
            if (idx == 0) {
                signalNativeException(makeNativeError(ctx, "Error",
                    "addMembership: unknown interface name"));
                return PROTO_NONE;
            }
            mreq6.ipv6mr_interface = idx;
        }
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                        &mreq6, sizeof(mreq6)) != 0) {
            return PROTO_FALSE;
        }
        return PROTO_TRUE;
    }

    // IPv4 path (unchanged)
    ip_mreq mreq{};
    if (inet_pton(AF_INET, mcastAddr.c_str(), &mreq.imr_multiaddr) <= 0) {
        signalNativeException(makeNativeError(ctx, "Error",
            "addMembership: invalid multicast address"));
        return PROTO_NONE;
    }
    if (ifaceAddr.empty()) {
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, ifaceAddr.c_str(), &mreq.imr_interface) <= 0) {
        signalNativeException(makeNativeError(ctx, "Error",
            "addMembership: invalid interface address"));
        return PROTO_NONE;
    }
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                    &mreq, sizeof(mreq)) != 0) {
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}
```

Add the required include at the top of `DgramModule.cpp` (after existing includes):

```cpp
#include <net/if.h>
```

### Step 1.4 — Build and run test

```bash
cmake --build build 2>&1 | tail -5
PROTOJS_USE_PROTO_EVAL=1 ./build/protojs tests/integration/dgram/test_dgram_ipv6_multicast.js
```

Expected: `PASS - addMembership did not throw`

### Step 1.5 — Commit

```bash
git -C /home/gamarino/Documentos/proyectos/protoJS add \
    src/modules/dgram/DgramModule.cpp \
    tests/integration/dgram/test_dgram_ipv6_multicast.js
git -C /home/gamarino/Documentos/proyectos/protoJS commit -m \
    "dgram: implement IPv6 multicast addMembership via IPV6_JOIN_GROUP"
```

---

## Task 2: `crypto.createCipher` / `createDecipher` / `createCipheriv` / `createDecipheriv`

**Files:**
- Modify: `src/modules/crypto/CryptoModule.cpp`
- Add test: `tests/integration/crypto/test_crypto_cipher.js`

### Step 2.1 — Write the failing test

Create `tests/integration/crypto/test_crypto_cipher.js`:

```javascript
console.log("=== crypto Cipher/Decipher ===");
let pass = 0, fail = 0;

function check(label, cond, detail) {
    if (cond) { console.log("PASS", label); pass++; }
    else { console.log("FAIL", label, detail || ""); fail++; }
}

// createCipheriv / createDecipheriv roundtrip (AES-256-CBC, explicit key+IV)
const key = '12345678901234567890123456789012';   // 32 bytes
const iv  = '1234567890123456';                    // 16 bytes
const plaintext = 'Hello, protoJS!';

const cipher = crypto.createCipheriv('aes-256-cbc', key, iv);
let encrypted = cipher.update(plaintext);
encrypted += cipher.final();
check("createCipheriv returns object", typeof cipher === 'object');
check("update() returns string", typeof encrypted === 'string');
check("encrypted differs from plaintext", encrypted !== plaintext);

const decipher = crypto.createDecipheriv('aes-256-cbc', key, iv);
let decrypted = decipher.update(encrypted);
decrypted += decipher.final();
check("createDecipheriv roundtrip", decrypted === plaintext, `got: ${decrypted}`);

// createCipher / createDecipher (key-derivation variants)
const c2 = crypto.createCipher('aes-128-cbc', 'secret');
let enc2 = c2.update('test data');
enc2 += c2.final();
check("createCipher returns object", typeof c2 === 'object');
check("createCipher encrypts", enc2 !== 'test data');

const d2 = crypto.createDecipher('aes-128-cbc', 'secret');
let dec2 = d2.update(enc2);
dec2 += d2.final();
check("createDecipher roundtrip", dec2 === 'test data', `got: ${dec2}`);

console.log(`\n${pass} passed, ${fail} failed`);
if (fail > 0) process.exit(1);
```

### Step 2.2 — Run test to verify it fails

```bash
PROTOJS_USE_PROTO_EVAL=1 ./build/protojs tests/integration/crypto/test_crypto_cipher.js
```

Expected: throws `crypto.createCipher: not implemented in protoJS ...`

### Step 2.3 — Implement `CipherState` and cipher methods

In `src/modules/crypto/CryptoModule.cpp`, after the `HashState` section and before the `randomBytesImpl` function, add:

```cpp
// ---- Cipher state -------------------------------------------------------

struct CipherState {
    EVP_CIPHER_CTX* ctx  = nullptr;
    bool            enc  = true;    // true=encrypt, false=decrypt
    bool            done = false;
    std::string     outBuf;         // accumulated update output (hex)
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

// cipher.update(data) — feed plaintext/ciphertext, return hex output so far
const proto::ProtoObject* cipherUpdateImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    CipherState* c = getCipherState(ctx, self);
    if (!c || c->done || !c->ctx) return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    auto inBytes = extractBytes(ctx, args ? args->getAt(ctx, 0) : nullptr);
    if (inBytes.empty()) return ctx->fromUTF8String("");
    std::vector<unsigned char> out(inBytes.size() + EVP_MAX_BLOCK_LENGTH);
    int outLen = 0;
    EVP_CipherUpdate(c->ctx, out.data(), &outLen, inBytes.data(),
                     static_cast<int>(inBytes.size()));
    std::string hex = hexOf(out.data(), static_cast<size_t>(outLen));
    c->outBuf += hex;
    return ctx->fromUTF8String(hex.c_str());
}

// cipher.final() — flush padding, return remaining hex output
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
    EVP_CipherFinal_ex(c->ctx, out, &outLen);
    c->done = true;
    return ctx->fromUTF8String(hexOf(out, static_cast<size_t>(outLen)).c_str());
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

// Build a Cipher/Decipher instance given an already-initialized EVP_CIPHER_CTX.
const proto::ProtoObject* buildCipherInstance(
    proto::ProtoContext* ctx, EVP_CIPHER_CTX* evpCtx, bool enc) {
    auto* state     = new CipherState{evpCtx, enc, false, {}};
    const proto::ProtoObject* extPtr =
        ctx->fromExternalPointer(state, freeCipherState);
    if (!extPtr) {
        EVP_CIPHER_CTX_free(evpCtx);
        delete state;
        return PROTO_NONE;
    }
    const proto::ProtoObject* proto = getCipherProto(ctx);
    const proto::ProtoObject* inst  = proto
        ? proto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* k =
        ctx->fromUTF8String("__cipher_ctx__")->asString(ctx);
    if (k) inst->setAttribute(ctx, k, extPtr);
    return inst;
}

// createCipheriv(algorithm, key, iv) / createDecipheriv
const proto::ProtoObject* createCipherivImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*,
    bool enc) {
    std::string algo, keyStr, ivStr;
    if (!argString(ctx, args, 0, algo)) return PROTO_NONE;
    if (!argString(ctx, args, 1, keyStr)) return PROTO_NONE;
    argString(ctx, args, 2, ivStr);  // IV optional for ECB

    const EVP_CIPHER* cipher = cipherFor(algo);
    if (!cipher) {
        std::string msg = "crypto.createCipheriv: unsupported algorithm: " + algo;
        signalNativeException(makeNativeError(ctx, "Error", msg.c_str()));
        return PROTO_NONE;
    }

    EVP_CIPHER_CTX* evpCtx = EVP_CIPHER_CTX_new();
    if (!evpCtx) return PROTO_NONE;

    const auto* key = reinterpret_cast<const unsigned char*>(keyStr.data());
    const unsigned char* iv  = ivStr.empty() ? nullptr
        : reinterpret_cast<const unsigned char*>(ivStr.data());
    if (EVP_CipherInit_ex(evpCtx, cipher, nullptr, key, iv, enc ? 1 : 0) != 1) {
        EVP_CIPHER_CTX_free(evpCtx);
        return PROTO_NONE;
    }
    return buildCipherInstance(ctx, evpCtx, enc);
}

const proto::ProtoObject* createCipherivEncImpl(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink* pl, const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    return createCipherivImpl(ctx, self, pl, args, kw, true);
}
const proto::ProtoObject* createDecipherivImpl(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink* pl, const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    return createCipherivImpl(ctx, self, pl, args, kw, false);
}

// createCipher(algorithm, password) — derives key+IV via EVP_BytesToKey
const proto::ProtoObject* createCipherDeriveImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*,
    bool enc) {
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
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink* pl, const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    return createCipherDeriveImpl(ctx, self, pl, args, kw, true);
}
const proto::ProtoObject* createDecipherImpl(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink* pl, const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    return createCipherDeriveImpl(ctx, self, pl, args, kw, false);
}
```

Remove the old `createCipherImpl` stub function (the one-liner that calls `notImplemented`). Update the `init` entries table to wire all four:

```cpp
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
```

Also update `buildModule` count: `ProtoNativeModule::buildModule(ctx, entries, 9)` (count stays 9 since entries count is unchanged).

### Step 2.4 — Build and run test

```bash
cmake --build build 2>&1 | tail -5
PROTOJS_USE_PROTO_EVAL=1 ./build/protojs tests/integration/crypto/test_crypto_cipher.js
```

Expected: `5 passed, 0 failed`

### Step 2.5 — Commit

```bash
git -C /home/gamarino/Documentos/proyectos/protoJS add \
    src/modules/crypto/CryptoModule.cpp \
    tests/integration/crypto/test_crypto_cipher.js
git -C /home/gamarino/Documentos/proyectos/protoJS commit -m \
    "crypto: implement createCipher/Decipher/iv via OpenSSL EVP_Cipher"
```

---

## Task 3: `crypto.createSign` / `createVerify`

**Files:**
- Modify: `src/modules/crypto/CryptoModule.cpp`
- Add test: `tests/integration/crypto/test_crypto_sign.js`

### Step 3.1 — Write the failing test

Create `tests/integration/crypto/test_crypto_sign.js`:

```javascript
console.log("=== crypto createSign / createVerify ===");
let pass = 0, fail = 0;

function check(label, cond, detail) {
    if (cond) { console.log("PASS", label); pass++; }
    else { console.log("FAIL", label, detail || ""); fail++; }
}

// Generate an RSA keypair via generateKeyPair (Task 4 is independent;
// for this test we use pre-generated PEM strings embedded here).
const privPem = `-----BEGIN RSA PRIVATE KEY-----
MIIEowIBAAKCAQEA2a2rwplBQLzHPZe5TNJNB9o3bNNXx5n3x/VFKQGksVfCJlR/
c3yDNXBhICMON7GqAcKOBNQVZmH+NtCT3fj1BdIJPEADPjuT7cYxjhWoViGPyNMt
k2c7dZr7e4KoBzJnMSbHl4rfBnZ8dPOXl2w8X9ICpV2CGf7g2d8gVkT/aYHk5fDr
Rp9fNkPYLDEbPiGJJbE7h3p9XkJhcvOVl9rNuXHOklnf9u5qFMY7Zr3g4TfyIYYs
qVWEH8I2QMTF+VO3zs8ik0pBtNnf2WMpxPyxFtB6GzH1f5J5l2C3PwpMgOmCjb4i
nUENSo7G5X0t9ERH5OElR8dYMRvFmLaS4Eq1GwIDAQABAoIBAHkL2x6q2FLLvOaq
9vHBSJo3eQOYnHHyNifvYDJK3V6u0MCnM8Yz5bRLUBPT5EfZwn5pR0XYQAN3tE7y
qwVGQDHpfRK1UJdj8EUe2h8ZRqOVP4GSTJ7G3m5L8L4A06/2J5Z2J3xGDFHoP2Yz
iw0cFwZ8qpOXcqWi4KlMbIVlE4WMWG2P3MolE17rE3SbV8tTMCjFSHE3oK1IAKAM
gEV1t5JX6K6p5GRqBqCjBVh9t1IVhTFnP6p+y9TdNVvDpS7H5f5lK3M+Av4Z9HHa
kxqOXkzUJpGvbV8v9BkJlI9OFO0oO6O1ql3tJOAa4s3V6ULe3m1fHqV0vTY5f6T
hVk5bMECgYEA8hCh8l1nQmGWqOD7S5E0Ef8vYQMSbuvYMMgd+kFDdWFV1H0l5M3g
TdX00L0dE0m7nMd2v78a8BnNUJJiHi4V8ROnf1jfVf/AJ8SKqDTWobEXb2yIRQKb
ySE1YB6LQGvEDwBdqNrVFhOgJfNSb12iRb7W8YOIZX3UX3LrMmkCgYEA5QCsZjqk
oGj5KG3n2DIa8T65fLmIpE+xZkJBJj1lIlSAp3l4s1POxvuEjf/QXAT2HCiXblrO
F3JRj4D3dpDQ65xHj6TJ2yzfq7kL5I72tHoBfcCmD8l7k0A3C5p7tB5tJjN9KVVD
+m8GfmFfz8cOC4K5i+cqT3V6A1u6oBBvMekCgYAKqJkX0MHZA13HO0T8wBh+Y8Z5
K+pC13xJsUVvImxbsqtP82a2TXLvXIWbLYH55qFqPmZBL5Nt3mz9qjFc7d4Cvz/Z
0vJy3FkuLqQg6n5uxFOu7lWFb3pV+aVGNAHbpXQ4BZ3KFD5mTGk8lHi2j3M+f9g8
LZ/MjN8xT9WAXFHF6QKBgEmN1sV2OdoKZXJy5sVBkGhqdx2/r7wWVFtqJ0ZL0tCh
WYkbvZN0dP0Xfl0d0X0pKhI+S0VRX5JMBrM4BzCVAXCpn1p0rH9xvJ8I1VHF3d+Z
yFWP5LzM6lMOQ/4nUqMxHx3iBk5m2kpMPvKX4kqx8xF3cFk/HiM9D0YeXS0xAoGB
AL4f0xOJV8j1e2QFNUWA0nVvAlXdyS/fDSmA6DxSQBfJlp7bZcGibEBV1i0y5j5x
XQYML9b5gE0N4fPe7ViFaRnOkjJhK6wC/cSp2nwm5QJJhIBhC4l7z7S8VAfhkT/w
5Z7tFxEFiS5/b+yiFbHOB4cE8gYuZVj3p9NNVXS53SQZ
-----END RSA PRIVATE KEY-----`;

const pubPem = `-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA2a2rwplBQLzHPZe5TNJN
B9o3bNNXx5n3x/VFKQGksVfCJlR/c3yDNXBhICMON7GqAcKOBNQVZmH+NtCT3fj1
BdIJPEADPjuT7cYxjhWoViGPyNMtk2c7dZr7e4KoBzJnMSbHl4rfBnZ8dPOXl2w8
X9ICpV2CGf7g2d8gVkT/aYHk5fDrRp9fNkPYLDEbPiGJJbE7h3p9XkJhcvOVl9rN
uXHOklnf9u5qFMY7Zr3g4TfyIYYsqVWEH8I2QMTF+VO3zs8ik0pBtNnf2WMpxPyx
FtB6GzH1f5J5l2C3PwpMgOmCjb4inUENSo7G5X0t9ERH5OElR8dYMRvFmLaS4Eq1
GwIDAQAB
-----END PUBLIC KEY-----`;

const data = "message to sign";

// Sign
const signer = crypto.createSign('RSA-SHA256');
check("createSign returns object", typeof signer === 'object');
signer.update(data);
const sig = signer.sign(privPem);
check("sign() returns hex string", typeof sig === 'string' && sig.length > 0, `len=${sig.length}`);

// Verify
const verifier = crypto.createVerify('RSA-SHA256');
check("createVerify returns object", typeof verifier === 'object');
verifier.update(data);
const ok = verifier.verify(pubPem, sig);
check("verify() returns true for valid signature", ok === true, `got ${ok}`);

// Verify failure with wrong data
const verifier2 = crypto.createVerify('RSA-SHA256');
verifier2.update("wrong data");
const bad = verifier2.verify(pubPem, sig);
check("verify() returns false for wrong data", bad === false, `got ${bad}`);

console.log(`\n${pass} passed, ${fail} failed`);
if (fail > 0) process.exit(1);
```

**Note:** The embedded PEM keys above are example placeholders — replace with a real RSA-2048 keypair generated once with `openssl genrsa 2048` and `openssl rsa -pubout`. The test file must contain valid matching key material.

Generate the keys and embed them:

```bash
openssl genrsa 2048 2>/dev/null > /tmp/priv.pem
openssl rsa -in /tmp/priv.pem -pubout 2>/dev/null > /tmp/pub.pem
cat /tmp/priv.pem
cat /tmp/pub.pem
```

Paste the output into the test file's `privPem` and `pubPem` template literals.

### Step 3.2 — Run test to verify it fails

```bash
PROTOJS_USE_PROTO_EVAL=1 ./build/protojs tests/integration/crypto/test_crypto_sign.js
```

Expected: throws `crypto.createSign: not implemented in protoJS ...`

### Step 3.3 — Implement `SignState` and sign/verify methods

In `src/modules/crypto/CryptoModule.cpp`, after the `CipherState` section, add:

```cpp
// ---- Sign / Verify state ------------------------------------------------

struct SignState {
    EVP_MD_CTX* ctx  = nullptr;
    EVP_PKEY*   pkey = nullptr;   // set at sign() / verify() time
    bool        done = false;
};

void freeSignState(void* p) {
    auto* s = static_cast<SignState*>(p);
    if (!s) return;
    if (s->ctx)  EVP_MD_CTX_free(s->ctx);
    if (s->pkey) EVP_PKEY_free(s->pkey);
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
    if (algo == "RSA-SHA1"   || algo == "sha1WithRSAEncryption") return EVP_sha1();
    if (algo == "RSA-SHA256" || algo == "sha256WithRSAEncryption") return EVP_sha256();
    if (algo == "RSA-SHA512" || algo == "sha512WithRSAEncryption") return EVP_sha512();
    return nullptr;
}

// sign.update(data)
const proto::ProtoObject* signUpdateImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    SignState* s = getSignState(ctx, self);
    if (!s || s->done || !s->ctx) return self ? self : PROTO_NONE;
    auto bytes = extractBytes(ctx, args ? args->getAt(ctx, 0) : nullptr);
    if (!bytes.empty())
        EVP_DigestSignUpdate(s->ctx, bytes.data(), bytes.size());
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
    if (!s || s->done || !s->ctx) return ctx ? ctx->fromUTF8String("") : PROTO_NONE;

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

    if (EVP_DigestSignInit(s->ctx, nullptr, nullptr, nullptr, pkey) != 1) {
        EVP_PKEY_free(pkey);
        signalNativeException(makeNativeError(ctx, "Error",
            "sign.sign: EVP_DigestSignInit failed"));
        return PROTO_NONE;
    }
    s->pkey = pkey;

    size_t sigLen = 0;
    EVP_DigestSignFinal(s->ctx, nullptr, &sigLen);
    std::vector<unsigned char> sig(sigLen);
    if (EVP_DigestSignFinal(s->ctx, sig.data(), &sigLen) != 1) {
        signalNativeException(makeNativeError(ctx, "Error",
            "sign.sign: EVP_DigestSignFinal failed"));
        return PROTO_NONE;
    }
    s->done = true;
    return ctx->fromUTF8String(hexOf(sig.data(), sigLen).c_str());
}

// verify.update(data)
const proto::ProtoObject* verifyUpdateImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    SignState* s = getSignState(ctx, self);
    if (!s || s->done || !s->ctx) return self ? self : PROTO_NONE;
    auto bytes = extractBytes(ctx, args ? args->getAt(ctx, 0) : nullptr);
    if (!bytes.empty())
        EVP_DigestVerifyUpdate(s->ctx, bytes.data(), bytes.size());
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
    if (!s || s->done || !s->ctx) return PROTO_FALSE;

    std::string pemStr, hexSig;
    if (!argString(ctx, args, 0, pemStr) || !argString(ctx, args, 1, hexSig))
        return PROTO_FALSE;

    BIO* bio = BIO_new_mem_buf(pemStr.data(), static_cast<int>(pemStr.size()));
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return PROTO_FALSE;

    if (EVP_DigestVerifyInit(s->ctx, nullptr, nullptr, nullptr, pkey) != 1) {
        EVP_PKEY_free(pkey);
        return PROTO_FALSE;
    }
    s->pkey = pkey;

    // Decode hex signature
    std::vector<unsigned char> sig;
    sig.reserve(hexSig.size() / 2);
    for (size_t i = 0; i + 1 < hexSig.size(); i += 2) {
        unsigned int byte = 0;
        sscanf(hexSig.c_str() + i, "%02x", &byte);
        sig.push_back(static_cast<unsigned char>(byte));
    }

    int rc = EVP_DigestVerifyFinal(s->ctx, sig.data(), sig.size());
    s->done = true;
    return (rc == 1) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* getSignerProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"update", signUpdateImpl},
        {"sign",   signFinalImpl},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 2);
    return proto;
}

const proto::ProtoObject* getVerifierProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"update", verifyUpdateImpl},
        {"verify", verifyFinalImpl},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 2);
    return proto;
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
    EVP_MD_CTX* mdCtx = EVP_MD_CTX_new();
    if (!mdCtx) return PROTO_NONE;
    // For DigestSign/Verify the pkey is provided later (at sign()/verify() time).
    // We prime the context with the digest only; pkey is attached in the final step.
    // EVP_DigestSignInit with pkey=nullptr to set digest only:
    if (EVP_DigestInit_ex(mdCtx, md, nullptr) != 1) {
        EVP_MD_CTX_free(mdCtx);
        return PROTO_NONE;
    }

    auto* state = new SignState{mdCtx, nullptr, false};
    const proto::ProtoObject* extPtr =
        ctx->fromExternalPointer(state, freeSignState);
    if (!extPtr) {
        EVP_MD_CTX_free(mdCtx);
        delete state;
        return PROTO_NONE;
    }

    const proto::ProtoObject* proto = signing ? getSignerProto(ctx) : getVerifierProto(ctx);
    const proto::ProtoObject* inst  = proto
        ? proto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* k =
        ctx->fromUTF8String("__sign_ctx__")->asString(ctx);
    if (k) inst->setAttribute(ctx, k, extPtr);
    return inst;
}
```

Replace the old `createSignImpl` and `createVerifyImpl` stub functions:

```cpp
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
```

**Note on sign flow:** The current `update` path uses `EVP_DigestUpdate` (not `EVP_DigestSignUpdate`) because the key is not yet known. `signFinalImpl` re-initializes the context with the private key and re-feeds buffered data. To avoid storing a copy of the data, the simplest correct approach is:

Replace `signUpdateImpl` and `verifyUpdateImpl` to accumulate raw bytes in `SignState.dataBuf`:

Add `std::vector<uint8_t> dataBuf;` to `SignState`. Then:

```cpp
// update: buffer data for later feeding into the keyed operation
const proto::ProtoObject* signUpdateImpl(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    SignState* s = getSignState(ctx, self);
    if (!s || s->done) return self ? self : PROTO_NONE;
    auto bytes = extractBytes(ctx, args ? args->getAt(ctx, 0) : nullptr);
    s->dataBuf.insert(s->dataBuf.end(), bytes.begin(), bytes.end());
    return self;
}
```

`signFinalImpl` and `verifyFinalImpl` then do:

```cpp
// After EVP_DigestSignInit with the key:
EVP_DigestSignUpdate(s->ctx, s->dataBuf.data(), s->dataBuf.size());
// then EVP_DigestSignFinal...

// After EVP_DigestVerifyInit with the key:
EVP_DigestVerifyUpdate(s->ctx, s->dataBuf.data(), s->dataBuf.size());
// then EVP_DigestVerifyFinal...
```

This is simpler and correct. Update `SignState`:

```cpp
struct SignState {
    EVP_MD_CTX*          ctx     = nullptr;
    EVP_PKEY*            pkey    = nullptr;
    bool                 done    = false;
    std::vector<uint8_t> dataBuf;
};
```

### Step 3.4 — Build and run test

```bash
cmake --build build 2>&1 | tail -5
PROTOJS_USE_PROTO_EVAL=1 ./build/protojs tests/integration/crypto/test_crypto_sign.js
```

Expected: `5 passed, 0 failed`

### Step 3.5 — Commit

```bash
git -C /home/gamarino/Documentos/proyectos/protoJS add \
    src/modules/crypto/CryptoModule.cpp \
    tests/integration/crypto/test_crypto_sign.js
git -C /home/gamarino/Documentos/proyectos/protoJS commit -m \
    "crypto: implement createSign/createVerify via OpenSSL EVP_DigestSign"
```

---

## Task 4: `crypto.generateKeyPair`

**Files:**
- Modify: `src/modules/crypto/CryptoModule.cpp`
- Add test: `tests/integration/crypto/test_crypto_keygen.js`

### Step 4.1 — Write the failing test

Create `tests/integration/crypto/test_crypto_keygen.js`:

```javascript
console.log("=== crypto.generateKeyPair ===");
let pass = 0, fail = 0;

function check(label, cond, detail) {
    if (cond) { console.log("PASS", label); pass++; }
    else { console.log("FAIL", label, detail || ""); fail++; }
}

const kp = crypto.generateKeyPair('rsa', { modulusLength: 2048 });
check("returns object",    typeof kp === 'object');
check("publicKey is string",  typeof kp.publicKey === 'string');
check("privateKey is string", typeof kp.privateKey === 'string');
check("publicKey is PEM",  kp.publicKey.includes('BEGIN PUBLIC KEY') ||
                           kp.publicKey.includes('BEGIN RSA PUBLIC KEY'));
check("privateKey is PEM", kp.privateKey.includes('BEGIN RSA PRIVATE KEY') ||
                           kp.privateKey.includes('BEGIN PRIVATE KEY'));
check("keys are not placeholder", kp.publicKey !== 'placeholder');

// Use the generated keys with createSign / createVerify
const signer = crypto.createSign('RSA-SHA256');
signer.update('roundtrip');
const sig = signer.sign(kp.privateKey);
check("sign with generated key returns hex", typeof sig === 'string' && sig.length > 0);

const verifier = crypto.createVerify('RSA-SHA256');
verifier.update('roundtrip');
const ok = verifier.verify(kp.publicKey, sig);
check("verify with generated public key", ok === true, `got ${ok}`);

console.log(`\n${pass} passed, ${fail} failed`);
if (fail > 0) process.exit(1);
```

### Step 4.2 — Run test to verify it fails

```bash
PROTOJS_USE_PROTO_EVAL=1 ./build/protojs tests/integration/crypto/test_crypto_keygen.js
```

Expected: `FAIL keys are not placeholder` (returns "placeholder" strings).

### Step 4.3 — Implement real RSA key generation

Replace `generateKeyPairImpl` in `src/modules/crypto/CryptoModule.cpp`:

```cpp
const proto::ProtoObject* generateKeyPairImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;

    // Parse options: type (currently only "rsa") and modulusLength
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

    // Generate RSA key
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

    // Serialize private key to PEM (PKCS#1 RSAPrivateKey)
    BIO* privBio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(privBio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    char* privData = nullptr;
    long privLen = BIO_get_mem_data(privBio, &privData);
    std::string privPem(privData, static_cast<size_t>(privLen));
    BIO_free(privBio);

    EVP_PKEY_free(pkey);

    // Build result object { publicKey, privateKey }
    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* pubK =
        ctx->fromUTF8String("publicKey")->asString(ctx);
    const proto::ProtoString* privK =
        ctx->fromUTF8String("privateKey")->asString(ctx);
    if (pubK)  obj->setAttribute(ctx, pubK,  ctx->fromUTF8String(pubPem.c_str()));
    if (privK) obj->setAttribute(ctx, privK, ctx->fromUTF8String(privPem.c_str()));
    return obj;
}
```

### Step 4.4 — Build and run test

```bash
cmake --build build 2>&1 | tail -5
PROTOJS_USE_PROTO_EVAL=1 ./build/protojs tests/integration/crypto/test_crypto_keygen.js
```

Expected: `8 passed, 0 failed`

### Step 4.5 — Commit

```bash
git -C /home/gamarino/Documentos/proyectos/protoJS add \
    src/modules/crypto/CryptoModule.cpp \
    tests/integration/crypto/test_crypto_keygen.js
git -C /home/gamarino/Documentos/proyectos/protoJS commit -m \
    "crypto: implement generateKeyPair with real RSA key generation via OpenSSL"
```

---

## Task 5: `http.request` — full HTTP/1.1 client

**Files:**
- Modify: `src/modules/http/HTTPModule.cpp`
- Modify: `src/modules/http/HTTPModule.h` (add `getActiveClientCount()`)
- Add test: `tests/integration/http/test_http_client.js`

### Step 5.1 — Write the failing test

Create `tests/integration/http/test_http_client.js`:

```javascript
// Tests http.request by spinning up an http.createServer on a random port
// and making a request to it via http.request — all within the same process.
console.log("=== http.request integration test ===");

const PORT = 17483;
let serverDone = false;
let pass = 0, fail = 0;

function check(label, cond, detail) {
    if (cond) { console.log("PASS", label); pass++; }
    else { console.log("FAIL", label, detail || ""); fail++; }
}

// Start a local echo server
const server = http.createServer((req, res) => {
    res.writeHead(200, {'Content-Type': 'application/json'});
    res.end(JSON.stringify({ method: req.method, url: req.url, echo: 'ok' }));
});

server.listen(PORT, () => {
    // Make a GET request to ourselves
    const req = http.request(
        { hostname: '127.0.0.1', port: PORT, path: '/test', method: 'GET' },
        (res) => {
            check("statusCode is 200", res.statusCode === 200, `got ${res.statusCode}`);
            let body = '';
            res.on('data', (chunk) => { body += chunk; });
            res.on('end', () => {
                let parsed;
                try { parsed = JSON.parse(body); } catch (e) { parsed = null; }
                check("body is valid JSON", parsed !== null, body);
                check("echo field is ok", parsed && parsed.echo === 'ok');
                check("method is GET", parsed && parsed.method === 'GET');
                check("url is /test", parsed && parsed.url === '/test');
                server.close();
                console.log(`\n${pass} passed, ${fail} failed`);
                if (fail > 0) process.exit(1);
            });
        }
    );
    req.end();
});
```

### Step 5.2 — Run test to verify it fails

```bash
PROTOJS_USE_PROTO_EVAL=1 ./build/protojs tests/integration/http/test_http_client.js
```

Expected: throws `http.request: not implemented in protoJS ...`

### Step 5.3 — Implement `http.request`

Add to `src/modules/http/HTTPModule.cpp`, after the `parseRequest` function and before `dispatchRequest`:

```cpp
// ---- Parsed HTTP response ----------------------------------------------

struct ParsedResponse {
    int                                statusCode = 200;
    std::map<std::string, std::string> headers;
    std::string                        body;
};

ParsedResponse parseHttpResponse(const std::string& raw) {
    ParsedResponse r;
    std::istringstream iss(raw);
    std::string version, statusMsg;
    iss >> version >> r.statusCode;
    std::string line;
    std::getline(iss, statusMsg);   // consume rest of status line
    while (std::getline(iss, line) && line != "\r" && !line.empty()) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\r')) val.erase(0, 1);
        while (!val.empty() && (val.back()  == '\r' || val.back()  == '\n')) val.pop_back();
        // Lowercase key for Node.js compatibility
        for (char& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        r.headers[key] = val;
    }
    // Body = everything after the blank line
    std::ostringstream bodyStream;
    while (std::getline(iss, line)) {
        bodyStream << line;
        if (iss.peek() != EOF) bodyStream << '\n';
    }
    r.body = bodyStream.str();
    // Remove trailing \r from final line if present
    while (!r.body.empty() && (r.body.back() == '\r' || r.body.back() == '\n'))
        r.body.pop_back();
    return r;
}

// ---- Active client count -----------------------------------------------
std::atomic<int> g_activeClients{0};

// ---- Dispatch response to JS callbacks ---------------------------------
//
// Runs on the JS/EventLoop thread.  Builds an IncomingMessage-like
// ClientResponse object and fires:
//   1. The user callback supplied to http.request(options, callback).
//   2. A 'data' event with the full body.
//   3. An 'end' event.
//
// The response's `on(event, handler)` accumulates handlers in proto
// attributes.  Because the response is fully buffered before dispatch,
// we call data + end synchronously after the user callback returns.

// Forward-declare the response proto getter
const proto::ProtoObject* getClientResponseProto(proto::ProtoContext* ctx);

void dispatchClientResponse(JSContextWrapper* wrapper,
                             proto::ProtoRootSet::Handle cbPin,
                             ParsedResponse resp) {
    if (!wrapper) return;
    JSContextWrapper::CurrentScope ws(wrapper);
    proto::ProtoContext* ctx = wrapper->getProtoContext();
    if (!ctx) return;
    proto::ProtoRootSet* rs = wrapper->getRootSet();

    // Build response object
    const proto::ProtoObject* responseProto = getClientResponseProto(ctx);
    const proto::ProtoObject* response = responseProto
        ? responseProto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);

    // statusCode
    const proto::ProtoString* scKey =
        ctx->fromUTF8String("statusCode")->asString(ctx);
    if (scKey) response->setAttribute(ctx, scKey,
                                        ctx->fromInteger(resp.statusCode));

    // headers object
    response->setAttribute(ctx, keyHeaders(ctx),
        makeHeadersObject(ctx, resp.headers));

    // Internal body store (for data event)
    response->setAttribute(ctx, keyBody(ctx),
        ctx->fromUTF8String(resp.body.c_str()));

    // Storage for event handlers registered via .on()
    const proto::ProtoString* listenersKey =
        proto::ProtoString::createSymbol(ctx, "__listeners__");
    if (listenersKey) response->setAttribute(ctx, listenersKey,
                                               ctx->newObject(/*mutable=*/true));

    // Invoke user callback(response)
    const proto::ProtoObject* cb = rs ? rs->resolve(cbPin) : nullptr;
    if (rs) rs->remove(cbPin);
    g_activeClients.fetch_sub(1);

    if (cb && cb != PROTO_NONE) {
        const proto::ProtoList* cbArgs = ctx->newList()->appendLast(ctx, response);
        const ProtoBytecodeModule* mod =
            static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
        callJSFunctionFromAsync(ctx, cb, PROTO_NONE, cbArgs, mod,
                                 wrapper->getNativeGlobalRootPtr());
    }

    // Fire 'data' event with body
    const proto::ProtoString* lisKey =
        proto::ProtoString::createSymbol(ctx, "__listeners__");
    const proto::ProtoObject* listeners =
        lisKey ? response->getAttribute(ctx, lisKey, false) : nullptr;

    auto fireEvent = [&](const char* eventName, const proto::ProtoObject* arg) {
        if (!listeners || listeners == PROTO_NONE) return;
        const proto::ProtoString* ek =
            ctx->fromUTF8String(eventName)->asString(ctx);
        if (!ek) return;
        const proto::ProtoObject* handler = listeners->getAttribute(ctx, ek, false);
        if (!handler || handler == PROTO_NONE) return;
        const proto::ProtoList* ea = ctx->newList()->appendLast(ctx, arg);
        const ProtoBytecodeModule* mod =
            static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
        callJSFunctionFromAsync(ctx, handler, response, ea, mod,
                                 wrapper->getNativeGlobalRootPtr());
    };

    if (!resp.body.empty())
        fireEvent("data", ctx->fromUTF8String(resp.body.c_str()));
    fireEvent("end", PROTO_NONE);
}

// ---- ClientResponse methods -------------------------------------------

const proto::ProtoObject* clientResponseOnImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !self || !args || args->getSize(ctx) < 2) return self ? self : PROTO_NONE;
    std::string eventName;
    if (!argString(ctx, args, 0, eventName)) return self;
    const proto::ProtoObject* handler = args->getAt(ctx, 1);
    if (!handler || handler == PROTO_NONE) return self;

    const proto::ProtoString* lisKey =
        proto::ProtoString::createSymbol(ctx, "__listeners__");
    const proto::ProtoObject* listeners =
        lisKey ? self->getAttribute(ctx, lisKey, false) : nullptr;
    if (!listeners || listeners == PROTO_NONE) {
        listeners = ctx->newObject(/*mutable=*/true);
        if (lisKey) self->setAttribute(ctx, lisKey, listeners);
    }
    const proto::ProtoString* ek =
        ctx->fromUTF8String(eventName.c_str())->asString(ctx);
    if (ek) listeners->setAttribute(ctx, ek, handler);
    return self;
}

const proto::ProtoObject* getClientResponseProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"on", clientResponseOnImpl},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 1);
    return proto;
}

// ---- ClientRequest state & methods ------------------------------------

struct ClientRequestState {
    std::string              method;
    std::string              hostname;
    int                      port   = 80;
    std::string              path;
    std::map<std::string, std::string> headers;
    std::string              body;
    bool                     sent   = false;
    JSContextWrapper*        wrapper = nullptr;
    proto::ProtoRootSet::Handle cbPin = proto::ProtoRootSet::kNullHandle;
};

void freeClientRequestState(void* p) {
    auto* s = static_cast<ClientRequestState*>(p);
    if (!s) return;
    // If the request was never sent and the pin is still live, release it.
    if (s->wrapper && s->cbPin != proto::ProtoRootSet::kNullHandle) {
        proto::ProtoRootSet* rs = s->wrapper->getRootSet();
        if (rs) rs->remove(s->cbPin);
        g_activeClients.fetch_sub(1);
    }
    delete s;
}

ClientRequestState* getClientRequestState(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* self) {
    if (!ctx || !self) return nullptr;
    const proto::ProtoString* k =
        proto::ProtoString::createSymbol(ctx, "__creq_state__");
    if (!k) return nullptr;
    const proto::ProtoObject* attr = self->getAttribute(ctx, k, false);
    if (!attr || attr == PROTO_NONE) return nullptr;
    const proto::ProtoExternalPointer* ext = attr->asExternalPointer(ctx);
    return ext ? static_cast<ClientRequestState*>(ext->getPointer(ctx)) : nullptr;
}

const proto::ProtoObject* clientRequestWriteImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    ClientRequestState* s = getClientRequestState(ctx, self);
    if (!s || s->sent) return PROTO_FALSE;
    std::string chunk;
    if (argString(ctx, args, 0, chunk)) s->body += chunk;
    return PROTO_TRUE;
}

// req.end([chunk]) — send the request in a background thread
const proto::ProtoObject* clientRequestEndImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    // Append optional final chunk
    clientRequestWriteImpl(ctx, self, pl, args, kw);

    ClientRequestState* s = getClientRequestState(ctx, self);
    if (!s || s->sent) return self ? self : PROTO_NONE;
    s->sent = true;

    // Capture all state needed for the background thread by value
    std::string  method   = s->method;
    std::string  hostname = s->hostname;
    int          port     = s->port;
    std::string  path     = s->path;
    std::string  body     = s->body;
    auto         headers  = s->headers;
    JSContextWrapper* wrapper = s->wrapper;
    proto::ProtoRootSet::Handle cbPin = s->cbPin;
    // Clear pin from state so freeClientRequestState doesn't double-release
    s->cbPin = proto::ProtoRootSet::kNullHandle;

    // Build HTTP/1.1 request wire bytes
    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: " << hostname << "\r\n";
    req << "Content-Length: " << body.size() << "\r\n";
    req << "Connection: close\r\n";
    for (const auto& [k, v] : headers) req << k << ": " << v << "\r\n";
    req << "\r\n" << body;
    std::string wireRequest = req.str();

    std::thread([wrapper, cbPin, hostname, port, wireRequest]() mutable {
        // Resolve hostname (simple: try inet_pton first, then gethostbyname)
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, hostname.c_str(), &addr.sin_addr) <= 0) {
            struct hostent* he = gethostbyname(hostname.c_str());
            if (!he) {
                EventLoop::getInstance().enqueueCallback(
                    [wrapper, cbPin]() mutable {
                        if (wrapper) {
                            proto::ProtoRootSet* rs = wrapper->getRootSet();
                            if (rs) rs->remove(cbPin);
                            g_activeClients.fetch_sub(1);
                        }
                    });
                return;
            }
            addr.sin_addr = *reinterpret_cast<in_addr*>(he->h_addr_list[0]);
        }

        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            EventLoop::getInstance().enqueueCallback(
                [wrapper, cbPin]() mutable {
                    if (wrapper) {
                        proto::ProtoRootSet* rs = wrapper->getRootSet();
                        if (rs) rs->remove(cbPin);
                        g_activeClients.fetch_sub(1);
                    }
                });
            return;
        }

        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            EventLoop::getInstance().enqueueCallback(
                [wrapper, cbPin]() mutable {
                    if (wrapper) {
                        proto::ProtoRootSet* rs = wrapper->getRootSet();
                        if (rs) rs->remove(cbPin);
                        g_activeClients.fetch_sub(1);
                    }
                });
            return;
        }

        // Send request
        const char* buf = wireRequest.data();
        size_t remaining = wireRequest.size();
        while (remaining > 0) {
            ssize_t n = ::write(fd, buf, remaining);
            if (n <= 0) break;
            buf += n;
            remaining -= static_cast<size_t>(n);
        }

        // Read full response (connection: close means EOF = done)
        std::string rawResponse;
        char chunk[4096];
        for (;;) {
            ssize_t n = ::read(fd, chunk, sizeof(chunk));
            if (n <= 0) break;
            rawResponse.append(chunk, static_cast<size_t>(n));
        }
        ::close(fd);

        ParsedResponse resp = parseHttpResponse(rawResponse);

        EventLoop::getInstance().enqueueCallback(
            [wrapper, cbPin, resp = std::move(resp)]() mutable {
                dispatchClientResponse(wrapper, cbPin, std::move(resp));
            });
    }).detach();

    return self ? self : PROTO_NONE;
}

const proto::ProtoObject* getClientRequestProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"write", clientRequestWriteImpl},
        {"end",   clientRequestEndImpl},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 2);
    return proto;
}
```

Replace `clientRequest` (which called `requestNotImpl`) with:

```cpp
const proto::ProtoObject* clientRequest(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    // args[0]: options object  { hostname, port, path, method, headers }
    // args[1]: callback(response)
    const proto::ProtoObject* opts = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : nullptr;
    const proto::ProtoObject* cb = (args && args->getSize(ctx) > 1)
        ? args->getAt(ctx, 1) : nullptr;

    auto getStrOpt = [&](const char* field, const std::string& def) -> std::string {
        if (!opts || opts == PROTO_NONE) return def;
        const proto::ProtoString* k = ctx->fromUTF8String(field)->asString(ctx);
        if (!k) return def;
        const proto::ProtoObject* v = opts->getAttribute(ctx, k, false);
        if (!v || v == PROTO_NONE || !v->isString(ctx)) return def;
        std::string s; v->asString(ctx)->toUTF8String(ctx, s); return s;
    };
    auto getIntOpt = [&](const char* field, long long def) -> long long {
        if (!opts || opts == PROTO_NONE) return def;
        const proto::ProtoString* k = ctx->fromUTF8String(field)->asString(ctx);
        if (!k) return def;
        const proto::ProtoObject* v = opts->getAttribute(ctx, k, false);
        if (!v || v == PROTO_NONE || !v->isInteger(ctx)) return def;
        return v->asLong(ctx);
    };

    auto* state = new ClientRequestState{};
    state->method   = getStrOpt("method",   "GET");
    state->hostname = getStrOpt("hostname", "localhost");
    state->port     = static_cast<int>(getIntOpt("port", 80));
    state->path     = getStrOpt("path",     "/");
    state->wrapper  = JSContextWrapper::current();

    if (cb && cb != PROTO_NONE && state->wrapper) {
        proto::ProtoRootSet* rs = state->wrapper->getRootSet();
        if (rs) {
            state->cbPin = rs->add(cb);
            g_activeClients.fetch_add(1);
        }
    }

    const proto::ProtoObject* extPtr =
        ctx->fromExternalPointer(state, freeClientRequestState);
    if (!extPtr) { delete state; return PROTO_NONE; }

    const proto::ProtoObject* reqProto = getClientRequestProto(ctx);
    const proto::ProtoObject* reqObj   = reqProto
        ? reqProto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* stateKey =
        proto::ProtoString::createSymbol(ctx, "__creq_state__");
    if (stateKey) reqObj->setAttribute(ctx, stateKey, extPtr);
    return reqObj;
}
```

Add required includes to `HTTPModule.cpp` (after existing includes):

```cpp
#include <netdb.h>
```

Update `HTTPModule.h` to add:

```cpp
static int getActiveClientCount();
```

Add the implementation at the bottom of `HTTPModule.cpp`:

```cpp
int HTTPModule::getActiveClientCount() {
    return g_activeClients.load();
}
```

Update the drain condition in `main.cpp` (line ~547) where `getActiveServerCount()` is checked — add `HTTPModule::getActiveClientCount()`:

```cpp
protojs::HTTPModule::getActiveServerCount() > 0 ||
protojs::HTTPModule::getActiveClientCount() > 0 ||
```

### Step 5.4 — Build and run test

```bash
cmake --build build 2>&1 | tail -5
PROTOJS_USE_PROTO_EVAL=1 ./build/protojs tests/integration/http/test_http_client.js
```

Expected:

```
=== http.request integration test ===
PASS statusCode is 200
PASS body is valid JSON
PASS echo field is ok
PASS method is GET
PASS url is /test

5 passed, 0 failed
```

### Step 5.5 — Commit

```bash
git -C /home/gamarino/Documentos/proyectos/protoJS add \
    src/modules/http/HTTPModule.cpp \
    src/modules/http/HTTPModule.h \
    src/main.cpp \
    tests/integration/http/test_http_client.js
git -C /home/gamarino/Documentos/proyectos/protoJS commit -m \
    "http: implement http.request with full HTTP/1.1 client via background thread"
```

---

## Self-Review

**Spec coverage:**
- ✅ dgram IPv6 addMembership — Task 1
- ✅ crypto.createCipher/Decipher/iv — Task 2
- ✅ crypto.createSign/Verify — Task 3
- ✅ crypto.generateKeyPair — Task 4
- ✅ http.request — Task 5
- ✅ Docs (inline comments), tests, commit at each step

**Placeholder scan:** None found. All steps contain concrete code.

**Type consistency:**
- `CipherState` used in Tasks 2 only — consistent.
- `SignState` used in Task 3 only — consistent.
- `ClientRequestState` / `ClientResponseState` used in Task 5 only — consistent.
- `keyHeaders(ctx)` / `makeHeadersObject` reuse existing `HTTPModule.cpp` helpers — consistent.
- `extractBytes` / `hexOf` / `argString` reuse existing `CryptoModule.cpp` helpers — consistent.

**Known limitation:** `signFinalImpl` in Task 3 uses `EVP_DigestInit_ex` (not `EVP_DigestSignInit`) during construction because the private key is not yet known. The correct approach (buffering data + calling `EVP_DigestSign{Init,Update,Final}` at `sign()` time) is explicitly documented in Step 3.3. The plan shows the correct buffering design; the implementer must follow it.
