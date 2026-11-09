#pragma once

#include "platform/NotchPaths.h"
#include "services/Languages.h"

#include <QSettings>

// The languages in use (Settings → Languages), kept in languages.conf: read by what listens (Audio's automatic language), by the notes of a
// PDF and by OCR, so the choice is one thing for all of them.
namespace LanguageSettings {

inline QStringList enabled()
{
    QSettings s(NotchPaths::conf(QStringLiteral("languages")), QSettings::IniFormat);
    QStringList out;
    for (const QString &c : s.value(QStringLiteral("enabled"), Lang::defaults()).toStringList())
        if (Lang::known(c) && !out.contains(c))
            out << c;
    return out.isEmpty() ? Lang::defaults() : out;
}
inline void setEnabled(const QStringList &codes)
{
    QSettings s(NotchPaths::conf(QStringLiteral("languages")), QSettings::IniFormat);
    s.setValue(QStringLiteral("enabled"), codes);
}
// The Whisper codes of those in use (Chinese once)
inline QStringList whisperCodes(const QStringList &codes)
{
    QStringList out;
    for (const QString &c : codes)
        if (const QString w = Lang::whisperCode(c); !w.isEmpty() && !out.contains(w))
            out << w;
    return out;
}
// A language the person may pick: "auto" or one in the catalog
inline QString valid(const QString &code) { return Lang::known(code) ? code : QStringLiteral("auto"); }

} // namespace LanguageSettings
