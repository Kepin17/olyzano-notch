#include "ShareCrypto.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QPasswordDigestor>
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
constexpr quint32 MaxMetaRecord = 512 * 1024;
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

namespace {
const QByteArray KeyAad("notch-key");
QByteArray passwordKey(const QString &password, const QByteArray &salt)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(), salt, KdfIterations, KeySize);
}
}

QString wrapKey(const QByteArray &key, const QString &password)
{
    QByteArray salt(16, 0), iv(12, 0);
    if (key.size() != KeySize || RAND_bytes(reinterpret_cast<unsigned char *>(salt.data()), 16) != 1
        || RAND_bytes(reinterpret_cast<unsigned char *>(iv.data()), 12) != 1)
        return {};
    const QByteArray sealed = seal(passwordKey(password, salt), iv, KeyAad, key);
    return sealed.isNull() ? QString() : keyToFragment(salt + iv + sealed);
}

QByteArray unwrapKey(const QString &fragment, const QString &password)
{
    const QByteArray raw = QByteArray::fromBase64(fragment.toLatin1(), QByteArray::Base64UrlEncoding);
    if (raw.size() != 16 + 12 + KeySize + TagSize)
        return {};
    return open(passwordKey(password, raw.left(16)), raw.mid(16, 12), KeyAad, raw.mid(28));
}

QString encryptFiles(const QList<SourceFile> &files, const QString &outPath, const QByteArray &key)
{
    if (key.size() != KeySize)
        return QStringLiteral("invalid key");
    if (files.isEmpty() || files.size() > MaxFiles)
        return QStringLiteral("a share can hold 1 to %1 files").arg(MaxFiles);
    QJsonArray list;
    QList<qint64> sizes;
    qint64 records = 1;
    for (const SourceFile &f : files) {
        const QFileInfo info(f.path);
        if (!info.isFile() || !info.isReadable())
            return QStringLiteral("cannot read \"%1\"").arg(f.name);
        sizes << info.size();
        records += (info.size() + ChunkSize - 1) / ChunkSize;
        list.append(QJsonObject{{QStringLiteral("name"), f.name},
                                {QStringLiteral("type"), f.mimeType},
                                {QStringLiteral("size"), double(info.size())}});
    }
    const QByteArray meta = QJsonDocument(QJsonObject{{QStringLiteral("files"), list}}).toJson(QJsonDocument::Compact);
    if (meta.size() > int(MaxMetaRecord))
        return QStringLiteral("the file names are too long");

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("cannot write the encrypted file");
    QByteArray prefix(PrefixSize, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(prefix.data()), PrefixSize) != 1)
        return QStringLiteral("no random numbers available");

    quint32 index = 0;
    auto put = [&](const QByteArray &plain) {
        const bool last = index == records - 1;
        const QByteArray sealed = seal(key, nonce(prefix, index), aad(index, last), plain);
        ++index;
        if (sealed.isNull())
            return false;
        const QByteArray record = be32(sealed.size()) + sealed;
        return out.write(record) == record.size();
    };
    if (out.write(Magic + prefix + be32(ChunkSize)) != HeaderSize || !put(meta))
        return QStringLiteral("cannot write the encrypted file");
    for (int i = 0; i < files.size(); ++i) {
        QFile in(files[i].path);
        if (!in.open(QIODevice::ReadOnly))
            return QStringLiteral("cannot read \"%1\"").arg(files[i].name);
        for (qint64 done = 0; done < sizes[i];) {
            const QByteArray chunk = in.read(qMin<qint64>(ChunkSize, sizes[i] - done));
            if (chunk.isEmpty())
                return QStringLiteral("\"%1\" changed while reading it").arg(files[i].name);
            done += chunk.size();
            if (!put(chunk))
                return QStringLiteral("cannot write the encrypted file");
        }
    }
    out.close();
    return {};
}

QString decryptFiles(const QString &inPath, const QString &outDir, const QByteArray &key, QStringList *names)
{
    QFile in(inPath);
    if (key.size() != KeySize || !in.open(QIODevice::ReadOnly))
        return QStringLiteral("cannot read the file");
    const QByteArray header = in.read(HeaderSize);
    if (header.size() != HeaderSize || !header.startsWith(Magic))
        return QStringLiteral("not a Olyzano Notch share");
    const QByteArray prefix = header.mid(4, PrefixSize);
    const quint32 chunk = qFromBigEndian<quint32>(header.constData() + 4 + PrefixSize);
    if (chunk == 0 || chunk > (16u << 20))
        return QStringLiteral("bad chunk size");

    // Next record's plaintext, or a null array when it is missing / does not authenticate.
    quint32 index = 0;
    auto next = [&](quint32 maxLen) {
        const QByteArray lenBytes = in.read(4);
        const quint32 len = lenBytes.size() == 4 ? qFromBigEndian<quint32>(lenBytes.constData()) : 0;
        if (len < TagSize || len > maxLen + TagSize)
            return QByteArray();
        const QByteArray sealed = in.read(len);
        if (quint32(sealed.size()) != len)
            return QByteArray();
        return open(key, nonce(prefix, index), aad(index, in.atEnd()), sealed);
    };
    const QByteArray metaPlain = next(MaxMetaRecord);
    if (metaPlain.isNull())
        return QStringLiteral("wrong key or damaged data");
    ++index;
    const QJsonArray list = QJsonDocument::fromJson(metaPlain).object().value(QStringLiteral("files")).toArray();
    if (list.isEmpty() || list.size() > MaxFiles)
        return QStringLiteral("damaged data");

    if (!QDir().mkpath(outDir))
        return QStringLiteral("cannot write the file");
    QStringList written;
    for (const QJsonValue &v : list) {
        const QJsonObject f = v.toObject();
        const QString name = QFileInfo(f.value(QStringLiteral("name")).toString()).fileName();
        const qint64 size = f.value(QStringLiteral("size")).toVariant().toLongLong();
        if (name.isEmpty() || name == QLatin1String("..") || size < 0)
            return QStringLiteral("damaged data");
        QFile out(QDir(outDir).filePath(name));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QStringLiteral("cannot write the file");
        for (qint64 left = size; left > 0;) {
            const qint64 want = qMin<qint64>(chunk, left);
            const QByteArray plain = next(chunk);
            if (plain.isNull() || plain.size() != want)
                return QStringLiteral("wrong key or damaged data");
            if (out.write(plain) != plain.size())
                return QStringLiteral("cannot write the file");
            ++index;
            left -= want;
        }
        written << name;
    }
    if (!in.atEnd())
        return QStringLiteral("damaged data");
    if (names)
        *names = written;
    return {};
}

}
