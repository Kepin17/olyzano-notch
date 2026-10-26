#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

// The pieces of Discord's local RPC protocol that need no connection, so they can be tested: the frame format of
// the named pipe (`discord-ipc-0` ... `-9`), and how Discord's voice settings read as muted or deafened.
// A frame is: opcode (int32, little endian), length (int32, little endian), then `length` bytes of JSON.
namespace DiscordIpc {

enum Op : qint32 { Handshake = 0, Frame = 1, Close = 2, Ping = 3, Pong = 4 };

inline QByteArray encode(qint32 op, const QJsonObject &payload)
{
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray out(8, '\0');
    qToLittleEndian<qint32>(op, out.data());
    qToLittleEndian<qint32>(qint32(body.size()), out.data() + 4);
    return out + body;
}

// Takes one whole frame off the front of `buffer`. False if more bytes are needed (or the header is nonsense:
// then the buffer is dropped, so a broken stream cannot grow without end).
inline bool take(QByteArray &buffer, qint32 *op, QJsonObject *payload)
{
    if (buffer.size() < 8)
        return false;
    const qint32 length = qFromLittleEndian<qint32>(buffer.constData() + 4);
    if (length < 0 || length > (1 << 20)) {
        buffer.clear();
        return false;
    }
    if (buffer.size() < 8 + length)
        return false;
    *op = qFromLittleEndian<qint32>(buffer.constData());
    *payload = QJsonDocument::fromJson(buffer.mid(8, length)).object();
    buffer.remove(0, 8 + length);
    return true;
}

enum class Mute { Unknown, Live, Muted, Deafened };

// Discord's voice settings: `mute` is the microphone switched off, `deaf` the sound switched off too.
// Deafened wins, because Discord mutes you whenever it deafens you.
inline Mute muteFrom(const QJsonObject &settings)
{
    if (!settings.contains(QLatin1String("mute")) && !settings.contains(QLatin1String("deaf")))
        return Mute::Unknown;
    if (settings.value(QLatin1String("deaf")).toBool())
        return Mute::Deafened;
    return settings.value(QLatin1String("mute")).toBool() ? Mute::Muted : Mute::Live;
}

}
