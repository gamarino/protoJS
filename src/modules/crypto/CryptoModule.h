#ifndef PROTOJS_CRYPTOMODULE_H
#define PROTOJS_CRYPTOMODULE_H
#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Node-style `crypto` module — Hash, Cipher/Decipher, Sign/Verify,
 *        randomBytes, and generateKeyPair.  All operations are backed by
 *        OpenSSL EVP.  Per-instance OpenSSL contexts (EVP_CIPHER_CTX,
 *        EVP_MD_CTX) attach as ProtoExternalPointer attributes with GC
 *        finalizers — protoCore-native, no JS_SetOpaque.
 *
 *        Cipher: createCipher/createDecipher derive key+IV via
 *        EVP_BytesToKey (Node.js legacy form); createCipheriv /
 *        createDecipheriv take explicit key+IV.  Encrypt path produces
 *        hex output; decrypt path consumes hex input, so the chain
 *        cipher.update + cipher.final pipes naturally into
 *        decipher.update + decipher.final.
 *
 *        Sign/Verify: data is buffered until sign(privKeyPem) /
 *        verify(pubKeyPem, hexSig) is called, at which point a fresh
 *        EVP_MD_CTX runs the full Init/Update/Final sequence using the
 *        algorithm captured at construction time.
 *
 *        generateKeyPair: RSA via EVP_PKEY_keygen, serialized as PKCS#8
 *        SubjectPublicKeyInfo / PrivateKeyInfo PEM.
 */
class CryptoModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);
};

} // namespace protojs

#endif
