#include "Signature.h"

#include <QString>

// libcrypto through a few declarations of our own (as in ShareCrypto.cpp), so building needs no OpenSSL headers on Linux.
// These calls exist since OpenSSL 1.1.1; 1087 is NID_ED25519.
struct EvpPkey;
struct EvpMdCtx;
extern "C" {
EvpPkey *EVP_PKEY_new_raw_public_key(int type, void *engine, const unsigned char *pub, size_t len);
void EVP_PKEY_free(EvpPkey *);
EvpMdCtx *EVP_MD_CTX_new(void);
void EVP_MD_CTX_free(EvpMdCtx *);
int EVP_DigestVerifyInit(EvpMdCtx *, void **pctx, const void *md, void *engine, EvpPkey *);
int EVP_DigestVerify(EvpMdCtx *, const unsigned char *sig, size_t siglen, const unsigned char *tbs, size_t tbslen);
}

namespace Signature {

bool verifyEd25519(const QByteArray &publicKey, const QByteArray &message, const QByteArray &signature)
{
    constexpr int NidEd25519 = 1087;
    if (publicKey.size() != 32 || signature.size() != 64)
        return false;
    const auto *pub = reinterpret_cast<const unsigned char *>(publicKey.constData());
    EvpPkey *key = EVP_PKEY_new_raw_public_key(NidEd25519, nullptr, pub, size_t(publicKey.size()));
    if (!key)
        return false;
    EvpMdCtx *ctx = EVP_MD_CTX_new();
    bool ok = false;
    // Ed25519 hashes the whole message itself: no digest is named
    if (ctx && EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1)
        ok = EVP_DigestVerify(ctx, reinterpret_cast<const unsigned char *>(signature.constData()), size_t(signature.size()),
                              reinterpret_cast<const unsigned char *>(message.constData()), size_t(message.size())) == 1;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

QByteArray decodeSignatureFile(const QByteArray &fileBytes)
{
    if (fileBytes.size() == 64)
        return fileBytes;
    const QByteArray text = fileBytes.trimmed();
    const QByteArray raw = QByteArray::fromBase64(text, QByteArray::AbortOnBase64DecodingErrors);
    return raw.size() == 64 ? raw : QByteArray();
}

QByteArray publicKeyFromHex(const QString &hex)
{
    const QString h = hex.trimmed();
    if (h.size() != 64)
        return {};
    const QByteArray raw = QByteArray::fromHex(h.toLatin1());
    return raw.size() == 32 ? raw : QByteArray();
}

} // namespace Signature
