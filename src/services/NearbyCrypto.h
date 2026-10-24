#pragma once

#include <QByteArray>
#include <QString>

// The cryptography of Nearby share (computer to computer on one network). Both sides make an ephemeral X25519
// key; the sender commits to its public key (SHA-256) *before* it sees the receiver's, then reveals it. From the
// shared secret and a transcript both sides derive the same 4-digit code, which the people compare on the two
// screens: because of the commitment a man in the middle cannot choose keys that make the two codes match, so
// a short code is enough. The file stream is encrypted with a key derived the same way.
namespace NearbyCrypto {

struct KeyPair {
    QByteArray priv, pub; // 32 raw bytes each; null when generation failed
};

KeyPair generate();
QByteArray agree(const QByteArray &priv, const QByteArray &peerPub); // shared secret Z (32 bytes), null if it fails
QByteArray commitment(const QByteArray &pub);                        // SHA-256 of a public key

// Binds the keys and the offered files into one value; the same bytes must go in on both sides.
QByteArray transcript(const QByteArray &senderPub, const QByteArray &receiverPub, const QByteArray &filesJson);
QString code(const QByteArray &z, const QByteArray &transcript); // "0000".."9999"
QByteArray sessionKey(const QByteArray &z, const QByteArray &transcript); // 32 bytes for ShareCrypto
QByteArray acceptProof(const QByteArray &z, const QByteArray &transcript);

}
