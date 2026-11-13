#pragma once

#include "audio/AudioController.h"
#include "services/TaskSuggest.h"

#include <QObject>
#include <QSet>
#include <QThreadPool>
#include <QVariantList>

#include <atomic>
#include <memory>

class NotchController;
class Tasks;

// Tasks the notch noticed (in a recording's notes, in a PDF) and offers on the Tasks page: Add puts one on the list, Dismiss forgets it for good.
// Nothing is added without Add. What is offered is kept in suggestions.json (and what was dismissed), so it is not asked twice.
class TaskSuggestController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)   // [{id, title, dueLabel, hasDue, source, quote, kind, ref, page, timestamp}]
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)     // "Looking for tasks in Tugas.pdf…", or what was found
    Q_PROPERTY(bool autoAdd READ autoAdd WRITE setAutoAdd NOTIFY changed) // put the tasks of a recording on the list at once (not only offer them)
    Q_PROPERTY(bool autoMeetings READ autoMeetings WRITE setAutoMeetings NOTIFY changed) // offer the tasks of a recording when its notes are made

public:
    TaskSuggestController(NotchController *notch, AudioController *audio, Tasks *tasks, QObject *parent = nullptr);
    ~TaskSuggestController() override;

    QVariantList list() const;
    int count() const { return int(m_list.size()); }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    bool autoMeetings() const { return m_autoMeetings; }
    void setAutoMeetings(bool on);
    bool autoAdd() const { return m_autoAdd; }
    void setAutoAdd(bool on);

    // The tasks of that recording straight onto the Tasks list: the ones that are on it already (or done) are left; {added, skipped}
    Q_INVOKABLE QVariantMap addMeeting(qlonglong id);
    Q_INVOKABLE void fromMeeting(qlonglong id);   // the action items of that recording's notes
    Q_INVOKABLE void findInPdf(const QString &itemId); // a PDF of the shelf: the language model reads it for things to do
    Q_INVOKABLE void accept(const QString &id);
    Q_INVOKABLE void dismiss(const QString &id);
    Q_INVOKABLE void acceptAll();
    Q_INVOKABLE void dismissAll();
    Q_INVOKABLE void cancel();

signals:
    void changed();

private:
    void offer(const QList<TaskSuggest::Suggestion> &found, const QString &what);
    void save() const;
    void load();
    static QString path();

    NotchController *m_notch;
    AudioController *m_audio;
    Tasks *m_tasks;
    QList<TaskSuggest::Suggestion> m_list;
    QSet<QString> m_dismissed;
    QThreadPool m_pool;
    std::atomic_bool m_cancel{false};
    bool m_busy = false, m_autoMeetings = true, m_autoAdd = false;
    QString m_status;
};
