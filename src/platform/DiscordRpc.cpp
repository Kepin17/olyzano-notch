#include "platform/DiscordRpc.h"

#include "models/FileItem.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

#include "notch_discord_app.h" // written by CMake from .env / -D / CI secrets; empty strings without them

namespace {
const QString RedirectUri = QStringLiteral("http://localhost"); // must be listed under the Discord app's redirects

QString clientId() { return QStringLiteral(NOTCH_DISCORD_CLIENT_ID); }

// The sign-in is kept encrypted for this Windows user (DPAPI): another user, or a copy of the file, cannot read it.
// On Linux it is a plain file that only this user may open (0600), like ssh keys and browser tokens.
QByteArray dpapi(const QByteArray &in, bool encrypt)
{
#ifndef Q_OS_WIN
    Q_UNUSED(encrypt)
    return in;
#else
    DATA_BLOB source{DWORD(in.size()), reinterpret_cast<BYTE *>(const_cast<char *>(in.constData()))};
    DATA_BLOB result{};
    const BOOL ok = encrypt ? CryptProtectData(&source, L"Olyzano Notch", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result)
                            : CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &result);
    if (!ok)
        return QByteArray();
    const QByteArray out(reinterpret_cast<const char *>(result.pbData), int(result.cbData));
    LocalFree(result.pbData);
    return out;
#endif
}

// Discord's pipe number i: 0..9 is `discord-ipc-N`; on Linux 10..29 are the same names where Flatpak and Snap put them.
QString pipeName(int i)
{
    const QString name = QStringLiteral("discord-ipc-%1").arg(i % 10);
#ifdef Q_OS_WIN
    return name;
#else
    const QString run = qEnvironmentVariable("XDG_RUNTIME_DIR", QStringLiteral("/tmp"));
    switch (i / 10) {
    case 1: return run + QStringLiteral("/app/com.discordapp.Discord/") + name;
    case 2: return run + QStringLiteral("/snap.discord/") + name;
    default: return run + u'/' + name;
    }
#endif
}
constexpr int PipeCount =
#ifdef Q_OS_WIN
    10;
#else
    30;
#endif

QString tokenFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/notch/discord.dat");
}
}

DiscordRpc::DiscordRpc(QObject *parent)
    : QObject(parent)
{
    m_retry.setInterval(15000); // Discord may start any time: look again now and then, cheaply
    connect(&m_retry, &QTimer::timeout, this, &DiscordRpc::tryConnect);
    connect(&m_socket, &QLocalSocket::readyRead, this, &DiscordRpc::onReadyRead);
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        m_buffer.clear();
        m_freshToken = false;
        m_authorizations = 0;
        m_socket.write(DiscordIpc::encode(DiscordIpc::Handshake, {{QStringLiteral("v"), 1}, {QStringLiteral("client_id"), clientId()}}));
    });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this] {
        if (!m_enabled || m_socket.state() != QLocalSocket::UnconnectedState)
            return;
        if (++m_pipe < PipeCount) { // that pipe is not there: Discord may hold the next one
            m_socket.connectToServer(pipeName(m_pipe));
            return;
        }
        setStatus(QStringLiteral("Waiting for Discord to start"));
    });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        m_pending.clear();
        m_buffer.clear();
        setMute(DiscordIpc::Mute::Unknown);
        if (m_enabled)
            setStatus(QStringLiteral("Waiting for Discord to start"));
    });
}

bool DiscordRpc::built()
{
    return !clientId().isEmpty() && qstrlen(NOTCH_DISCORD_CLIENT_SECRET) > 0;
}

void DiscordRpc::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (!on) {
        m_retry.stop();
        m_socket.abort();
        m_access.clear();
        m_refresh.clear();
        setMute(DiscordIpc::Mute::Unknown);
        setStatus(QString());
        return;
    }
    if (!built()) {
        setStatus(QStringLiteral("This build has no Discord app, so it cannot read your mute state"));
        return;
    }
    setStatus(QStringLiteral("Waiting for Discord to start"));
    m_retry.start();
    tryConnect();
}

void DiscordRpc::tryConnect()
{
    if (!m_enabled || !built() || m_socket.state() != QLocalSocket::UnconnectedState)
        return;
    m_pipe = 0;
    m_socket.connectToServer(pipeName(0));
}

void DiscordRpc::onReadyRead()
{
    m_buffer += m_socket.readAll();
    qint32 op = 0;
    QJsonObject message;
    while (DiscordIpc::take(m_buffer, &op, &message)) {
        if (op == DiscordIpc::Ping) {
            m_socket.write(DiscordIpc::encode(DiscordIpc::Pong, message));
        } else if (op == DiscordIpc::Close) { // e.g. {"code":4000,"message":"Invalid Client ID"}
            setStatus(QStringLiteral("Discord closed the connection: %1").arg(message.value(QLatin1String("message")).toString()));
            m_socket.abort();
        } else if (op == DiscordIpc::Frame) {
            onMessage(message);
        }
    }
}

void DiscordRpc::send(const QString &cmd, const QJsonObject &args, std::function<void(const QJsonObject &)> done, const QString &evt)
{
    const QString nonce = QString::number(++m_nonce);
    QJsonObject message{{QStringLiteral("cmd"), cmd}, {QStringLiteral("args"), args}, {QStringLiteral("nonce"), nonce}};
    if (!evt.isEmpty())
        message.insert(QStringLiteral("evt"), evt);
    if (done)
        m_pending.insert(nonce, std::move(done));
    m_socket.write(DiscordIpc::encode(DiscordIpc::Frame, message));
}

void DiscordRpc::onMessage(const QJsonObject &message)
{
    const QString evt = message.value(QLatin1String("evt")).toString();
    if (evt == QLatin1String("READY")) {
        authenticate();
        return;
    }
    if (evt == QLatin1String("VOICE_SETTINGS_UPDATE")) {
        setMute(DiscordIpc::muteFrom(message.value(QLatin1String("data")).toObject()));
        return;
    }
    const auto it = m_pending.find(message.value(QLatin1String("nonce")).toString());
    if (it != m_pending.end()) {
        const auto done = it.value();
        m_pending.erase(it);
        done(message);
    }
}

void DiscordRpc::loadTokens()
{
    QFile file(tokenFile());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(dpapi(file.readAll(), false)).object();
    m_access = o.value(QLatin1String("access")).toString();
    m_refresh = o.value(QLatin1String("refresh")).toString();
}

void DiscordRpc::saveTokens() const
{
    QDir().mkpath(QFileInfo(tokenFile()).absolutePath());
    QFile file(tokenFile());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(dpapi(QJsonDocument(QJsonObject{{QStringLiteral("access"), m_access}, {QStringLiteral("refresh"), m_refresh}}).toJson(), true));
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

// AUTHENTICATE with the saved sign-in; ask Discord for a new one (it opens an approval window) when there is none.
void DiscordRpc::authenticate()
{
    if (m_access.isEmpty())
        loadTokens();
    if (m_access.isEmpty()) {
        if (m_refresh.isEmpty())
            authorize(true);
        else
            tokenRequest(QStringLiteral("refresh_token"), m_refresh);
        return;
    }
    setStatus(QStringLiteral("Signing in to Discord"));
    send(QStringLiteral("AUTHENTICATE"), {{QStringLiteral("access_token"), m_access}}, [this](const QJsonObject &reply) {
        if (reply.value(QLatin1String("evt")).toString() != QLatin1String("ERROR")) {
            ready();
            return;
        }
        m_access.clear(); // expired, taken back, or of another Discord account: refresh it, or ask again
        if (m_freshToken || m_refresh.isEmpty()) {
            m_freshToken = false;
            m_refresh.clear();
            authorize(true);
        } else {
            tokenRequest(QStringLiteral("refresh_token"), m_refresh);
        }
    });
}

void DiscordRpc::authorize(bool withVoiceScope)
{
    if (++m_authorizations > 3) {
        setStatus(QStringLiteral("Discord did not accept the sign-in. Check that this Discord account may use the app, then turn the switch off and on"));
        return;
    }
    setStatus(QStringLiteral("Approve Olyzano Notch in the window Discord just opened"));
    QJsonArray scopes{QStringLiteral("rpc")};
    if (withVoiceScope)
        scopes.append(QStringLiteral("rpc.voice.read"));
    send(QStringLiteral("AUTHORIZE"), {{QStringLiteral("client_id"), clientId()}, {QStringLiteral("scopes"), scopes}},
         [this, withVoiceScope](const QJsonObject &reply) {
             const QJsonObject data = reply.value(QLatin1String("data")).toObject();
             if (reply.value(QLatin1String("evt")).toString() == QLatin1String("ERROR") || data.value(QLatin1String("code")).toString().isEmpty()) {
                 if (withVoiceScope) // Discord may only give that scope to approved apps: try without it
                     authorize(false);
                 else
                     setStatus(QStringLiteral("Discord did not allow it: %1").arg(data.value(QLatin1String("message")).toString()));
                 return;
             }
             tokenRequest(QStringLiteral("authorization_code"), data.value(QLatin1String("code")).toString());
         });
}

// Trades Discord's one-time code (or the saved refresh token) for an access token, at discord.com.
void DiscordRpc::tokenRequest(const QString &grant, const QString &value)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), clientId());
    form.addQueryItem(QStringLiteral("client_secret"), QStringLiteral(NOTCH_DISCORD_CLIENT_SECRET));
    form.addQueryItem(QStringLiteral("grant_type"), grant);
    if (grant == QLatin1String("refresh_token")) {
        form.addQueryItem(QStringLiteral("refresh_token"), value);
    } else {
        form.addQueryItem(QStringLiteral("code"), value);
        form.addQueryItem(QStringLiteral("redirect_uri"), RedirectUri);
    }
    QNetworkRequest request(QUrl(QStringLiteral("https://discord.com/api/v10/oauth2/token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply *reply = m_net.post(request, form.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, grant] {
        reply->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (o.value(QLatin1String("access_token")).toString().isEmpty()) {
            m_refresh.clear();
            if (grant == QLatin1String("refresh_token") && m_socket.state() == QLocalSocket::ConnectedState)
                authorize(true); // the saved sign-in is gone: ask once more
            else
                setStatus(QStringLiteral("Discord refused the sign-in: %1")
                              .arg(o.value(QLatin1String("error_description")).toString(o.value(QLatin1String("error")).toString())));
            return;
        }
        m_access = o.value(QLatin1String("access_token")).toString();
        m_refresh = o.value(QLatin1String("refresh_token")).toString();
        m_freshToken = true;
        saveTokens();
        if (m_socket.state() == QLocalSocket::ConnectedState)
            authenticate();
    });
}

// Signed in: read the current voice settings, then be told about every change.
void DiscordRpc::ready()
{
    send(QStringLiteral("GET_VOICE_SETTINGS"), {}, [this](const QJsonObject &reply) {
        if (reply.value(QLatin1String("evt")).toString() == QLatin1String("ERROR")) {
            setStatus(QStringLiteral("Signed in, but Discord does not give this app your voice settings: %1")
                          .arg(reply.value(QLatin1String("data")).toObject().value(QLatin1String("message")).toString()));
            return;
        }
        m_freshToken = false;
        m_authorizations = 0;
        setMute(DiscordIpc::muteFrom(reply.value(QLatin1String("data")).toObject()));
        setStatus(QStringLiteral("Connected to Discord"));
        send(QStringLiteral("SUBSCRIBE"), {}, {}, QStringLiteral("VOICE_SETTINGS_UPDATE"));
    });
}

void DiscordRpc::setStatus(const QString &status)
{
    if (status == m_status)
        return;
    m_status = status;
    emit changed();
}

void DiscordRpc::setMute(DiscordIpc::Mute mute)
{
    if (mute == m_mute)
        return;
    m_mute = mute;
    emit changed();
}
