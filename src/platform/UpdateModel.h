#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QHash>
#include <QUrl>
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

// The reply of /repos/<owner>/<repo>/contributors: people with commits, most first; bots are left out.
inline QStringList parseContributors(const QByteArray &json)
{
    QStringList names;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        const QString login = o.value(QStringLiteral("login")).toString();
        if (!login.isEmpty() && o.value(QStringLiteral("type")).toString() != QLatin1String("Bot") && !login.endsWith(QLatin1String("[bot]")))
            names << login;
    }
    return names;
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


// ---- Installing the update from inside the app ----

// The checksum file of this system in the release: SHA256SUMS-linux.txt / SHA256SUMS-windows.txt.
inline QString pickChecksums(const QList<QPair<QString, QString>> &assets, const QString &platform)
{
    const QString want = platform == QLatin1String("linux") ? QStringLiteral("SHA256SUMS-linux.txt") : QStringLiteral("SHA256SUMS-windows.txt");
    for (const auto &a : assets)
        if (a.first == want && a.second.startsWith(QLatin1String("https://")))
            return a.second;
    return {};
}

// Its signature: SHA256SUMS-linux.txt.sig / SHA256SUMS-windows.txt.sig (Ed25519, made with the maintainer's key).
inline QString pickSignature(const QList<QPair<QString, QString>> &assets, const QString &platform)
{
    const QString want = (platform == QLatin1String("linux") ? QStringLiteral("SHA256SUMS-linux.txt") : QStringLiteral("SHA256SUMS-windows.txt")) + QStringLiteral(".sig");
    for (const auto &a : assets)
        if (a.first == want && a.second.startsWith(QLatin1String("https://")))
            return a.second;
    return {};
}

// "<64 hex>  file name" or "<64 hex> *file name" per line (sha256sum / Get-FileHash style) -> file name -> lower-case hex.
inline QHash<QString, QString> parseChecksums(const QString &text)
{
    static const QRegularExpression line(QStringLiteral(R"(^\s*([0-9a-fA-F]{64})\s+\*?(.+?)\s*$)"));
    QHash<QString, QString> out;
    for (const QString &l : text.split(u'\n')) {
        const QRegularExpressionMatch m = line.match(l);
        if (m.hasMatch())
            out.insert(m.captured(2), m.captured(1).toLower());
    }
    return out;
}

inline bool sha256Matches(const QByteArray &hashHex, const QString &expectedHex)
{
    return !expectedHex.isEmpty() && QString::fromLatin1(hashHex).compare(expectedHex, Qt::CaseInsensitive) == 0;
}

// Only GitHub's own release hosts are trusted to serve the file (the address came from the API, but a redirect could lead anywhere).
inline bool trustedHost(const QUrl &url)
{
    if (url.scheme() != QLatin1String("https"))
        return false;
    const QString h = url.host().toLower();
    return h == QLatin1String("github.com") || h.endsWith(QLatin1String(".githubusercontent.com")) || h.endsWith(QLatin1String(".github.com"));
}

// What the release's own asset links (before any redirect) must look like: GitHub, and when it is github.com itself, this project's repository.
// (A redirect then goes to GitHub's storage hosts, which trustedHost() allows.) The file name is only a name.
inline bool trustedAsset(const QUrl &url)
{
    if (!trustedHost(url))
        return false;
    const QString name = url.fileName();
    if (name.isEmpty() || name.startsWith(u'.') || name.contains(u'\\'))
        return false;
    return url.host().toLower() != QLatin1String("github.com") || url.path().startsWith(QLatin1String("/Kepin17/olyzano-notch/"));
}

// The arguments of the Windows installer (Inno Setup): no windows, no restart of the PC, close Notch and start it again afterwards.
inline QStringList windowsInstallerArgs() { return {QStringLiteral("/SILENT"), QStringLiteral("/SUPPRESSMSGBOXES"), QStringLiteral("/NORESTART"), QStringLiteral("/CLOSEAPPLICATIONS"), QStringLiteral("/RESTARTAPPLICATIONS")}; }

// The Linux command that installs the .deb with the system's own privilege prompt (polkit).
inline QStringList linuxInstallCommand(const QString &debPath) { return {QStringLiteral("pkexec"), QStringLiteral("apt-get"), QStringLiteral("install"), QStringLiteral("-y"), debPath}; }

}
