#include "FormServer.h"

#include "services/DeviceLabel.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QUrlQuery>

// One HTTP exchange with a visitor's browser. Owned by its socket.
class FormConnection : public QObject
{
public:
    FormConnection(FormServer *server, QTcpSocket *socket)
        : QObject(socket), m_server(server), m_socket(socket), m_ip(socket->peerAddress().toString())
    {
        m_idle.setSingleShot(true);
        m_idle.setInterval(30000);
        connect(&m_idle, &QTimer::timeout, socket, &QTcpSocket::abort);
        connect(socket, &QTcpSocket::readyRead, this, &FormConnection::onRead);
        connect(socket, &QTcpSocket::disconnected, this, &FormConnection::onGone);
        m_idle.start();
    }

private:
    static constexpr int MaxHeader = 16 * 1024, MaxSubmit = 256 * 1024;

    void onRead()
    {
        m_idle.start();
        if (!m_headDone) {
            m_buf += m_socket->readAll();
            if (m_buf.size() > MaxHeader + MaxSubmit)
                return m_socket->abort();
            const int end = m_buf.indexOf("\r\n\r\n");
            if (end < 0) {
                if (m_buf.size() > MaxHeader)
                    m_socket->abort();
                return;
            }
            if (!parseHead(m_buf.left(end)))
                return;
            m_body = m_buf.mid(end + 4);
            m_buf.clear();
            m_headDone = true;
            if (!begin())
                return;
        } else {
            m_body = m_socket->readAll();
        }
        feed(m_body);
        m_body.clear();
    }

    bool parseHead(const QByteArray &head)
    {
        const QStringList lines = QString::fromLatin1(head).split(QStringLiteral("\r\n"));
        const QStringList first = lines.value(0).split(u' ');
        if (first.size() < 2)
            return fail(400, "Bad Request");
        m_method = first.value(0);
        const QUrl url = QUrl::fromEncoded(first.value(1).toLatin1());
        m_path = url.path();
        m_query = QUrlQuery(url);
        for (int i = 1; i < lines.size(); ++i) {
            const int colon = lines[i].indexOf(u':');
            if (colon > 0)
                m_headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
        }
        m_device = DeviceLabel::from(m_headers.value(QStringLiteral("user-agent")));
        return true;
    }

    int authorize() { return m_server->check(m_ip, m_headers.value(QStringLiteral("x-pin"))); }
    bool fail(int code, const char *text, const QByteArray &body = {}, const QString &type = QStringLiteral("text/plain"))
    {
        reply(code, text, body.isEmpty() ? QByteArray(text) : body, type);
        return false;
    }
    void reply(int code, const char *text, const QByteArray &body, const QString &type = QStringLiteral("application/json"), const QByteArray &extra = {})
    {
        if (m_done)
            return;
        m_done = true;
        closeFile(true);
        QByteArray h = "HTTP/1.1 " + QByteArray::number(code) + " " + text + "\r\nContent-Type: " + type.toLatin1() + "\r\nContent-Length: " + QByteArray::number(body.size())
                       + "\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n" + extra + "\r\n";
        m_socket->write(h + body);
        m_socket->disconnectFromHost();
    }
    void json(int code, const char *text, const QJsonObject &o) { reply(code, text, QJsonDocument(o).toJson(QJsonDocument::Compact)); }

    // after the head: decide what this request is. false when it has been answered.
    bool begin()
    {
        const QString base = QLatin1Char('/') + m_server->m_token + QLatin1Char('/');
        if (!m_path.startsWith(base) && m_path != base.chopped(1))
            return fail(404, "Not Found");
        const QString rest = m_path.mid(base.size());
        const bool body = m_method == QLatin1String("PUT") || m_method == QLatin1String("POST");
        if (m_method == QLatin1String("GET") && rest.isEmpty()) {
            reply(200, "OK", m_server->page(), QStringLiteral("text/html; charset=utf-8"),
                  "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; img-src data:; form-action 'none'; frame-ancestors 'none'\r\n");
            return false;
        }
        if (m_method == QLatin1String("GET") && rest == QLatin1String("def")) {
            if (const int status = authorize(); status != 200)
                return fail(status, status == 429 ? "Too Many Requests" : "Forbidden");
            reply(200, "OK", m_server->definition());
            return false;
        }
        if (m_method == QLatin1String("GET") && rest == QLatin1String("avail")) { // the choices that have taken all they may, for the page to switch off
            if (const int status = authorize(); status != 200)
                return fail(status, status == 429 ? "Too Many Requests" : "Forbidden");
            reply(200, "OK", QJsonDocument(QJsonObject{{QStringLiteral("full"), m_server->fullChoices()}}).toJson(QJsonDocument::Compact));
            return false;
        }
        if (m_method == QLatin1String("GET") && rest == QLatin1String("mine")) {
            if (const int status = authorize(); status != 200)
                return fail(status, status == 429 ? "Too Many Requests" : "Forbidden");
            QByteArray out;
            const int code = m_server->mine(m_query.queryItemValue(QStringLiteral("e")), &out);
            reply(code, code == 200 ? "OK" : "Not Found", out);
            return false;
        }
        if (!body || (rest != QLatin1String("up") && rest != QLatin1String("submit")))
            return fail(404, "Not Found");
        if (const int status = authorize(); status != 200)
            return fail(status, status == 429 ? "Too Many Requests" : "Forbidden");
        bool okLength = false;
        m_length = m_headers.value(QStringLiteral("content-length")).toLongLong(&okLength);
        if (!okLength || m_length < 0)
            return fail(411, "Length Required");
        if (m_server->m_opensAt > QDateTime::currentSecsSinceEpoch()) { // scheduled: not yet
            json(425, "Too Early", {{QStringLiteral("error"), QStringLiteral("not_open")}, {QStringLiteral("message"), QStringLiteral("This form is not open yet.")},
                                    {QStringLiteral("opens_at"), double(m_server->m_opensAt)}});
            return false;
        }
        // somebody changing an answer they sent is not a new person (the token in the link proves which answer it is)
        if (m_query.queryItemValue(QStringLiteral("replace")) == QLatin1String("1")) {
            if (!m_server->knowsEdit(m_query.queryItemValue(QStringLiteral("e")))) {
                json(404, "Not Found", {{QStringLiteral("error"), QStringLiteral("not_found")}, {QStringLiteral("message"), QStringLiteral("This answer cannot be changed any more.")}});
                return false;
            }
            m_replace = true;
        }
        if (!m_replace && !m_server->admit(m_ip, m_device)) {
            json(423, "Locked", {{QStringLiteral("error"), QStringLiteral("people")}, {QStringLiteral("message"), QStringLiteral("The people allowed have already started.")}});
            return false;
        }
        if (rest == QLatin1String("submit")) {
            m_kind = Submit;
            if (m_length > MaxSubmit)
                return fail(413, "Payload Too Large");
            return true;
        }
        m_kind = Upload;
        const qint64 cap = qint64(m_server->m_form.limits.maxFileMb) * 1048576;
        if (m_length > cap) {
            json(413, "Payload Too Large", {{QStringLiteral("error"), QStringLiteral("too_big")}, {QStringLiteral("message"), QStringLiteral("This file is bigger than %1 MB.").arg(m_server->m_form.limits.maxFileMb)}});
            return false;
        }
        const QString field = m_query.queryItemValue(QStringLiteral("field"));
        bool known = false;
        for (const Forms::Field &f : std::as_const(m_server->m_form.fields))
            known = known || (f.id == field && Forms::takesFiles(f));
        if (!known)
            return fail(400, "Bad Request", "No such file question");
        // what this device already holds in waiting: not more than the whole answer may be, three times over
        qint64 held = 0;
        for (const FormServer::Pending &p : std::as_const(m_server->m_pending))
            if (p.ip == m_ip)
                held += p.size;
        if (held + m_length > qint64(m_server->m_form.limits.maxTotalMb) * 1048576 * 3) {
            json(413, "Payload Too Large", {{QStringLiteral("error"), QStringLiteral("too_much")}, {QStringLiteral("message"), QStringLiteral("Too many files are waiting from this device.")}});
            return false;
        }
        QByteArray raw(12, 0);
        QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(raw.data()), raw.size() / 4);
        m_uploadId = QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
        m_file.setFileName(m_server->m_tempDir + QLatin1Char('/') + m_uploadId + QStringLiteral(".part"));
        if (!m_file.open(QIODevice::WriteOnly))
            return fail(500, "Internal Server Error");
        m_field = field;
        m_name = m_query.queryItemValue(QStringLiteral("name"), QUrl::FullyDecoded);
        return true;
    }

    // the body, as it arrives
    void feed(const QByteArray &data)
    {
        if (m_done)
            return;
        m_got += data.size();
        if (m_got > m_length)
            return (void)fail(400, "Bad Request", "Too much data");
        if (m_kind == Upload) {
            if (m_file.write(data) != data.size())
                return (void)fail(500, "Internal Server Error");
        } else {
            m_submit += data;
        }
        if (m_got < m_length)
            return;
        if (m_kind == Upload) {
            const qint64 size = m_file.size();
            m_file.close();
            m_server->m_pending.insert(m_uploadId, {m_ip, m_field, m_name, m_file.fileName(), size});
            m_complete = true;
            json(200, "OK", {{QStringLiteral("id"), m_uploadId}, {QStringLiteral("size"), double(size)}});
        } else {
            QByteArray out;
            const int code = m_server->submit(m_ip, m_device, m_submit, &out, m_replace);
            reply(code, code == 201 ? "Created" : code == 200 ? "OK" : code == 404 ? "Not Found" : code == 409 ? "Conflict" : code == 429 ? "Too Many Requests" : code == 410 ? "Gone" : "Bad Request", out);
        }
    }

    void closeFile(bool remove)
    {
        if (m_file.isOpen())
            m_file.close();
        if (remove && m_kind == Upload && !m_complete && !m_file.fileName().isEmpty())
            QFile::remove(m_file.fileName());
    }
    void onGone()
    {
        if (!m_complete && m_kind == Upload)
            closeFile(true); // the visitor gave up halfway: nothing is kept
        m_socket->deleteLater();
    }

    enum Kind { None, Upload, Submit };
    FormServer *m_server;
    QTcpSocket *m_socket;
    QString m_ip, m_device, m_method, m_path, m_field, m_name, m_uploadId;
    QUrlQuery m_query;
    QHash<QString, QString> m_headers;
    QByteArray m_buf, m_body, m_submit;
    QFile m_file;
    QTimer m_idle;
    qint64 m_length = 0, m_got = 0;
    Kind m_kind = None;
    bool m_headDone = false, m_done = false, m_complete = false, m_replace = false;
};

FormServer::FormServer(FormStore *store, QObject *parent) : QObject(parent), m_store(store)
{
    connect(&m_server, &QTcpServer::newConnection, this, &FormServer::onConnection);
    m_expire.setSingleShot(true);
    connect(&m_expire, &QTimer::timeout, this, [this] { finish(QStringLiteral("expired")); });
}

QString FormServer::start(const QHostAddress &address, const Forms::Form &form, const QString &pin)
{
    stop();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (const QString why = Forms::validateForPublish(form, now); !why.isEmpty())
        return why;
    if (!FormStore::validId(form.id))
        return tr("This form has no valid id");
    m_tempDir = m_store->tempDir(form.id);
    if (m_tempDir.isEmpty())
        return tr("Cannot make a place for the files");
    if (!m_server.listen(address, 0))
        return m_server.errorString();
    QByteArray raw(16, 0);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(raw.data()), raw.size() / 4);
    m_token = QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    m_form = form;
    m_pin = pin;
    m_responses = m_store->responseCount(form.id);
    m_people.clear();
    m_answered.clear();
    m_pending.clear();
    m_fails.clear();
    m_sent.clear();
    m_url = QUrl(QStringLiteral("http://%1:%2/%3/").arg(address.toString()).arg(m_server.serverPort()).arg(m_token));
    const int ttl = Forms::effectiveTtl(form, now);
    m_expiresAt = now + ttl;
    m_opensAt = form.limits.opensAt;
    m_expire.start(qMin<qint64>(ttl, 24 * 3600 * 90) * 1000);
    if (m_responses >= form.limits.maxResponses) { // already full: no point in a link
        m_server.close();
        return tr("This form already has all the answers it takes");
    }
    return {};
}

void FormServer::stop() { finish(QStringLiteral("stopped")); }

void FormServer::onConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection())
        new FormConnection(this, socket);
}

void FormServer::finish(const QString &reason)
{
    if (!m_server.isListening())
        return;
    m_server.close();
    m_expire.stop();
    for (QTcpSocket *s : m_server.findChildren<QTcpSocket *>())
        if (reason != QLatin1String("full")) // let the last answer be told it arrived
            s->abort();
    for (const Pending &p : std::as_const(m_pending))
        QFile::remove(p.path); // files that were sent but never became an answer
    m_pending.clear();
    emit finished(reason);
}

bool FormServer::admit(const QString &ip, const QString &device)
{
    if (m_people.contains(ip))
        return true;
    if (m_form.limits.maxPeople > 0 && m_people.size() >= m_form.limits.maxPeople) {
        emit turnedAway(device);
        return false;
    }
    m_people.insert(ip);
    emit peopleChanged(int(m_people.size()));
    return true;
}

int FormServer::check(const QString &ip, const QString &pin)
{
    if (m_pin.isEmpty())
        return 200;
    auto &fail = m_fails[ip];
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (fail.second > now)
        return 429;
    const QByteArray a = pin.toUtf8(), b = m_pin.toUtf8(); // the time taken must not say how much of a guess was right
    int diff = a.size() ^ b.size();
    for (int i = 0; i < a.size(); ++i)
        diff |= a.at(i) ^ b.at(i % qMax<int>(1, b.size()));
    if (diff == 0) {
        m_fails.remove(ip);
        return 200;
    }
    if (++fail.first >= 5)
        fail = {0, now + 600};
    return 403;
}

bool FormServer::rateOk(const QString &ip)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QList<qint64> &times = m_sent[ip];
    while (!times.isEmpty() && times.first() < now - 600)
        times.removeFirst();
    if (times.size() >= 30)
        return false;
    times.append(now);
    return true;
}

QJsonObject FormServer::fullChoices() const { return Forms::fullOptions(m_form, m_store->responses(m_form.id)); }

int FormServer::mine(const QString &editId, QByteArray *reply) const
{
    const int n = knowsEdit(editId) ? m_store->findByEdit(m_form.id, Forms::editHash(editId)) : 0;
    const QJsonObject doc = n > 0 ? m_store->response(m_form.id, n) : QJsonObject();
    if (doc.isEmpty()) {
        *reply = QJsonDocument(QJsonObject{{QStringLiteral("error"), QStringLiteral("not_found")}}).toJson(QJsonDocument::Compact);
        return 404;
    }
    *reply = QJsonDocument(QJsonObject{{QStringLiteral("answers"), doc.value(QStringLiteral("answers")).toObject()}}).toJson(QJsonDocument::Compact); // file questions hold their file names
    return 200;
}

QByteArray FormServer::definition() const { return QJsonDocument(QJsonObject{{QStringLiteral("form"), Forms::toJson(m_form)}}).toJson(QJsonDocument::Compact); }

QByteArray FormServer::page() const
{
    QFile f(QStringLiteral(":/share/form.html"));
    if (!f.open(QIODevice::ReadOnly))
        return "Notch form page missing";
    QJsonObject data{{QStringLiteral("mode"), QStringLiteral("local")},
                     {QStringLiteral("expires_at"), double(m_expiresAt)},
                     {QStringLiteral("now"), double(QDateTime::currentSecsSinceEpoch())}, {QStringLiteral("opens_at"), double(m_opensAt)}};
    if (m_pin.isEmpty())
        data.insert(QStringLiteral("form"), Forms::toJson(m_form));
    else
        data.insert(QStringLiteral("protection"), QStringLiteral("pin"));
    QByteArray json = QJsonDocument(data).toJson(QJsonDocument::Compact).replace('<', "\\u003c"); // no text can close the script element
    QByteArray html = f.readAll();
    static const QByteArray marker = R"(<script id="form-data" type="application/json">null</script>)";
    return html.replace(marker, R"(<script id="form-data" type="application/json">)" + json + "</script>");
}

int FormServer::submit(const QString &ip, const QString &device, const QByteArray &body, QByteArray *reply, bool replace)
{
    const auto say = [&](const QString &msg) { *reply = QJsonDocument(QJsonObject{{QStringLiteral("error"), QStringLiteral("invalid")}, {QStringLiteral("message"), msg}}).toJson(QJsonDocument::Compact); };
    if (!replace && m_responses >= m_form.limits.maxResponses) { // changing an answer already in takes no place
        say(tr("This form is full."));
        return 410;
    }
    if (!rateOk(ip)) {
        say(tr("Too many answers from this device. Try again later."));
        return 429;
    }
    if (!replace && m_form.oneAnswer && m_answered.contains(ip)) { // one from each device; changing the one sent is another matter
        *reply = QJsonDocument(QJsonObject{{QStringLiteral("error"), QStringLiteral("already")}, {QStringLiteral("message"), tr("You already answered this form.")}}).toJson(QJsonDocument::Compact);
        return 409;
    }
    QJsonParseError err;
    const QJsonObject o = QJsonDocument::fromJson(body, &err).object();
    if (err.error != QJsonParseError::NoError) {
        say(tr("The answer could not be read."));
        return 400;
    }
    QList<Forms::Upload> uploads;
    QStringList ids;
    for (const QJsonValue &v : o.value(QStringLiteral("uploads")).toArray()) {
        const QString id = v.toString();
        const auto it = m_pending.constFind(id);
        if (it == m_pending.cend() || it->ip != ip || ids.contains(id)) { // only the files this device sent, once each
            say(tr("A file is not there any more. Add it again."));
            return 400;
        }
        ids << id;
        uploads.append({it->field, it->name, it->path, it->size});
    }
    {   // a choice that has taken all it may is not taken any more (counted here, on the one thread that keeps the answers: nobody slips in between)
        const QJsonObject given = o.value(QStringLiteral("answers")).toObject();
        const bool limited = std::any_of(m_form.fields.begin(), m_form.fields.end(), [](const Forms::Field &x) { return !x.optionLimits.isEmpty(); });
        if (limited) {
            int own = 0; // the answer being changed does not count against itself
            if (replace && m_form.allowEdit && Forms::validEditId(o.value(QStringLiteral("edit")).toString()))
                own = m_store->findByEdit(m_form.id, Forms::editHash(o.value(QStringLiteral("edit")).toString()));
            const auto full = Forms::optionFull(m_form, given, Forms::optionCounts(m_form, m_store->responses(m_form.id), own));
            if (!full.first.isEmpty()) {
                QString label = full.second;
                *reply = QJsonDocument(QJsonObject{{QStringLiteral("error"), QStringLiteral("option_full")}, {QStringLiteral("field"), full.first}, {QStringLiteral("option"), full.second},
                                                   {QStringLiteral("message"), tr("\"%1\" is full. Choose another.").arg(label)}}).toJson(QJsonDocument::Compact);
                return 409;
            }
        }
    }
    const QString editId = o.value(QStringLiteral("edit")).toString();
    const QString hash = m_form.allowEdit && Forms::validEditId(editId) ? Forms::editHash(editId) : QString(); // no personal link when the form takes no changes
    if (replace) {
        const int at = hash.isEmpty() ? 0 : m_store->findByEdit(m_form.id, hash);
        if (at == 0) {
            *reply = QJsonDocument(QJsonObject{{QStringLiteral("error"), QStringLiteral("not_found")}, {QStringLiteral("message"), tr("This answer cannot be changed any more.")}}).toJson(QJsonDocument::Compact);
            return 404;
        }
        QJsonObject kept;
        if (const QJsonObject doc = m_store->response(m_form.id, at); !doc.isEmpty())
            kept = doc.value(QStringLiteral("answers")).toObject();
        QJsonObject again;
        if (const QString why = Forms::validateSubmission(m_form, o.value(QStringLiteral("answers")).toObject(), uploads, &again, kept); !why.isEmpty()) {
            say(why);
            return 400;
        }
        if (!m_store->replaceResponse(m_form.id, at, again, uploads, device)) {
            say(tr("The answer could not be saved. Try again."));
            return 500;
        }
        for (const QString &id : ids)
            m_pending.remove(id);
        *reply = QJsonDocument(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("n"), at}, {QStringLiteral("edited"), true}}).toJson(QJsonDocument::Compact);
        emit edited(at, Forms::nameOf(m_form, again), device);
        return 200;
    }
    QJsonObject clean;
    if (const QString why = Forms::validateSubmission(m_form, o.value(QStringLiteral("answers")).toObject(), uploads, &clean); !why.isEmpty()) {
        say(why);
        return 400;
    }
    if (!hash.isEmpty() && m_store->findByEdit(m_form.id, hash) > 0) {
        say(tr("This personal link is already used."));
        return 400;
    }
    const int n = m_store->addResponse(m_form.id, clean, uploads, device, hash);
    if (n == 0) {
        say(tr("The answer could not be saved. Try again."));
        return 500;
    }
    for (const QString &id : ids)
        m_pending.remove(id);
    m_answered.insert(ip);
    ++m_responses;
    *reply = QJsonDocument(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("n"), n}}).toJson(QJsonDocument::Compact);
    emit responded(n, Forms::nameOf(m_form, clean), device);
    if (m_responses >= m_form.limits.maxResponses)
        QTimer::singleShot(200, this, [this] { finish(QStringLiteral("full")); }); // after the reply has gone out
    return 201;
}
