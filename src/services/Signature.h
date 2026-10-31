#pragma once

#include <QByteArray>

// Ed25519 signatures, checked with OpenSSL's libcrypto (the same library ShareCrypto uses). What the updater trusts: the release's checksum
// file is signed with a key only the maintainer holds, and the matching public key is built into the program.
namespace Signature {

// `publicKey`: 32 bytes. `signature`: 64 bytes. false for any wrong length, wrong key, changed message or damaged signature.
bool verifyEd25519(const QByteArray &publicKey, const QByteArray &message, const QByteArray &signature);

// A .sig file as published: the 64 raw bytes, or the same in base64 text (with or without a trailing newline). {} when it is neither.
QByteArray decodeSignatureFile(const QByteArray &fileBytes);

// 64 hex characters -> 32 bytes ({} when it is not exactly that)
QByteArray publicKeyFromHex(const QString &hex);

} // namespace Signature
