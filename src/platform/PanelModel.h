#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>

// What Quick Settings can work out without asking the system. Pure, for the tests.
namespace PanelModel {

struct Sink {
    QString name;        // what `pactl set-default-sink` takes
    QString description; // what a person reads
    int volume = 0;      // percent, the loudest channel
    bool mute = false;
    bool isDefault = false;
};

// `pactl -f json list sinks`: volume.<channel>.value_percent is "62%"; the monitor / dummy sinks are kept, the
// default one is marked.
inline QList<Sink> parseSinks(const QByteArray &json, const QString &defaultSink)
{
    QList<Sink> out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        Sink s;
        s.name = o.value(QLatin1String("name")).toString();
        if (s.name.isEmpty())
            continue;
        s.description = o.value(QLatin1String("description")).toString();
        s.mute = o.value(QLatin1String("mute")).toBool();
        const QJsonObject channels = o.value(QLatin1String("volume")).toObject();
        for (auto it = channels.begin(); it != channels.end(); ++it)
            s.volume = qMax(s.volume, it.value().toObject().value(QLatin1String("value_percent")).toString().remove(u'%').toInt());
        s.isDefault = s.name == defaultSink;
        out << s;
    }
    return out;
}

// `pactl -f json list sources`: the same shape as the sinks; what plays through the speakers (".monitor") is not a microphone.
inline QList<Sink> parseSources(const QByteArray &json, const QString &defaultSource)
{
    QList<Sink> all = parseSinks(json, defaultSource), out;
    for (const Sink &s : all)
        if (!s.name.endsWith(QLatin1String(".monitor")))
            out << s;
    return out;
}

// Wi-Fi signal strength (0-100) as 0-4 bars.
inline int wifiBars(int strength)
{
    return strength <= 0 ? 0 : strength < 25 ? 1 : strength < 50 ? 2 : strength < 75 ? 3 : 4;
}

// 0 muted or silent, 1 low, 2 medium, 3 high: which speaker to draw.
inline int volumeLevel(int volume, bool mute)
{
    return mute || volume <= 0 ? 0 : volume < 34 ? 1 : volume < 67 ? 2 : 3;
}

// "2 h 05 min", "35 min"; empty when unknown (0 or less).
inline QString timeLeft(qint64 seconds)
{
    if (seconds <= 0)
        return {};
    const qint64 minutes = (seconds + 30) / 60;
    if (minutes < 60)
        return QStringLiteral("%1 min").arg(qMax<qint64>(1, minutes));
    return QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

}
