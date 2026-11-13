#include "FormRelay.h"

#include "services/ShareCrypto.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QtConcurrent>

namespace {
constexpr int PollMs = 5000;
constexpr qint64 Overhead = 2 * 1048576; // the sealed answer is a little bigger than the files in it

struct Opened {
    QString error, dir;
    QStringList names;
};
}

FormRelay::FormRelay(QNetworkAccessManager *nam, const RelayAccess &relay, FormStore *store, const QString &formId, QObject *parent)
    : QObject(parent), m_nam(nam), m_relay(relay), m_store(store), m_formId(formId)
{
    m_timer.setInterval(PollMs);
    connect(&m_timer, &QTimer::timeout, this, &FormRelay::poll);
}

void FormRelay::save() const
{
    m_store->saveRelay(m_formId, {{QStringLiteral("serverId"), m_relay.id}, {QStringLiteral("relayId"), m_relayId}, {QStringLiteral("key"), ShareCrypto::keyToFragment(m_key)},
                                  {QStringLiteral("url"), m_url}, {QStringLiteral("expiresAt"), double(m_expiresAt)}, {QStringLiteral("seen"), m_seen},
                                  {QStringLiteral("maxBytes"), double(m_maxBytes)}});
}

void FormRelay::publish(const Forms::Form &form, const QString &pin)
{
    if (!m_relay.valid()) {
        emit published(tr("Set up your server first (Settings → Sharing)"));
        return;
    }
    m_key = ShareCrypto::newKey();
    const QByteArray sealed = ShareCrypto::sealBlob(m_key, QJsonDocument(QJsonObject{{QStringLiteral("form"), Forms::toJson(form)}}).toJson(QJsonDocument::Compact));
    if (m_key.isEmpty() || sealed.isEmpty()) {
        emit published(tr("The form could not be sealed"));
        return;
    }
    QNetworkRequest req = m_relay.request(QStringLiteral("/api/v1/forms"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    req.setRawHeader("X-TTL", QByteArray::number(Forms::effectiveTtl(form, QDateTime::currentSecsSinceEpoch())));
    req.setRawHeader("X-Opens-At", QByteArray::number(form.limits.opensAt));
    if (form.allowEdit)
        req.setRawHeader("X-Allow-Edit", "1");
    if (form.oneAnswer)
        req.setRawHeader("X-One-Answer", "1");
    req.setRawHeader("X-Max-Responses", QByteArray::number(form.limits.maxResponses));
    req.setRawHeader("X-Max-People", QByteArray::number(form.limits.maxPeople));
    req.setRawHeader("X-Max-Response-Bytes", QByteArray::number(qint64(form.limits.maxTotalMb) * 1048576 + Overhead));
    if (!pin.isEmpty())
        req.setRawHeader("X-Pin", pin.toUtf8());
    QNetworkReply *reply = m_nam->post(req, sealed);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (status != 201) {
            QString why = o.value(QStringLiteral("message")).toString();
            if (why.isEmpty())
                why = status == 401 || status == 403 ? tr("The server did not accept the key") : reply->error() != QNetworkReply::NoError ? tr("Cannot reach the server") : tr("The server could not take the form");
            emit published(why);
            return;
        }
        m_relayId = o.value(QStringLiteral("id")).toString();
        m_expiresAt = o.value(QStringLiteral("expires_at")).toVariant().toLongLong();
        m_maxBytes = o.value(QStringLiteral("max_bytes")).toVariant().toLongLong();
        QUrl url = m_relay.base;
        url.setPath(url.path() + QStringLiteral("/f/") + m_relayId);
        url.setFragment(ShareCrypto::keyToFragment(m_key));
        m_url = url.toString();
        m_seen = 0;
        m_active = true;
        save();
        m_timer.start();
        emit published(QString());
        emit changed();
    });
}

bool FormRelay::resume()
{
    const QJsonObject o = m_store->relay(m_formId);
    if (o.isEmpty() || !m_relay.valid())
        return false;
    m_relayId = o.value(QStringLiteral("relayId")).toString();
    m_key = ShareCrypto::keyFromFragment(o.value(QStringLiteral("key")).toString());
    m_url = o.value(QStringLiteral("url")).toString();
    m_expiresAt = o.value(QStringLiteral("expiresAt")).toVariant().toLongLong();
    m_maxBytes = o.value(QStringLiteral("maxBytes")).toVariant().toLongLong();
    m_seen = o.value(QStringLiteral("seen")).toInt();
    if (m_relayId.isEmpty() || m_key.isEmpty() || m_expiresAt <= QDateTime::currentSecsSinceEpoch())
        return false;
    m_active = true;
    m_timer.start();
    QTimer::singleShot(500, this, &FormRelay::poll);
    return true;
}

void FormRelay::close()
{
    ++m_generation;
    m_timer.stop();
    if (m_active && !m_relayId.isEmpty() && m_relay.valid()) {
        QNetworkReply *r = m_nam->deleteResource(m_relay.request(QStringLiteral("/api/v1/forms/") + m_relayId));
        connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
    }
    m_active = false;
    m_store->removeRelay(m_formId);
    emit changed();
}

void FormRelay::poll()
{
    if (!m_active || m_busy)
        return;
    m_busy = true;
    const int gen = m_generation;
    QNetworkReply *reply = m_nam->get(m_relay.request(QStringLiteral("/api/v1/forms/%1/responses?after=%2").arg(m_relayId).arg(m_seen)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen] {
        reply->deleteLater();
        if (gen != m_generation) {
            m_busy = false;
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) { // gone from the server: its time was up, or it was closed from elsewhere
            m_busy = false;
            m_active = false;
            m_timer.stop();
            m_store->removeRelay(m_formId);
            emit changed();
            emit ended(m_expiresAt <= QDateTime::currentSecsSinceEpoch() + 5 ? QStringLiteral("expired") : QStringLiteral("closed"));
            return;
        }
        if (status != 200) { // no network now: the next look
            m_busy = false;
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const int people = o.value(QStringLiteral("people")).toInt();
        if (people != m_people) {
            m_people = people;
            emit changed();
        }
        QList<QJsonObject> queue;
        for (const QJsonValue &v : o.value(QStringLiteral("responses")).toArray())
            queue.append(v.toObject());
        fetch(queue);
    });
}

// the answers one after another: the sealed blob, opened on a worker thread, checked and kept here
void FormRelay::fetch(const QList<QJsonObject> &queue)
{
    if (queue.isEmpty()) {
        m_busy = false;
        return;
    }
    const QJsonObject head = queue.first();
    const QList<QJsonObject> rest = queue.mid(1);
    const int n = head.value(QStringLiteral("n")).toInt();
    const QString device = head.value(QStringLiteral("device")).toString();
    const int gen = m_generation;
    QNetworkReply *reply = m_nam->get(m_relay.request(QStringLiteral("/api/v1/forms/%1/responses/%2").arg(m_relayId).arg(n)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, n, device, rest] {
        reply->deleteLater();
        if (gen != m_generation) {
            m_busy = false;
            return;
        }
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) { // try again at the next look
            m_busy = false;
            return;
        }
        const QByteArray blob = reply->readAll();
        const QString work = m_store->tempDir(m_formId) + QStringLiteral("/answer-") + QString::number(n);
        const QByteArray key = m_key;
        auto *watcher = new QFutureWatcher<Opened>(this);
        connect(watcher, &QFutureWatcher<Opened>::finished, this, [this, watcher, gen, n, device, rest, work] {
            const Opened opened = watcher->result();
            watcher->deleteLater();
            if (gen != m_generation) {
                QDir(work).removeRecursively();
                m_busy = false;
                return;
            }
            if (opened.error.isEmpty())
                keep(n, device, opened.dir, opened.names);
            else
                emit rejected(device); // not made with this form's key, or damaged: nothing to keep
            QDir(work).removeRecursively();
            finishOne(rest, n);
        });
        watcher->setFuture(QtConcurrent::run([blob, key, work] {
            Opened o;
            QDir(work).removeRecursively();
            QDir().mkpath(work);
            const QString file = work + QStringLiteral(".blob");
            QFile f(file);
            if (!f.open(QIODevice::WriteOnly) || f.write(blob) != blob.size()) {
                o.error = QStringLiteral("cannot write");
                return o;
            }
            f.close();
            o.error = ShareCrypto::decryptFiles(file, work, key, &o.names);
            QFile::remove(file);
            o.dir = work;
            return o;
        }));
    });
}

void FormRelay::keep(int n, const QString &device, const QString &dir, const QStringList &names)
{
    bool ok = false;
    const Forms::Form form = m_store->load(m_formId, &ok);
    QFile af(dir + QStringLiteral("/answers.json"));
    if (!ok || !names.contains(QStringLiteral("answers.json")) || !af.open(QIODevice::ReadOnly)) {
        emit rejected(device);
        return;
    }
    const QJsonObject doc = QJsonDocument::fromJson(af.readAll()).object();
    static const QRegularExpression entryName(QStringLiteral("^f\\d{4}$"));
    QList<Forms::Upload> uploads;
    for (const QJsonValue &v : doc.value(QStringLiteral("files")).toArray()) { // what the browser says about its files is checked, not believed
        const QJsonObject f = v.toObject();
        const QString entry = f.value(QStringLiteral("entry")).toString();
        if (!entryName.match(entry).hasMatch() || !names.contains(entry)) {
            emit rejected(device);
            return;
        }
        const QString path = dir + QLatin1Char('/') + entry;
        uploads.append({f.value(QStringLiteral("field")).toString(), f.value(QStringLiteral("name")).toString().left(300), path, QFileInfo(path).size()});
    }
    // a personal link: the token is inside the seal, so only the owner's app and the person know it
    const QString editId = doc.value(QStringLiteral("edit")).toString();
    const QString hash = form.allowEdit && Forms::validEditId(editId) ? Forms::editHash(editId) : QString();
    const bool replace = doc.value(QStringLiteral("replace")).toBool();
    const int at = replace && !hash.isEmpty() ? m_store->findByEdit(m_formId, hash) : 0;
    if (replace && at == 0) { // a change for an answer that is not here
        emit rejected(device);
        return;
    }
    QJsonObject clean;
    const QJsonObject kept = at > 0 ? m_store->response(m_formId, at).value(QStringLiteral("answers")).toObject() : QJsonObject();
    if (!Forms::validateSubmission(form, doc.value(QStringLiteral("answers")).toObject(), uploads, &clean, kept).isEmpty()) {
        emit rejected(device);
        return;
    }
    // only files of questions that take files, and only as many as each takes (validateSubmission counts the ones of known questions)
    QSet<QString> fileFields;
    for (const Forms::Field &x : form.fields)
        if (Forms::takesFiles(x))
            fileFields.insert(x.id);
    for (const Forms::Upload &u : std::as_const(uploads))
        if (!fileFields.contains(u.field)) {
            emit rejected(device);
            return;
        }
    if (at > 0) {
        if (m_store->replaceResponse(m_formId, at, clean, uploads, device))
            emit edited(at, Forms::nameOf(form, clean), device);
        return;
    }
    if (!hash.isEmpty() && m_store->findByEdit(m_formId, hash) > 0) { // one token, one answer
        emit rejected(device);
        return;
    }
    if (m_store->addResponse(m_formId, clean, uploads, device, hash) > 0)
        emit responded(n, Forms::nameOf(form, clean), device);
}

void FormRelay::finishOne(const QList<QJsonObject> &rest, int n)
{
    m_seen = qMax(m_seen, n);
    save();
    QNetworkReply *ack = m_nam->deleteResource(m_relay.request(QStringLiteral("/api/v1/forms/%1/responses/%2").arg(m_relayId).arg(n)));
    connect(ack, &QNetworkReply::finished, ack, &QObject::deleteLater);
    emit changed();
    fetch(rest);
}
