#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QString>

// How the app tells whether a newer release is out. Pure, for the tests.
namespace UpdateModel {

// "v0.11.0", "0.11", "0.12.0-beta.1" -> {0, 11, 0} / {0, 11} / {0, 12, 0}; anything else -> {}.
inline QList<int> parseVersion(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*[vV]?(\d+(?:\.\d+){0,3})(?:[-+].*)?\s*$)"));
    const auto m = re.match(text);
    if (!m.hasMatch())
        return {};
    QList<int> out;
    for (const QString &p : m.captured(1).split(u'.'))
        out << p.toInt();
    return out;
}

// Numbers, not text: 0.10.0 is newer than 0.9.3. A missing part counts as 0 (0.11 == 0.11.0). Garbage is never newer.
inline bool isNewer(const QString &latest, const QString &installed)
{
    const QList<int> a = parseVersion(latest), b = parseVersion(installed);
    if (a.isEmpty() || b.isEmpty())
        return false;
    for (int i = 0; i < qMax(a.size(), b.size()); ++i) {
        const int x = i < a.size() ? a.at(i) : 0, y = i < b.size() ? b.at(i) : 0;
        if (x != y)
            return x > y;
    }
    return false;
}

struct Release {
    QString tag, notes, url;                  // the tag ("v0.11.0"), what the release says, its page
    QList<QPair<QString, QString>> assets;    // file name, download address
    bool valid() const { return !parseVersion(tag).isEmpty(); }
    QString version() const { return tag.startsWith(u'v') || tag.startsWith(u'V') ? tag.mid(1) : tag; }
};

// The reply of /repos/<owner>/<repo>/releases/latest. A draft or a pre-release is not "latest" for anyone.
inline Release parseRelease(const QByteArray &json)
{
    Release r;
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (o.isEmpty() || o.value(QStringLiteral("draft")).toBool() || o.value(QStringLiteral("prerelease")).toBool())
        return r;
    r.tag = o.value(QStringLiteral("tag_name")).toString();
    r.notes = o.value(QStringLiteral("body")).toString().trimmed();
    r.url = o.value(QStringLiteral("html_url")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject a = v.toObject();
        r.assets << qMakePair(a.value(QStringLiteral("name")).toString(), a.value(QStringLiteral("browser_download_url")).toString());
    }
    if (!r.valid())
        return {};
    return r;
}

// The file for this system: the .deb on Linux; on Windows the installer, or the zip when the app runs from a folder it was unpacked to.
// Empty when there is none (the release page is then what opens).
inline QString pickAsset(const QList<QPair<QString, QString>> &assets, const QString &platform, bool portable)
{
    const QRegularExpression re(platform == QLatin1String("linux") ? QStringLiteral(R"(^notch_.+_amd64\.deb$)")
                                : portable                          ? QStringLiteral(R"(-windows-x64-portable\.zip$)")
                                                                    : QStringLiteral(R"(-windows-x64-setup\.exe$)"));
    for (const auto &a : assets)
        if (re.match(a.first).hasMatch() && a.second.startsWith(QLatin1String("https://")))
            return a.second;
    return {};
}

}
