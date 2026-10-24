#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// End-to-end encryption for shared files (AES-256-GCM in chunks). Notch encrypts, the relay stores
// the result, and the download page decrypts it with Web Crypto. The key never leaves the link's
// #fragment. File format, all integers big-endian:
//   "NSH1" | nonce prefix (8 random bytes) | chunk size (u32) | record*
//   record = length (u32) | ciphertext + 16-byte tag
// Record 0 holds JSON {files: [{name, type, size}, ...]}. The records after it hold the files one
// after another, each chunked on its own boundary (an empty file has none), so the byte range of any
// one file follows from the sizes alone and a page can fetch just that range.
// Nonce = prefix | record index (u32); AAD = index (u32) | 1 byte, 1 on the last record, so records
// can be neither reordered nor cut off. Blocking calls: run them on a worker thread.
namespace ShareCrypto {

constexpr int KeySize = 32;
constexpr quint32 ChunkSize = 1u << 20;
constexpr int MaxFiles = 500;

struct SourceFile {
    QString path, name, mimeType;
};

QByteArray newKey();
QString keyToFragment(const QByteArray &key); // base64url without padding

// Both return an empty string on success, else a user-facing reason. Names must be unique.
QString encryptFiles(const QList<SourceFile> &files, const QString &out, const QByteArray &key);
// Writes every file into outDir under its stored name; `names` gets them in order.
QString decryptFiles(const QString &in, const QString &outDir, const QByteArray &key, QStringList *names = nullptr);

}
