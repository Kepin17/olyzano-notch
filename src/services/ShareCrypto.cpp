#include "ShareCrypto.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

// OpenSSL's libcrypto (already loaded by Qt Network) is used through a handful of declarations
// instead of its headers, so building needs no libssl-dev. These signatures are stable across
// OpenSSL 1.1 and 3.x.
struct EvpCtx;
struct EvpCipher;
extern "C" {
EvpCtx *EVP_CIPHER_CTX_new(void);
void EVP_CIPHER_CTX_free(EvpCtx *);
const EvpCipher *EVP_aes_256_gcm(void);
int EVP_EncryptInit_ex(EvpCtx *, const EvpCipher *, void *engine, const unsigned char *key, const unsigned char *iv);
int EVP_EncryptUpdate(EvpCtx *, unsigned char *out, int *outl, const unsigned char *in, int inl);
int EVP_EncryptFinal_ex(EvpCtx *, unsigned char *out, int *outl);
int EVP_DecryptInit_ex(EvpCtx *, const EvpCipher *, void *engine, const unsigned char *key, const unsigned char *iv);
int EVP_DecryptUpdate(EvpCtx *, unsigned char *out, int *outl, const unsigned char *in, int inl);
int EVP_DecryptFinal_ex(EvpCtx *, unsigned char *out, int *outl);
int EVP_CIPHER_CTX_ctrl(EvpCtx *, int type, int arg, void *ptr);
int RAND_bytes(unsigned char *buf, int num);
}

namespace ShareCrypto {
namespace {
constexpr int EvpCtrlGetTag = 0x10;
constexpr int EvpCtrlSetTag = 0x11;
constexpr int TagSize = 16;
constexpr int PrefixSize = 8;
constexpr int HeaderSize = 4 + PrefixSize + 4;
constexpr quint32 MaxMetaRecord = 64 * 1024;
const QByteArray Magic("NSH1");

const auto *bytes(const QByteArray &b) { return reinterpret_cast<const unsigned char *>(b.constData()); }

QByteArray be32(quint32 v)
{
    QByteArray b(4, 0);
    qToBigEndian(v, b.data());
    return b;
}

QByteArray nonce(const QByteArray &prefix, quint32 index) { return prefix + be32(index); }
QByteArray aad(quint32 index, bool last) { return be32(index) + QByteArray(1, last ? 1 : 0); }

// ciphertext + tag
QByteArray seal(const QByteArray &key, const QByteArray &iv, const QByteArray &ad, const QByteArray &plain)
{
    EvpCtx *ctx = EVP_CIPHER_CTX_new();
    QByteArray out(plain.size() + TagSize, 0);
    int n = 0, total = 0;
    bool ok = ctx && EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, bytes(key), bytes(iv))
              && EVP_EncryptUpdate(ctx, nullptr, &n, bytes(ad), ad.size())
              && EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &n, bytes(plain), plain.size());
    total = n;
    ok = ok && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + total, &n);
    total += n;
    ok = ok && total == plain.size()
         && EVP_CIPHER_CTX_ctrl(ctx, EvpCtrlGetTag, TagSize, out.data() + plain.size());
    EVP_CIPHER_CTX_free(ctx);
    return ok ? out : QByteArray();
}

// plaintext, or a null QByteArray when the tag does not match
QByteArray open(const QByteArray &key, const QByteArray &iv, const QByteArray &ad, const QByteArray &sealed)
{
    if (sealed.size() < TagSize)
        return {};
    const int len = sealed.size() - TagSize;
    QByteArray tag = sealed.right(TagSize);
    EvpCtx *ctx = EVP_CIPHER_CTX_new();
    QByteArray out(len, 0);
    int n = 0, total = 0;
    bool ok = ctx && EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, bytes(key), bytes(iv))
              && EVP_DecryptUpdate(ctx, nullptr, &n, bytes(ad), ad.size())
              && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &n, bytes(sealed), len);
    total = n;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EvpCtrlSetTag, TagSize, tag.data())
         && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + total, &n);
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return {};
    return out.isNull() ? QByteArray("") : out; // an empty plaintext is valid, not a failure
}
}

QByteArray newKey()
{
    QByteArray key(KeySize, 0);
    return RAND_bytes(reinterpret_cast<unsigned char *>(key.data()), KeySize) == 1 ? key : QByteArray();
}

QString keyToFragment(const QByteArray &key)
{
    return QString::fromLatin1(key.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString encryptFile(const QString &inPath, const QString &outPath, const QByteArray &key, const QString &name,
                    const QString &mimeType)
{
    if (key.size() != KeySize)
        return QStringLiteral("invalid key");
    QFile in(inPath), out(outPath);
    if (!in.open(QIODevice::ReadOnly))
        return QStringLiteral("cannot read the file");
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("cannot write the encrypted file");
    QByteArray prefix(PrefixSize, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(prefix.data()), PrefixSize) != 1)
        return QStringLiteral("no random numbers available");

    const qint64 total = in.size();
    const QByteArray meta = QJsonDocument(QJsonObject{{QStringLiteral("name"), name},
                                                      {QStringLiteral("type"), mimeType},
                                                      {QStringLiteral("size"), double(total)}}).toJson(QJsonDocument::Compact);
    auto put = [&](quint32 index, const QByteArray &plain, bool last) {
        const QByteArray sealed = seal(key, nonce(prefix, index), aad(index, last), plain);
        if (sealed.isNull())
            return false;
        const QByteArray record = be32(sealed.size()) + sealed;
        return out.write(record) == record.size();
    };
    if (out.write(Magic + prefix + be32(ChunkSize)) != HeaderSize || !put(0, meta, total == 0))
        return QStringLiteral("cannot write the encrypted file");

    qint64 done = 0;
    for (quint32 index = 1; done < total; ++index) {
        const QByteArray chunk = in.read(qMin<qint64>(ChunkSize, total - done));
        if (chunk.isEmpty())
            return QStringLiteral("the file changed while reading it");
        done += chunk.size();
        if (!put(index, chunk, done >= total))
            return QStringLiteral("cannot write the encrypted file");
    }
    out.close();
    return {};
}

QString decryptFile(const QString &inPath, const QString &outPath, const QByteArray &key, QString *name)
{
    QFile in(inPath), out(outPath);
    if (key.size() != KeySize || !in.open(QIODevice::ReadOnly))
        return QStringLiteral("cannot read the file");
    const QByteArray header = in.read(HeaderSize);
    if (header.size() != HeaderSize || !header.startsWith(Magic))
        return QStringLiteral("not a Notch share");
    const QByteArray prefix = header.mid(4, PrefixSize);
    const quint32 chunk = qFromBigEndian<quint32>(header.constData() + 4 + PrefixSize);
    if (chunk == 0 || chunk > (16u << 20))
        return QStringLiteral("bad chunk size");
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("cannot write the file");

    for (quint32 index = 0; !in.atEnd(); ++index) {
        const QByteArray lenBytes = in.read(4);
        const quint32 len = lenBytes.size() == 4 ? qFromBigEndian<quint32>(lenBytes.constData()) : 0;
        if (len < TagSize || len > (index == 0 ? MaxMetaRecord : chunk) + TagSize)
            return QStringLiteral("damaged data");
        const QByteArray sealed = in.read(len);
        if (quint32(sealed.size()) != len)
            return QStringLiteral("damaged data");
        const QByteArray plain = open(key, nonce(prefix, index), aad(index, in.atEnd()), sealed);
        if (plain.isNull())
            return QStringLiteral("wrong key or damaged data");
        if (index == 0) {
            if (name)
                *name = QJsonDocument::fromJson(plain).object().value(QStringLiteral("name")).toString();
        } else if (out.write(plain) != plain.size()) {
            return QStringLiteral("cannot write the file");
        }
    }
    out.close();
    return {};
}

}
