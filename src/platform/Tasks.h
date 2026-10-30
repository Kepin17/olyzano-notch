#pragma once

#include "platform/TasksModel.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>

// Your tasks and their deadlines, as `tasks` in QML: a list kept in notch/tasks.json, a heads-up 15 minutes before a
// deadline and a note when it passes (each once, even across restarts), and the next ones for the idle capsule radar.
class Tasks : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)      // [{id, title, course, done, hasDue, remaining, state}], in display order
    Q_PROPERTY(int openCount READ openCount NOTIFY changed)
    Q_PROPERTY(int overdueCount READ overdueCount NOTIFY changed)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY changed)        // an open task with a deadline exists
    Q_PROPERTY(QString nextTitle READ nextTitle NOTIFY changed)
    Q_PROPERTY(QString nextRemaining READ nextRemaining NOTIFY changed)
    Q_PROPERTY(QString nextState READ nextState NOTIFY changed) // none | later | critical | soon | overdue
    Q_PROPERTY(QStringList courseSuggestions READ courseSuggestions NOTIFY changed) // free-text courses already used, most used first

public:
    explicit Tasks(QObject *parent = nullptr);

    QVariantList list() const { return m_view; }
    int openCount() const;
    int overdueCount() const { return TasksModel::overdueCount(m_tasks, QDateTime::currentDateTime()); }
    bool hasNext() const { return TasksModel::nextOpen(m_tasks) >= 0; }
    QString nextTitle() const;
    QString nextRemaining() const;
    QString nextState() const;
    QStringList courseSuggestions() const;

    Q_INVOKABLE bool addQuick(const QString &line); // "Buy milk tomorrow 17:00": false when the line is empty
    Q_INVOKABLE bool add(const QString &title, const QString &dueIso, const QString &course = {}); // course free-text; "" = none
    Q_INVOKABLE void toggle(const QString &id);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void clearDone();
    Q_INVOKABLE void setCourse(const QString &id, const QString &course); // edit the course of one task
    // What `line` would become, for a hint under the field: "Buy milk · tomorrow 17:00".
    Q_INVOKABLE QString preview(const QString &line) const;

signals:
    void changed();
    void taskWarning(const QString &title); // 15 minutes to go
    void taskDue(const QString &title);     // the deadline

private:
    void tick();
    void refresh();
    void save() const;
    static QString stateString(TasksModel::DueState s);

    QList<TasksModel::Task> m_tasks;
    QVariantList m_view;
    QTimer m_tick;
    QString m_path;
};
