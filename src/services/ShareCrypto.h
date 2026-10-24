#pragma once

#include <QByteArray>
#include <QString>

// End-to-end encryption for shared files (AES-256-GCM in chunks). Notch encrypts, the relay stores
// the result, and the download page decrypts it with Web Crypto. The key never leaves the link's
// #fragment. File format, all integers big-endian:
//   "NSH1" | nonce prefix (8 random bytes) | chunk size (u32) | record*
//   record = length (u32) | ciphertext + 16-byte tag
// Record 0 holds JSON {name, type, size}; records 1.. hold the file in chunks.
// Nonce = prefix | record index (u32); AAD = index (u32) | 1 byte, 1 on the last record, so records
// can be neither reordered nor cut off. Blocking calls: run them on a worker thread.
namespace ShareCrypto {

constexpr int KeySize = 32;
constexpr quint32 ChunkSize = 1u << 20;

QByteArray newKey();
QString keyToFragment(const QByteArray &key); // base64url without padding

// Both return an empty string on success, else a user-facing reason.
QString encryptFile(const QString &in, const QString &out, const QByteArray &key, const QString &name,
                    const QString &mimeType);
QString decryptFile(const QString &in, const QString &out, const QByteArray &key, QString *name = nullptr);

}
