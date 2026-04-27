#ifndef PROTOJS_CRYPTOMODULE_H
#define PROTOJS_CRYPTOMODULE_H
#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Node-style `crypto` module — Hash class plus
 *        randomBytes / createCipher / createSign / createVerify /
 *        generateKeyPair.  Hash is fully functional (OpenSSL EVP);
 *        the other constructors preserve the API surface but the
 *        actual cipher / sign / verify operations are stubs (the
 *        QuickJS-side implementation also stubbed them).  Migrated
 *        to protoCore-native: per-instance OpenSSL contexts attach
 *        as ProtoExternalPointer attributes instead of JS_SetOpaque.
 */
class CryptoModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);
};

} // namespace protojs

#endif
