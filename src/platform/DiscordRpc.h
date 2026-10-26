#pragma once

#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>

#include <functional>

#include "platform/DiscordIpc.h"

// Talks to the running Discord program through its local pipe (RPC) to learn whether you are muted or deafened,
// which nothing else shows (not the audio, not the window). It needs the Discord app this build was made with
// (NOTCH_DISCORD_CLIENT_ID / NOTCH_DISCORD_CLIENT_SECRET) and your OK once, in a window Discord opens; the sign-in
// is then kept encrypted for this Windows user. Off until turned on in Settings. Nothing leaves the computer
// except Discord's own sign-in request to discord.com.
class DiscordRpc : public QObject
{
    Q_OBJECT

public:
    explicit DiscordRpc(QObject *parent = nullptr);

    static bool built(); // this build carries a Discord app
    void setEnabled(bool on);
    QString status() const { return m_status; } // what to tell the person, for Settings
    DiscordIpc::Mute mute() const { return m_mute; }

signals:
    void changed();

private:
    void tryConnect();
    void onReadyRead();
    void send(const QString &cmd, const QJsonObject &args, std::function<void(const QJsonObject &)> done = {},
              const QString &evt = QString());
    void onMessage(const QJsonObject &message);
    void authenticate();
    void authorize(bool withVoiceScope);
    void tokenRequest(const QString &grant, const QString &value);
    void ready();
    void loadTokens();
    void saveTokens() const;
    void setStatus(const QString &status);
    void setMute(DiscordIpc::Mute mute);

    QLocalSocket m_socket;
    QByteArray m_buffer;
    QTimer m_retry;
    QNetworkAccessManager m_net;
    QHash<QString, std::function<void(const QJsonObject &)>> m_pending;
    int m_nonce = 0;
    int m_pipe = 0; // discord-ipc-N: Discord takes the first free one of 0..9
    bool m_enabled = false;
    QString m_status;
    DiscordIpc::Mute m_mute = DiscordIpc::Mute::Unknown;
    QString m_access, m_refresh;
    // A token Discord just handed out that was refused at once means it belongs to another Discord account than
    // the one signed in to the app: ask again instead of refreshing the same token for ever.
    bool m_freshToken = false;
    int m_authorizations = 0; // approval windows opened on this connection; stops after a few
};
