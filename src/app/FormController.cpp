#include "FormController.h"

#include "NotchController.h"
#include "FormRelay.h"
#include "ShareController.h"
#include "platform/NotchPaths.h"
#include "services/ShareServer.h"

#include <QDateTime>
#include <QUrl>
#include <QRegularExpression>
#include <QImage>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace {
QVariantMap toMap(const QJsonObject &o) { return o.toVariantMap(); }
QJsonObject toObject(const QVariantMap &m) { return QJsonObject::fromVariantMap(m); }
}

FormController::FormController(NotchController *notch, ShareController *share, QObject *parent)
    : QObject(parent), m_notch(notch), m_share(share), m_store(NotchPaths::dataDir(QStringLiteral("forms")))
{
    for (const Forms::Form &f : m_store.list()) {
        if (f.status != QLatin1String("open"))
            continue;
        // a form on this network lived only while Notch ran; one on the user's server goes on without it, and is picked up again
        const QJsonObject saved = m_store.relay(f.id);
        bool resumed = false;
        if (!saved.isEmpty()) {
            auto *relay = new FormRelay(m_share->nam(), m_share->relayFor(saved.value(QStringLiteral("serverId")).toString()), &m_store, f.id, this);
            resumed = relay->resume();
            if (resumed)
                watch(f.id, relay);
            else
                relay->deleteLater();
        }
        if (!resumed) {
            m_store.removeRelay(f.id);
            setStatus(f.id, QStringLiteral("closed"));
        }
    }
}

void FormController::watch(const QString &id, FormRelay *relay)
{
    m_relays.insert(id, relay);
    connect(relay, &FormRelay::edited, this, [this, id](int n, const QString &name, const QString &device) { announceEdit(id, name, device); answered(id, n); });
    connect(relay, &FormRelay::changed, this, [this] { emit changed(); });
    connect(relay, &FormRelay::responded, this, [this, id](int n, const QString &name, const QString &device) {
        answered(id, n);
        bool good = false;
        const Forms::Form form = m_store.load(id, &good);
        const QString who = name.isEmpty() ? tr("Someone (%1)").arg(device) : name;
        m_notch->announce(tr("New answer"), tr("%1 answered \"%2\" (%3 of %4)").arg(who, form.title).arg(m_store.responseCount(id)).arg(form.limits.maxResponses), QStringLiteral("mail-message-new"));
        if (m_store.responseCount(id) >= form.limits.maxResponses) // the server has closed it: it takes no more
            QTimer::singleShot(0, this, [this, id] { close(id); });
        emit changed();
    });
    connect(relay, &FormRelay::rejected, this, [this, id](const QString &device) {
        bool good = false;
        const Forms::Form form = m_store.load(id, &good);
        m_notch->announce(tr("An answer was thrown away"), tr("%1 sent something that does not follow \"%2\"").arg(device, form.title), QStringLiteral("dialog-warning"));
    });
    connect(relay, &FormRelay::ended, this, [this, id](const QString &reason) {
        setStatus(id, QStringLiteral("closed"));
        bool good = false;
        const Forms::Form form = m_store.load(id, &good);
        m_notch->announce(tr("Form closed"), reason == QLatin1String("expired") ? tr("The link of \"%1\" has expired").arg(form.title) : tr("\"%1\" was closed on the server").arg(form.title), QStringLiteral("dialog-information"));
        emit changed();
    });
}

FormController::~FormController()
{
    for (FormServer *s : std::as_const(m_servers))
        s->stop(); // (a form on the user's server stays open there)
}

void FormController::setIndex(IndexFn index, std::function<void(const QString &)> forget)
{
    m_index = std::move(index);
    m_forget = std::move(forget);
    QTimer::singleShot(25000, this, [this] { reindexAll(); }); // what came in while Notch was closed
}

void FormController::reindexAll()
{
    for (const Forms::Form &f : m_store.list())
        indexForm(f.id);
}

// A passage for each answer ("Question: answer" lines; `page` is its number, `who` the name given), for the search box.
void FormController::indexForm(const QString &id)
{
    if (!m_index)
        return;
    bool ok = false;
    const Forms::Form f = m_store.load(id, &ok);
    if (!ok)
        return;
    QList<Knowledge::Chunk> chunks;
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(f.title.toUtf8());
    for (const Forms::Response &r : m_store.responses(id)) {
        const QString text = Forms::answerText(f, r);
        if (text.isEmpty())
            continue;
        chunks.append({r.n, -1, Forms::nameOf(f, r.answers), text});
        hash.addData(QByteArray::number(r.n) + ':' + QByteArray::number(r.edited) + ':' + text.toUtf8());
    }
    m_index(id, f.title, chunks, QString::fromLatin1(hash.result().toHex()));
}

void FormController::announceEdit(const QString &id, const QString &name, const QString &device)
{
    indexForm(id);
    bool good = false;
    const Forms::Form form = m_store.load(id, &good);
    const QString who = name.isEmpty() ? tr("Someone (%1)").arg(device) : name;
    m_notch->announce(tr("Answer changed"), tr("%1 changed their answer to \"%2\"").arg(who, form.title), QStringLiteral("mail-message-new"));
    emit changed();
}

QVariantMap FormController::summary(const Forms::Form &f) const
{
    FormServer *s = m_servers.value(f.id);
    FormRelay *relay = m_relays.value(f.id);
    if (relay && relay->active())
        return {{QStringLiteral("id"), f.id}, {QStringLiteral("title"), f.title}, {QStringLiteral("status"), QStringLiteral("open")}, {QStringLiteral("answers"), m_store.responseCount(f.id)},
                {QStringLiteral("max"), f.limits.maxResponses}, {QStringLiteral("running"), true}, {QStringLiteral("url"), relay->url()}, {QStringLiteral("people"), relay->people()},
                {QStringLiteral("expiresAt"), double(relay->expiresAt())}, {QStringLiteral("questions"), int(std::count_if(f.fields.begin(), f.fields.end(), [](const Forms::Field &x) { return Forms::isQuestion(x); }))}, {QStringLiteral("mode"), QStringLiteral("relay")},
                {QStringLiteral("opensAt"), double(f.limits.opensAt)}, {QStringLiteral("closesAt"), double(f.limits.closesAt)},
                {QStringLiteral("maxBytes"), double(relay->maxBytes())}};
    const bool running = s && s->running();
    return {{QStringLiteral("id"), f.id}, {QStringLiteral("title"), f.title}, {QStringLiteral("status"), running ? QStringLiteral("open") : f.status == QLatin1String("open") ? QStringLiteral("closed") : f.status},
            {QStringLiteral("answers"), m_store.responseCount(f.id)}, {QStringLiteral("max"), f.limits.maxResponses}, {QStringLiteral("running"), running},
            {QStringLiteral("url"), running ? s->url().toString() : QString()}, {QStringLiteral("people"), running ? s->people() : 0},
            {QStringLiteral("expiresAt"), running ? double(s->expiresAt()) : 0.0},
            {QStringLiteral("questions"), int(std::count_if(f.fields.begin(), f.fields.end(), [](const Forms::Field &x) { return Forms::isQuestion(x); }))},
            {QStringLiteral("opensAt"), double(f.limits.opensAt)}, {QStringLiteral("closesAt"), double(f.limits.closesAt)}};
}

QVariantList FormController::list() const
{
    QVariantList out;
    for (const Forms::Form &f : m_store.list())
        out.append(summary(f));
    return out;
}

void FormController::setStatus(const QString &id, const QString &status)
{
    bool ok = false;
    Forms::Form f = m_store.load(id, &ok);
    if (!ok || f.status == status)
        return;
    f.status = status;
    m_store.save(f);
}

QVariantMap FormController::newForm(const QString &template_) { return toMap(Forms::toJson(Forms::templateFor(template_))); }

QVariantMap FormController::newField(const QString &type) const
{
    Forms::Field f = Forms::field(type, QString());
    if (type == QLatin1String("choice") || type == QLatin1String("multi"))
        f.options = {tr("Option 1"), tr("Option 2")};
    if (type == QLatin1String("file"))
        f.maxFiles = 3;
    return toMap(Forms::toJson([&] { Forms::Form x; x.fields = {f}; return x; }()).value(QStringLiteral("fields")).toArray().first().toObject());
}

QVariantMap FormController::loadForm(const QString &id) const
{
    bool ok = false;
    const Forms::Form f = m_store.load(id, &ok);
    return ok ? toMap(Forms::toJson(f)) : QVariantMap();
}

QString FormController::validate(const QVariantMap &form) const { return Forms::validateDefinition(Forms::fromJson(toObject(form))); }

bool FormController::saveForm(const QVariantMap &form)
{
    Forms::Form f = Forms::fromJson(toObject(form));
    if (f.id.isEmpty())
        f.id = Forms::newId();
    if (!f.created)
        f.created = QDateTime::currentSecsSinceEpoch();
    // text that is too long is cut here, so that what was typed never makes the file unreadable
    f.title = f.title.left(Forms::MaxTitle);
    f.description = f.description.left(Forms::MaxDescription);
    for (Forms::Field &x : f.fields) {
        x.label = x.label.left(Forms::MaxLabel);
        x.help = x.help.left(Forms::MaxHelp);
        if (x.id.isEmpty())
            x.id = QStringLiteral("q") + Forms::newId().left(6);
    }
    if ((m_servers.contains(f.id) && m_servers.value(f.id)->running()) || (m_relays.contains(f.id) && m_relays.value(f.id)->active()))
        return false; // not while it is open: the people filling it in see the form as it was
    if (!m_store.save(f))
        return false;
    indexForm(f.id);
    emit changed();
    return true;
}

bool FormController::removeForm(const QString &id)
{
    if (m_forget)
        m_forget(id);
    close(id);
    if (FormRelay *r = m_relays.take(id))
        r->deleteLater();
    if (FormServer *s = m_servers.take(id))
        s->deleteLater();
    const bool ok = m_store.remove(id);
    emit changed();
    return ok;
}

QStringList FormController::lanAddresses() const
{
    QStringList out;
    for (const QHostAddress &a : ShareServer::lanAddresses())
        out << a.toString();
    return out;
}

QString FormController::publish(const QString &id, const QString &address, const QString &pin)
{
    bool ok = false;
    Forms::Form f = m_store.load(id, &ok);
    if (!ok)
        return tr("This form cannot be found");
    if (!pin.isEmpty()) {
        static const QRegularExpression digits(QStringLiteral("^[0-9]{4,12}$"));
        if (!digits.match(pin).hasMatch())
            return tr("A PIN is 4 to 12 digits");
    }
    QHostAddress host;
    for (const QHostAddress &a : ShareServer::lanAddresses())
        if (a.toString() == address)
            host = a;
    if (host.isNull() && !ShareServer::lanAddresses().isEmpty())
        host = ShareServer::lanAddresses().first();
    if (host.isNull())
        return tr("No network connection found. Connect to WiFi first");
    FormServer *s = m_servers.value(id);
    if (!s) {
        s = new FormServer(&m_store, this);
        m_servers.insert(id, s);
        connect(s, &FormServer::responded, this, [this, id](int n, const QString &name, const QString &device) {
            bool good = false;
            const Forms::Form form = m_store.load(id, &good);
            const QString who = name.isEmpty() ? tr("Someone (%1)").arg(device) : name;
            m_notch->announce(tr("New answer"), tr("%1 answered \"%2\" (%3 of %4)").arg(who, form.title).arg(m_store.responseCount(id)).arg(form.limits.maxResponses), QStringLiteral("mail-message-new"));
            answered(id, n);
            emit changed();
        });
        connect(s, &FormServer::edited, this, [this, id](int n, const QString &name, const QString &device) { announceEdit(id, name, device); answered(id, n); });
        connect(s, &FormServer::peopleChanged, this, [this] { emit changed(); });
        connect(s, &FormServer::turnedAway, this, [this, id](const QString &device) {
            bool good = false;
            const Forms::Form form = m_store.load(id, &good);
            m_notch->announce(tr("Turned away"), tr("%1 came to \"%2\" after the people allowed had started").arg(device, form.title), QStringLiteral("dialog-warning"));
        });
        connect(s, &FormServer::finished, this, [this, id](const QString &reason) {
            setStatus(id, QStringLiteral("closed"));
            if (reason == QLatin1String("full") || reason == QLatin1String("expired")) {
                bool good = false;
                const Forms::Form form = m_store.load(id, &good);
                m_notch->announce(tr("Form closed"), reason == QLatin1String("full") ? tr("\"%1\" has all the answers it takes").arg(form.title) : tr("The link of \"%1\" has expired").arg(form.title),
                                  QStringLiteral("dialog-information"));
            }
            emit changed();
        });
    }
    if (const QString why = Forms::validateForPublish(f, QDateTime::currentSecsSinceEpoch()); !why.isEmpty())
        return why;
    if (const QString why = s->start(host, f, pin); !why.isEmpty())
        return why;
    setStatus(id, QStringLiteral("open"));
    emit changed();
    return {};
}

void FormController::close(const QString &id)
{
    if (FormServer *s = m_servers.value(id))
        s->stop();
    if (FormRelay *r = m_relays.value(id); r && r->active()) {
        r->close();
        setStatus(id, QStringLiteral("closed"));
        emit changed();
    }
}

QVariantList FormController::relayServers() const
{
    QVariantList out;
    for (const QVariant &v : m_share->servers()) {
        const QVariantMap s = v.toMap();
        out.append(QVariantMap{{QStringLiteral("id"), s.value(QStringLiteral("id"))}, {QStringLiteral("name"), s.value(QStringLiteral("name"))},
                               {QStringLiteral("ok"), s.value(QStringLiteral("ok")).toBool() && s.value(QStringLiteral("formsOk")).toBool()},
                               {QStringLiteral("old"), s.value(QStringLiteral("ok")).toBool() && !s.value(QStringLiteral("formsOk")).toBool()},
                               {QStringLiteral("schedule"), s.value(QStringLiteral("formsSchedule")).toBool()}, {QStringLiteral("edit"), s.value(QStringLiteral("formsEdit")).toBool()}, {QStringLiteral("once"), s.value(QStringLiteral("formsOnce")).toBool()}});
    }
    return out;
}

void FormController::publishRelay(const QString &id, const QString &serverId, const QString &pin)
{
    bool ok = false;
    const Forms::Form f = m_store.load(id, &ok);
    if (!ok) {
        emit publishFinished(id, tr("This form cannot be found"));
        return;
    }
    if (const QString why = Forms::validateForPublish(f, QDateTime::currentSecsSinceEpoch()); !why.isEmpty()) {
        emit publishFinished(id, why);
        return;
    }
    if (!pin.isEmpty()) {
        static const QRegularExpression digits(QStringLiteral("^[0-9]{4,12}$"));
        if (!digits.match(pin).hasMatch()) {
            emit publishFinished(id, tr("A PIN is 4 to 12 digits"));
            return;
        }
    }
    if (std::any_of(f.fields.begin(), f.fields.end(), [](const Forms::Field &x) { return !x.optionLimits.isEmpty(); })) { // the server cannot count what it cannot read
        emit publishFinished(id, tr("Limits per choice work on this WiFi only: through your server the answers are sealed and cannot be counted. Remove the limits or share on this WiFi."));
        return;
    }
    bool can = false, old = false, schedule = false, edits = false, once = false;
    for (const QVariant &v : relayServers()) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("id")).toString() == serverId || (serverId.isEmpty() && s.value(QStringLiteral("ok")).toBool())) {
            can = s.value(QStringLiteral("ok")).toBool();
            old = s.value(QStringLiteral("old")).toBool();
            schedule = s.value(QStringLiteral("schedule")).toBool();
            edits = s.value(QStringLiteral("edit")).toBool();
            once = s.value(QStringLiteral("once")).toBool();
        }
    }
    if (can && f.limits.opensAt > QDateTime::currentSecsSinceEpoch() && !schedule) { // an older server would open it at once
        emit publishFinished(id, tr("Your server needs an update to open a form later (see server/README.md). Or open it now."));
        return;
    }
    if (can && f.allowEdit && !edits) { // an older server would keep no copy to change an answer from
        emit publishFinished(id, tr("Your server needs an update so that people can change their answers (see server/README.md). Or turn that off."));
        return;
    }
    if (can && f.oneAnswer && !once) { // an older server would take as many as were sent
        emit publishFinished(id, tr("Your server needs an update to take one answer from each device (see server/README.md). Or allow many answers."));
        return;
    }
    if (!can) {
        emit publishFinished(id, old ? tr("Your server needs an update to take forms (see server/README.md)") : tr("That server cannot be reached or is not set up (Settings → Sharing)"));
        return;
    }
    if (FormRelay *old = m_relays.take(id))
        old->deleteLater();
    auto *relay = new FormRelay(m_share->nam(), m_share->relayFor(serverId), &m_store, id, this);
    connect(relay, &FormRelay::published, this, [this, id, relay](const QString &error) {
        if (error.isEmpty()) {
            watch(id, relay);
            setStatus(id, QStringLiteral("open"));
        } else {
            relay->deleteLater();
        }
        emit publishFinished(id, error);
        emit changed();
    });
    relay->publish(f, pin);
}

QString FormController::linkOf(const QString &id) const
{
    if (FormRelay *r = m_relays.value(id); r && r->active())
        return r->url();
    FormServer *s = m_servers.value(id);
    return s && s->running() ? s->url().toString() : QString();
}

QVariantMap FormController::responses(const QString &id) const
{
    bool ok = false;
    const Forms::Form f = m_store.load(id, &ok);
    if (!ok)
        return {};
    QVariantList columns, rows;
    for (const Forms::Field &x : f.fields)
        if (Forms::isQuestion(x))
            columns.append(QVariantMap{{QStringLiteral("id"), x.id}, {QStringLiteral("label"), x.label}, {QStringLiteral("type"), x.type}});
    for (const Forms::Response &r : m_store.responses(id)) {
        QVariantMap cells;
        for (const Forms::Field &x : f.fields)
            if (Forms::isQuestion(x))
                cells.insert(x.id, Forms::cellText(x, r.answers.value(x.id)));
        QVariantList files; // what came with the answer: name, where it is, whether it is a picture
        for (const QString &p : m_store.files(id, r.n))
            files.append(QVariantMap{{QStringLiteral("name"), QFileInfo(p).fileName()}, {QStringLiteral("url"), QUrl::fromLocalFile(p).toString()}, {QStringLiteral("path"), p},
                                     {QStringLiteral("image"), Forms::isImageName(p)}, {QStringLiteral("size"), double(QFileInfo(p).size())}});
        rows.append(QVariantMap{{QStringLiteral("n"), r.n}, {QStringLiteral("time"), QDateTime::fromSecsSinceEpoch(r.time).toString(QStringLiteral("yyyy-MM-dd HH:mm"))},
                                {QStringLiteral("device"), r.device}, {QStringLiteral("cells"), cells}, {QStringLiteral("files"), files},
                                {QStringLiteral("edited"), r.edited > 0 ? QDateTime::fromSecsSinceEpoch(r.edited).toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString()}});
    }
    return {{QStringLiteral("title"), f.title}, {QStringLiteral("columns"), columns}, {QStringLiteral("rows"), rows}};
}

QVariantMap FormController::summary(const QString &id) const
{
    bool ok = false;
    const Forms::Form f = m_store.load(id, &ok);
    return ok ? toMap(Forms::summarize(f, m_store.responses(id), QDate::currentDate())) : QVariantMap();
}

bool FormController::removeResponse(const QString &id, int n)
{
    const bool ok = m_store.removeResponse(id, n);
    indexForm(id);
    emit changed();
    return ok;
}

int FormController::exportCsv(const QString &id)
{
    bool ok = false;
    const Forms::Form f = m_store.load(id, &ok);
    if (!ok)
        return 0;
    const QString dir = m_notch->scratchPath(false) + QStringLiteral("/forms");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/answers-") + id + QStringLiteral(".csv");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return 0;
    file.write("\xEF\xBB\xBF" + Forms::toCsv(f, m_store.responses(id)).toUtf8()); // a byte-order mark: a spreadsheet reads the accents right
    file.close();
    const QString name = Forms::sanitizeFileName(f.title) + QStringLiteral(" answers.csv");
    if (m_notch->adoptLocalFile(path, name, tr("Answers of a form"), QString()).isEmpty())
        return 0;
    m_notch->announce(tr("Answers saved"), name, QStringLiteral("text-csv"));
    return 1;
}

// The files of one answer to the Files shelf: in a category named like the form, under tidy names; the ones already there are not sent twice.
int FormController::shelve(const QString &id, int n, bool always)
{
    bool ok = false;
    const Forms::Form form = m_store.load(id, &ok);
    if (!ok || (!always && !form.filesToShelf))
        return 0;
    const QList<Forms::Response> all = m_store.responses(id);
    const auto at = std::find_if(all.begin(), all.end(), [n](const Forms::Response &r) { return r.n == n; });
    if (at == all.end())
        return 0;
    const QStringList done = m_store.shelved(id, n);
    const QString dir = m_notch->scratchPath(false) + QStringLiteral("/forms");
    QDir().mkpath(dir);
    QString category;
    QStringList added;
    for (const QString &p : m_store.files(id, n)) {
        const QString kept = QFileInfo(p).fileName();
        if (done.contains(kept))
            continue;
        QString field; // which question it answers
        for (const Forms::Field &x : form.fields)
            if (Forms::takesFiles(x) && at->answers.value(x.id).toArray().contains(kept))
                field = x.id;
        if (category.isEmpty())
            category = m_notch->createCategory(form.title);
        const QString copy = dir + QStringLiteral("/") + QString::number(n) + QStringLiteral("-") + kept; // adopted files are moved: the answer keeps its own
        QFile::remove(copy);
        if (QFile::copy(p, copy) && !m_notch->adoptLocalFile(copy, Forms::shelfName(form, *at, field, kept), tr("Sent through a form"), category).isEmpty())
            added << kept;
    }
    m_store.addShelved(id, n, added);
    return int(added.size());
}

void FormController::answered(const QString &id, int n)
{
    indexForm(id);
    bool ok = false;
    const Forms::Form form = m_store.load(id, &ok);
    if (!ok || !form.filesToShelf)
        return;
    if (const int count = shelve(id, n, false); count > 0)
        m_notch->announce(tr("Files saved"), tr("%n file(s) from \"%1\" are in Files", nullptr, count).arg(form.title), QStringLiteral("document-save"));
}

int FormController::saveFiles(const QString &id, int n)
{
    int count = 0;
    QList<int> numbers;
    if (n > 0)
        numbers << n;
    else
        for (const Forms::Response &r : m_store.responses(id))
            numbers << r.n;
    for (int k : numbers)
        count += shelve(id, k, true);
    if (count > 0)
        m_notch->announce(tr("Files saved"), tr("%n file(s) are in Files", nullptr, count), QStringLiteral("document-save"));
    return count;
}

// A page of its own with the answers in a table and the pictures in it (small copies, inside the page), for looking at them without the app.
QString FormController::reportHtml(const QString &id) const
{
    bool ok = false;
    const Forms::Form f = m_store.load(id, &ok);
    if (!ok)
        return {};
    const auto esc = [](QString s) { return s.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")); };
    const QList<Forms::Response> rs = m_store.responses(id);
    qint64 embedded = 0; // the page stays a size a person can open: after about 40 MB the pictures are named, not shown
    QString html = QStringLiteral("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                                  "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; img-src data:; style-src 'unsafe-inline'\"><title>%1</title><style>"
                                  "body{font:15px/1.5 system-ui,sans-serif;margin:32px auto;max-width:1100px;padding:0 16px;color:#1d2330}h1{font-size:22px;margin:0 0 4px}.sub{color:#667;margin:0 0 20px}"
                                  "table{border-collapse:collapse;width:100%}th,td{border:1px solid #d9dde6;padding:8px 10px;vertical-align:top;text-align:left}th{background:#f4f6fa;font-size:13px}"
                                  ".n{white-space:nowrap;color:#667;font-size:12px}.pics{display:flex;flex-wrap:wrap;gap:6px}.pics figure{margin:0}.pics img{display:block;max-width:220px;max-height:220px;border-radius:6px}"
                                  ".pics figcaption,.file{font-size:12px;color:#667;word-break:break-all}</style></head><body><h1>%1</h1><p class=\"sub\">%2 answers</p><table><thead><tr><th>#</th>")
                       .arg(esc(f.title)).arg(rs.size());
    for (const Forms::Field &x : f.fields)
        if (Forms::isQuestion(x))
            html += QStringLiteral("<th>%1</th>").arg(esc(x.label));
    html += QStringLiteral("</tr></thead><tbody>");
    for (const Forms::Response &r : rs) {
        html += QStringLiteral("<tr><td class=\"n\">%1<br>%2</td>").arg(r.n).arg(esc(QDateTime::fromSecsSinceEpoch(r.time).toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        const QStringList paths = m_store.files(id, r.n);
        for (const Forms::Field &x : f.fields) {
            if (!Forms::isQuestion(x))
                continue;
            if (!Forms::takesFiles(x)) {
                html += QStringLiteral("<td>%1</td>").arg(esc(Forms::cellText(x, r.answers.value(x.id))));
                continue;
            }
            html += QStringLiteral("<td><div class=\"pics\">");
            for (const QJsonValue &v : r.answers.value(x.id).toArray()) {
                const QString name = v.toString();
                QString path;
                for (const QString &p : paths)
                    if (p.endsWith(QLatin1Char('/') + name))
                        path = p;
                QByteArray jpeg;
                QString mime = QStringLiteral("jpeg");
                if (!path.isEmpty() && Forms::isImageName(name) && QFileInfo(path).size() < 40 * 1048576 && embedded < 40 * 1048576) {
                    QImage img(path);
                    if (!img.isNull()) {
                        if (img.width() > 800 || img.height() > 800)
                            img = img.scaled(800, 800, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        QBuffer buf(&jpeg);
                        buf.open(QIODevice::WriteOnly);
                        if (!img.convertToFormat(QImage::Format_RGB32).save(&buf, "JPEG", 80)) { // no JPEG writer here: a PNG is bigger, and still a picture
                            jpeg.clear();
                            buf.seek(0);
                            if (img.save(&buf, "PNG"))
                                mime = QStringLiteral("png");
                        }
                    }
                }
                if (!jpeg.isEmpty()) {
                    embedded += jpeg.size();
                    html += QStringLiteral("<figure><img alt=\"%1\" src=\"data:image/%3;base64,%2\"><figcaption>%1</figcaption></figure>").arg(esc(name), QString::fromLatin1(jpeg.toBase64()), mime);
                } else {
                    html += QStringLiteral("<span class=\"file\">%1</span>").arg(esc(name));
                }
            }
            html += QStringLiteral("</div></td>");
        }
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</tbody></table></body></html>");
    return html;
}

int FormController::exportReport(const QString &id)
{
    bool ok = false;
    const Forms::Form f = m_store.load(id, &ok);
    const QString html = ok ? reportHtml(id) : QString();
    if (html.isEmpty())
        return 0;
    const QString dir = m_notch->scratchPath(false) + QStringLiteral("/forms");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/report-") + id + QStringLiteral(".html");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return 0;
    file.write(html.toUtf8());
    file.close();
    const QString name = Forms::sanitizeFileName(f.title) + QStringLiteral(" report.html");
    if (m_notch->adoptLocalFile(path, name, tr("Answers of a form"), QString()).isEmpty())
        return 0;
    m_notch->announce(tr("Report saved"), name, QStringLiteral("text-html"));
    return 1;
}

QString FormController::debugSample()
{
    Forms::Form f = Forms::templateFor(QStringLiteral("rsvp"));
    f.title = QStringLiteral("Team lunch");
    f.id = QStringLiteral("sample-lunch");
    f.status = QStringLiteral("closed");
    Forms::Field photo = Forms::field(QStringLiteral("file"), QStringLiteral("A photo"));
    photo.maxFiles = 3;
    f.fields.append(photo);
    m_store.remove(f.id);
    m_store.save(f);
    const QString name = f.fields[0].id, will = f.fields[1].id, count = f.fields[2].id, note = f.fields[3].id;
    const auto picture = [&](const QString &file, const QColor &color) { // a small picture to look at
        QImage img(480, 320, QImage::Format_RGB32);
        img.fill(color);
        const QString path = m_store.tempDir(f.id) + QLatin1Char('/') + file;
        img.save(path, "PNG");
        return Forms::Upload{photo.id, file, path, QFileInfo(path).size()};
    };
    m_store.addResponse(f.id, {{name, QStringLiteral("Budi")}, {will, QStringLiteral("Yes")}, {count, QStringLiteral("2")}, {note, QStringLiteral("Vegetarian for one, please.")}},
                        {picture(QStringLiteral("table.png"), QColor("#3b82c4")), picture(QStringLiteral("menu.png"), QColor("#e0a23b"))}, QStringLiteral("iPhone · Safari"));
    m_store.addResponse(f.id, {{name, QStringLiteral("Sari")}, {will, QStringLiteral("Maybe")}, {count, QString()}, {note, QString()}}, {}, QStringLiteral("Android · Chrome"));
    m_store.addResponse(f.id, {{name, QStringLiteral("Anton")}, {will, QStringLiteral("No")}, {count, QString()}, {note, QStringLiteral("Out of town.")}}, {}, QStringLiteral("Windows · Edge"));
    emit changed();
    return f.id;
}
