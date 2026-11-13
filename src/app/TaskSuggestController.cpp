#include "TaskSuggestController.h"

#include "NotchController.h"
#include "audio/AudioEngines.h"
#include "platform/NotchPaths.h"
#include "platform/ProcessRunner.h"
#include "platform/Tasks.h"
#include "services/Chunker.h"
#include "services/PdfText.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QSaveFile>
#include <QSettings>
#include <QtConcurrent>

namespace {
QSettings settings() { return QSettings(NotchPaths::conf(QStringLiteral("knowledge")), QSettings::IniFormat); }
}

QString TaskSuggestController::path() { return NotchPaths::dataDir() + QStringLiteral("/suggestions.json"); }

TaskSuggestController::TaskSuggestController(NotchController *notch, AudioController *audio, Tasks *tasks, QObject *parent) : QObject(parent), m_notch(notch), m_audio(audio), m_tasks(tasks)
{
    m_pool.setMaxThreadCount(1);
    m_autoMeetings = settings().value(QStringLiteral("suggestMeetings"), true).toBool();
    m_autoAdd = settings().value(QStringLiteral("addMeetings"), false).toBool();
    load();
    connect(audio, &AudioController::finished, this, [this](qlonglong id, bool ok) {
        if (ok && m_autoAdd)
            addMeeting(id);
        else if (ok && m_autoMeetings)
            fromMeeting(id);
    });
}

TaskSuggestController::~TaskSuggestController()
{
    m_cancel = true;
    m_pool.waitForDone(8000);
}

void TaskSuggestController::setAutoMeetings(bool on)
{
    if (on == m_autoMeetings)
        return;
    m_autoMeetings = on;
    { QSettings s = settings(); s.setValue(QStringLiteral("suggestMeetings"), on); }
    emit changed();
}

void TaskSuggestController::setAutoAdd(bool on)
{
    if (on == m_autoAdd)
        return;
    m_autoAdd = on;
    { QSettings s = settings(); s.setValue(QStringLiteral("addMeetings"), on); }
    emit changed();
}

QVariantMap TaskSuggestController::addMeeting(qlonglong id)
{
    QSet<QString> have;
    for (const QVariant &t : m_tasks->list())
        have.insert(TaskSuggest::normalizedTitle(t.toMap().value(QStringLiteral("title")).toString()));
    int added = 0, skipped = 0;
    for (const TaskSuggest::Suggestion &s : TaskSuggest::fromActions(id, m_audio->meetingTitle(id), m_audio->actionItemsOf(id), QDateTime::currentDateTime())) {
        for (int i = 0; i < m_list.size(); ++i) // it was offered: it is on the list now, so not offered any more
            if (m_list[i].id == s.id) {
                m_list.removeAt(i);
                break;
            }
        m_dismissed.insert(s.id);
        const QString key = TaskSuggest::normalizedTitle(s.title);
        if (have.contains(key)) {
            ++skipped;
            continue;
        }
        have.insert(key);
        m_tasks->add(s.title, s.due.isValid() ? s.due.toString(Qt::ISODate) : QString());
        ++added;
    }
    save();
    m_status = added > 0 ? tr("%n task(s) added to the list.", nullptr, added) : tr("No new tasks to add.");
    emit changed();
    return {{QStringLiteral("added"), added}, {QStringLiteral("skipped"), skipped}};
}

void TaskSuggestController::load()
{
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue &v : o.value(QStringLiteral("offered")).toArray())
        m_list.append(TaskSuggest::fromJson(v.toObject()));
    for (const QJsonValue &v : o.value(QStringLiteral("dismissed")).toArray())
        m_dismissed.insert(v.toString());
}

void TaskSuggestController::save() const
{
    QJsonArray offered, dismissed;
    for (const TaskSuggest::Suggestion &s : m_list)
        offered.append(TaskSuggest::toJson(s));
    for (const QString &d : m_dismissed)
        dismissed.append(d);
    QSaveFile f(path());
    if (f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(QJsonObject{{QStringLiteral("offered"), offered}, {QStringLiteral("dismissed"), dismissed}}).toJson(QJsonDocument::Compact)) >= 0)
        f.commit();
}

QVariantList TaskSuggestController::list() const
{
    QVariantList out;
    for (const TaskSuggest::Suggestion &s : m_list) {
        const bool meeting = s.kind == QLatin1String("meeting");
        QString source = meeting ? tr("Recording · %1").arg(s.sourceName) : tr("PDF · %1").arg(s.sourceName);
        if (meeting && s.timestamp >= 0)
            source += QStringLiteral(" · ") + Audio::clock(s.timestamp);
        else if (!meeting && s.page > 0)
            source += tr(" · page %1").arg(s.page);
        out.append(QVariantMap{{QStringLiteral("id"), s.id}, {QStringLiteral("title"), s.title}, {QStringLiteral("hasDue"), s.due.isValid()},
                               {QStringLiteral("dueLabel"), s.due.isValid() ? QLocale().toString(s.due, QLocale::ShortFormat) : s.dueText.isEmpty() ? QString() : s.dueText},
                               {QStringLiteral("source"), source}, {QStringLiteral("quote"), s.quote}, {QStringLiteral("kind"), s.kind}, {QStringLiteral("ref"), s.sourceRef}, {QStringLiteral("page"), s.page},
                               {QStringLiteral("timestamp"), s.timestamp}});
    }
    return out;
}

void TaskSuggestController::offer(const QList<TaskSuggest::Suggestion> &found, const QString &what)
{
    QStringList existing;
    for (const QVariant &t : m_tasks->list())
        existing << t.toMap().value(QStringLiteral("title")).toString();
    const QList<TaskSuggest::Suggestion> add = TaskSuggest::worthOffering(found, existing, m_dismissed, m_list, QDateTime::currentDateTime());
    m_list += add;
    m_status = add.isEmpty() ? tr("No new tasks in %1.").arg(what) : tr("%n task(s) found in %1.", nullptr, int(add.size())).arg(what);
    if (!add.isEmpty())
        save();
    emit changed();
}

void TaskSuggestController::fromMeeting(qlonglong id)
{
    offer(TaskSuggest::fromActions(id, m_audio->meetingTitle(id), m_audio->actionItemsOf(id), QDateTime::currentDateTime()), m_audio->meetingTitle(id));
}

void TaskSuggestController::cancel() { m_cancel = true; }

void TaskSuggestController::findInPdf(const QString &itemId)
{
    if (m_busy)
        return;
    const QVariantMap info = m_notch->documentInfo(itemId);
    if (!info.value(QStringLiteral("ok")).toBool())
        return;
    if (!Audio::LlamaSession::available(m_audio->modelStore())) {
        m_status = tr("Finding tasks needs the language model: download it in Settings → AI.");
        emit changed();
        return;
    }
    const QString file = info.value(QStringLiteral("path")).toString(), name = info.value(QStringLiteral("name")).toString();
    m_busy = true;
    m_cancel = false;
    m_status = tr("Looking for tasks in %1…").arg(name);
    emit changed();
    ModelStore *store = m_audio->modelStore();
    const int threads = m_audio->threads();
    (void)QtConcurrent::run(&m_pool, [this, file, name, itemId, store, threads] {
        QList<TaskSuggest::Suggestion> found;
        QString error;
        const auto res = ProcessRunner::run(QStringLiteral("pdftotext"), {QStringLiteral("-layout"), file, QStringLiteral("-")}, 180000);
        QStringList pages = res.ok ? QString::fromUtf8(res.out).split(u'\f') : QStringList();
        if (!pages.isEmpty() && pages.last().trimmed().isEmpty())
            pages.removeLast();
        if (pages.isEmpty())
            error = tr("No text could be read from %1.").arg(name);
        else {
            Audio::LlamaSession session(store, [threads] { return threads; });
            error = session.start(&m_cancel);
            if (error.isEmpty()) {
                const QDateTime now = QDateTime::currentDateTime();
                const Audio::ChatFn chat = [&](const QString &s, const QString &u, const QJsonObject &sc) { return session.chat(s, u, sc, &m_cancel, 700); };
                // two pages at a time: a task and its date are usually near each other
                for (int p = 0; p < pages.size() && p < 120 && !m_cancel.load(); p += 2) {
                    const QString window = pages.value(p) + QStringLiteral("\n") + pages.value(p + 1);
                    for (TaskSuggest::Suggestion s : TaskSuggest::findTasks(chat, window, p + 1, name, itemId, now)) {
                        if (DocSummary::quoteOnPage(s.quote, pages.value(p + 1)) && !DocSummary::quoteOnPage(s.quote, pages.value(p)))
                            s.page = p + 2; // the quote is on the second page of the two
                        found.append(s);
                    }
                }
            }
            session.stop();
        }
        if (m_cancel.load())
            error = QStringLiteral("cancelled");
        QMetaObject::invokeMethod(this, [this, found, error, name] {
            m_busy = false;
            if (error == QLatin1String("cancelled")) {
                m_status.clear();
                emit changed();
            } else if (!error.isEmpty()) {
                m_status = error;
                emit changed();
            } else {
                offer(found, name);
            }
        }, Qt::QueuedConnection);
    });
}

void TaskSuggestController::accept(const QString &id)
{
    for (int i = 0; i < m_list.size(); ++i)
        if (m_list[i].id == id) {
            const TaskSuggest::Suggestion s = m_list.takeAt(i);
            m_tasks->add(s.title, s.due.isValid() ? s.due.toString(Qt::ISODate) : QString());
            m_dismissed.insert(s.id); // on the list now: not offered again
            save();
            emit changed();
            return;
        }
}

void TaskSuggestController::dismiss(const QString &id)
{
    for (int i = 0; i < m_list.size(); ++i)
        if (m_list[i].id == id) {
            m_dismissed.insert(m_list.takeAt(i).id);
            save();
            emit changed();
            return;
        }
}

void TaskSuggestController::acceptAll()
{
    const QList<TaskSuggest::Suggestion> all = m_list;
    for (const TaskSuggest::Suggestion &s : all)
        accept(s.id);
}

void TaskSuggestController::dismissAll()
{
    for (const TaskSuggest::Suggestion &s : std::as_const(m_list))
        m_dismissed.insert(s.id);
    m_list.clear();
    save();
    emit changed();
}
