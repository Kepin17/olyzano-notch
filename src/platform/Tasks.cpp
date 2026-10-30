#include "platform/Tasks.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

using namespace TasksModel;

Tasks::Tasks(QObject *parent)
    : QObject(parent)
    , m_path(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/notch/tasks.json"))
{
    if (QFile file(m_path); file.open(QIODevice::ReadOnly))
        m_tasks = fromJson(file.readAll());
    m_tick.setInterval(20000); // reminders to the nearest 20 s is plenty for a deadline
    connect(&m_tick, &QTimer::timeout, this, &Tasks::tick);
    m_tick.start();
    QTimer::singleShot(3000, this, &Tasks::tick); // a deadline that passed while Notch was closed is settled once, quietly
    refresh();
}

void Tasks::save() const
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(toJson(m_tasks));
        file.commit();
    }
}

void Tasks::tick()
{
    const QList<Event> events = fire(m_tasks, QDateTime::currentDateTime());
    if (!events.isEmpty())
        save();
    refresh();
    for (const Event &e : events)
        emit e.now ? taskDue(e.title) : taskWarning(e.title);
}

QString Tasks::stateString(DueState s)
{
    switch (s) {
    case DueState::Overdue: return QStringLiteral("overdue");
    case DueState::Soon: return QStringLiteral("soon");
    case DueState::Critical: return QStringLiteral("critical");
    case DueState::Later: return QStringLiteral("later");
    case DueState::None: break;
    }
    return QStringLiteral("none");
}

void Tasks::refresh()
{
    const QDateTime now = QDateTime::currentDateTime();
    QVariantList view;
    for (const Task &t : sorted(m_tasks)) {
        const DueState s = dueState(t, now);
        view << QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("title"), t.title},
                            {QStringLiteral("course"), t.course}, {QStringLiteral("done"), t.done},
                            {QStringLiteral("hasDue"), t.due.isValid()},
                            {QStringLiteral("dueMs"), t.due.isValid() ? t.due.toMSecsSinceEpoch() : qint64(0)},
                            {QStringLiteral("remaining"), remaining(t.due, now)},
                            {QStringLiteral("state"), stateString(s)}};
    }
    m_view = view;
    emit changed();
}

int Tasks::openCount() const
{
    int n = 0;
    for (const Task &t : m_tasks)
        n += !t.done;
    return n;
}

QString Tasks::nextTitle() const
{
    const int i = nextOpen(m_tasks);
    return i >= 0 ? m_tasks[i].title : QString();
}

QString Tasks::nextRemaining() const
{
    const int i = nextOpen(m_tasks);
    return i >= 0 ? remaining(m_tasks[i].due, QDateTime::currentDateTime()) : QString();
}

QString Tasks::nextState() const
{
    const int i = nextOpen(m_tasks);
    if (i < 0)
        return QStringLiteral("none");
    return stateString(dueState(m_tasks[i], QDateTime::currentDateTime()));
}

QStringList Tasks::courseSuggestions() const
{
    QHash<QString, int> counts;
    for (const Task &t : m_tasks) {
        const QString c = t.course.trimmed();
        if (!c.isEmpty())
            ++counts[c];
    }
    QList<QPair<int, QString>> ranked; // count desc, then A-Z (stable for ties)
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        ranked << qMakePair(it.value(), it.key());
    std::sort(ranked.begin(), ranked.end(), [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
        return a.first != b.first ? a.first > b.first : a.second.compare(b.second, Qt::CaseInsensitive) < 0;
    });
    QStringList out;
    for (const auto &r : ranked)
        out << r.second;
    return out;
}

bool Tasks::add(const QString &title, const QString &dueIso, const QString &course)
{
    const QString c = course.trimmed();
    const QDateTime due = QDateTime::fromString(dueIso, Qt::ISODate).toLocalTime(); // from the picker (UTC "Z" form)
    if (!due.isValid()) {
        // No hand-picked deadline: same natural-language parse as the quick field.
        if (!addQuick(title))
            return false;
        if (!c.isEmpty() && !m_tasks.isEmpty()) {
            m_tasks.last().course = c;
            save();
            refresh();
        }
        return true;
    }
    if (title.trimmed().isEmpty())
        return false;
    Task t;
    t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    t.title = title.trimmed();
    t.course = c;
    t.due = due;
    m_tasks << t;
    save();
    tick();
    return true;
}

bool Tasks::addQuick(const QString &line)
{
    const Quick q = parseQuick(line, QDateTime::currentDateTime());
    if (q.title.trimmed().isEmpty())
        return false;
    Task t;
    t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    t.title = q.title;
    t.due = q.due;
    m_tasks << t;
    save();
    tick(); // a deadline in a minute should not wait for the next tick to be heard
    return true;
}

QString Tasks::preview(const QString &line) const
{
    const QDateTime now = QDateTime::currentDateTime();
    const Quick q = parseQuick(line, now);
    if (q.title.isEmpty())
        return {};
    return q.due.isValid() ? q.title + QStringLiteral(" · ") + remaining(q.due, now) : q.title;
}

void Tasks::toggle(const QString &id)
{
    for (Task &t : m_tasks)
        if (t.id == id)
            t.done = !t.done;
    save();
    refresh();
}

void Tasks::remove(const QString &id)
{
    m_tasks.removeIf([&](const Task &t) { return t.id == id; });
    save();
    refresh();
}

void Tasks::clearDone()
{
    m_tasks.removeIf([](const Task &t) { return t.done; });
    save();
    refresh();
}

void Tasks::setCourse(const QString &id, const QString &course)
{
    for (Task &t : m_tasks)
        if (t.id == id) {
            t.course = course.trimmed();
            break;
        }
    save();
    refresh();
}
