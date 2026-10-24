#include "NearbyCrypto.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QtEndian>

// libcrypto through a few hand-written declarations, like ShareCrypto: stable in OpenSSL 1.1.1 and 3.x, and
// building needs no OpenSSL headers on Linux.
struct EvpPkey;
struct EvpPkeyCtx;
extern "C" {
EvpPkeyCtx *EVP_PKEY_CTX_new_id(int id, void *engine);
EvpPkeyCtx *EVP_PKEY_CTX_new(EvpPkey *pkey, void *engine);
void EVP_PKEY_CTX_free(EvpPkeyCtx *);
int EVP_PKEY_keygen_init(EvpPkeyCtx *);
int EVP_PKEY_keygen(EvpPkeyCtx *, EvpPkey **ppkey);
int EVP_PKEY_get_raw_public_key(const EvpPkey *, unsigned char *pub, size_t *len);
int EVP_PKEY_get_raw_private_key(const EvpPkey *, unsigned char *priv, size_t *len);
EvpPkey *EVP_PKEY_new_raw_private_key(int type, void *engine, const unsigned char *key, size_t len);
EvpPkey *EVP_PKEY_new_raw_public_key(int type, void *engine, const unsigned char *key, size_t len);
int EVP_PKEY_derive_init(EvpPkeyCtx *);
int EVP_PKEY_derive_set_peer(EvpPkeyCtx *, EvpPkey *peer);
int EVP_PKEY_derive(EvpPkeyCtx *, unsigned char *key, size_t *keylen);
void EVP_PKEY_free(EvpPkey *);
}

namespace NearbyCrypto {
namespace {
constexpr int X25519 = 1034; // NID_X25519
constexpr size_t KeyBytes = 32;
const auto *bytes(const QByteArray &b) { return reinterpret_cast<const unsigned char *>(b.constData()); }

QByteArray mac(const QByteArray &z, const char *label, const QByteArray &transcript)
{
    return QMessageAuthenticationCode::hash(QByteArray(label) + transcript, z, QCryptographicHash::Sha256);
}
}

KeyPair generate()
{
    KeyPair kp;
    EvpPkeyCtx *ctx = EVP_PKEY_CTX_new_id(X25519, nullptr);
    EvpPkey *key = nullptr;
    if (ctx && EVP_PKEY_keygen_init(ctx) == 1 && EVP_PKEY_keygen(ctx, &key) == 1) {
        QByteArray pub(KeyBytes, 0), priv(KeyBytes, 0);
        size_t pl = KeyBytes, sl = KeyBytes;
        if (EVP_PKEY_get_raw_public_key(key, reinterpret_cast<unsigned char *>(pub.data()), &pl) == 1
            && EVP_PKEY_get_raw_private_key(key, reinterpret_cast<unsigned char *>(priv.data()), &sl) == 1
            && pl == KeyBytes && sl == KeyBytes)
            kp = {priv, pub};
    }
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(ctx);
    return kp;
}

QByteArray agree(const QByteArray &priv, const QByteArray &peerPub)
{
    if (priv.size() != int(KeyBytes) || peerPub.size() != int(KeyBytes))
        return {};
    EvpPkey *mine = EVP_PKEY_new_raw_private_key(X25519, nullptr, bytes(priv), KeyBytes);
    EvpPkey *theirs = EVP_PKEY_new_raw_public_key(X25519, nullptr, bytes(peerPub), KeyBytes);
    EvpPkeyCtx *ctx = mine ? EVP_PKEY_CTX_new(mine, nullptr) : nullptr;
    QByteArray z(KeyBytes, 0);
    size_t len = KeyBytes;
    // OpenSSL refuses the all-zero result that low-order public keys would force.
    const bool ok = theirs && ctx && EVP_PKEY_derive_init(ctx) == 1 && EVP_PKEY_derive_set_peer(ctx, theirs) == 1
                    && EVP_PKEY_derive(ctx, reinterpret_cast<unsigned char *>(z.data()), &len) == 1 && len == KeyBytes;
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(mine);
    EVP_PKEY_free(theirs);
    return ok ? z : QByteArray();
}

QByteArray commitment(const QByteArray &pub)
{
    return QCryptographicHash::hash(pub, QCryptographicHash::Sha256);
}

QByteArray transcript(const QByteArray &senderPub, const QByteArray &receiverPub, const QByteArray &filesJson)
{
    return QCryptographicHash::hash(QByteArray("notch-nearby-1") + senderPub + receiverPub + filesJson, QCryptographicHash::Sha256);
}

QString code(const QByteArray &z, const QByteArray &transcript)
{
    const quint32 n = qFromBigEndian<quint32>(mac(z, "sas", transcript).constData());
    return QStringLiteral("%1").arg(n % 10000, 4, 10, QLatin1Char('0'));
}

QByteArray sessionKey(const QByteArray &z, const QByteArray &transcript) { return mac(z, "key", transcript); }
QByteArray acceptProof(const QByteArray &z, const QByteArray &transcript) { return mac(z, "accept", transcript); }

}
