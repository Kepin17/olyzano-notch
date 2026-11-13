#pragma once

#include "app/ShareSession.h"
#include "services/FormStore.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>

class QNetworkAccessManager;

// A form shared through the user's own server (the relay): the form is sealed on this computer (the key stays in the link's #fragment), the server
// holds it and the sealed answers people send from their browsers, and this fetches the answers, opens them, checks them against the form and keeps
// them in the FormStore. The server can read none of it. It goes on while Notch runs; the form stays open on the server when Notch is closed, and
// the answers that came meanwhile are fetched at the next start.
class FormRelay : public QObject
{
    Q_OBJECT
public:
    FormRelay(QNetworkAccessManager *nam, const RelayAccess &relay, FormStore *store, const QString &formId, QObject *parent = nullptr);

    void publish(const Forms::Form &form, const QString &pin); // asynchronous: `published(error)`
    bool resume();                                             // from what was saved; false when there is nothing (or it ended)
    void close();                                              // the form is deleted on the server; what was fetched stays
    QString url() const { return m_url; }
    qint64 expiresAt() const { return m_expiresAt; }
    int people() const { return m_people; }
    qint64 maxBytes() const { return m_maxBytes; } // the largest answer the server takes
    bool active() const { return m_active; }
    void setPollInterval(int ms) { m_timer.setInterval(ms); } // (the default is a few seconds; for tests)

signals:
    void published(const QString &error); // "" when the form is on the server
    void responded(int n, const QString &name, const QString &device);
    void edited(int n, const QString &name, const QString &device); // somebody changed an answer they had sent
    void rejected(const QString &device); // an answer that did not follow the form was thrown away
    void changed();
    void ended(const QString &reason);    // "expired" | "closed"

private:
    void poll();
    void fetch(const QList<QJsonObject> &queue);
    void keep(int n, const QString &device, const QString &dir, const QStringList &names);
    void finishOne(const QList<QJsonObject> &rest, int n);
    void save() const;

    QNetworkAccessManager *m_nam;
    RelayAccess m_relay;
    FormStore *m_store;
    QString m_formId, m_relayId, m_url;
    QByteArray m_key;
    QTimer m_timer;
    qint64 m_expiresAt = 0, m_maxBytes = 0;
    int m_seen = 0, m_people = 0, m_generation = 0;
    bool m_active = false, m_busy = false;
};
