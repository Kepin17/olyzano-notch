#pragma once

#include <QString>
#include <QStringList>

// The microphone-to-speakers loop behind the karaoke button, as PulseAudio / PipeWire modules. Pure, for the tests.
namespace KaraokeModel {

inline const char *Tag = "OlyzanoKaraoke"; // names our stream, so a leftover one can be found and removed

// Arguments of `pactl`: the microphone straight to the output, a short buffer so the voice isn't late.
inline QStringList loadArgs(int latencyMs = 30)
{
    return {QStringLiteral("load-module"), QStringLiteral("module-loopback"), QStringLiteral("source=@DEFAULT_SOURCE@"),
            QStringLiteral("sink=@DEFAULT_SINK@"), QStringLiteral("latency_msec=%1").arg(latencyMs),
            QStringLiteral("source_output_properties=application.name=%1").arg(QLatin1String(Tag))};
}

// `pactl load-module` prints the new module's number.
inline int moduleId(const QString &output)
{
    bool ok = false;
    const int id = output.trimmed().toInt(&ok);
    return ok && id >= 0 ? id : -1;
}

// `pactl list modules short`: "index<TAB>name<TAB>arguments". The loops of ours that are still around.
inline QList<int> staleModules(const QString &list)
{
    QList<int> ids;
    for (const QString &line : list.split(u'\n')) {
        const QStringList cols = line.split(u'\t');
        if (cols.size() >= 3 && cols[1] == QLatin1String("module-loopback") && cols[2].contains(QLatin1String(Tag)))
            if (const int id = moduleId(cols[0]); id >= 0)
                ids << id;
    }
    return ids;
}

}
