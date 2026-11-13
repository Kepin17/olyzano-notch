#pragma once

#include "services/FormStore.h"

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QSet>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

// A form on this network: a short-lived web server on one LAN address (plain HTTP, like Share → This WiFi) that shows the form to whoever has the link
// and takes their answers and files. Under an unguessable path:
//   GET  /<token>/            the page (server/form.html with the form in it; with a PIN, the form is fetched from /def after the PIN)
//   GET  /<token>/def         the form as JSON (PIN)
//   PUT  /<token>/up?field=<id>&name=<file name>    one file, as the raw body; answers {id, size}
//   POST /<token>/submit      {"answers": {...}, "uploads": [ids]} -> checked against the form, then kept in the FormStore
// Limits (from the form): answers taken (then it closes), different devices that may answer (423 for the next), seconds the link works, the size
// of each file and of all the files of one answer, 30 answers per device per 10 minutes. A PIN gates every call (5 wrong tries lock a device for 10 minutes).
class FormServer : public QObject
{
    Q_OBJECT
public:
    explicit FormServer(FormStore *store, QObject *parent = nullptr);
    ~FormServer() override { stop(); }

    // "" on success, else why not. `pin`: "" for none.
    QString start(const QHostAddress &address, const Forms::Form &form, const QString &pin = {});
    void stop();
    bool running() const { return m_server.isListening(); }
    QUrl url() const { return m_url; }
    int responses() const { return m_responses; }
    int people() const { return int(m_people.size()); }
    qint64 expiresAt() const { return m_expiresAt; }
    qint64 opensAt() const { return m_opensAt; }

signals:
    void responded(int n, const QString &name, const QString &device); // an answer was kept (`name`: the one the form marks, or "")
    void edited(int n, const QString &name, const QString &device);    // somebody changed an answer they had sent (not a new one)
    void peopleChanged(int people);
    void turnedAway(const QString &device);
    void finished(const QString &reason); // "full" | "expired" | "stopped"

private:
    friend class FormConnection;
    struct Pending {
        QString ip, field, name, path;
        qint64 size = 0;
    };
    void onConnection();
    void finish(const QString &reason);
    bool admit(const QString &ip, const QString &device);
    int check(const QString &ip, const QString &pin); // 200, 403 wrong, 429 locked
    bool rateOk(const QString &ip);
    QByteArray page() const;
    QByteArray definition() const;
    // the answer: 201 and the number, or a status and why
    int submit(const QString &ip, const QString &device, const QByteArray &body, QByteArray *reply, bool replace);
    // the answer a personal link belongs to, for filling the form again: 200 and the answers, or 404 (the same for a token that is wrong and for a form that takes no changes)
    int mine(const QString &editId, QByteArray *reply) const;
    QJsonObject fullChoices() const; // {question id: [the choices that have taken all they may]}
    bool knowsEdit(const QString &editId) const { return m_form.allowEdit && Forms::validEditId(editId) && m_store->findByEdit(m_form.id, Forms::editHash(editId)) > 0; }
    void dropUploadsOf(const QString &ip);

    FormStore *m_store;
    QTcpServer m_server;
    QTimer m_expire;
    QUrl m_url;
    QString m_token, m_pin, m_tempDir;
    Forms::Form m_form;
    qint64 m_expiresAt = 0, m_opensAt = 0;
    int m_responses = 0;
    QSet<QString> m_people;
    QSet<QString> m_answered; // the addresses that have sent an answer (a form that takes one from each)
    QHash<QString, Pending> m_pending;         // upload id -> the file being held
    QHash<QString, QPair<int, qint64>> m_fails; // ip -> wrong PINs, locked until
    QHash<QString, QList<qint64>> m_sent;       // ip -> when it answered (the last 10 minutes)
};
