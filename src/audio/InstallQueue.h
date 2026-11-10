#pragma once

#include "ModelStore.h"

#include <QObject>
#include <QSet>
#include <QStringList>

// Downloads a list of catalog items through the ModelStore, a few at a time, and goes on by itself as each one ends. An item that fails (no
// room, no network, a wrong checksum) is noted and skipped; what was downloaded stays for "try again". Nothing starts but by start().
class InstallQueue : public QObject
{
    Q_OBJECT
public:
    static constexpr int Parallel = 2;
    explicit InstallQueue(ModelStore *store, QObject *parent = nullptr);

    bool active() const { return !m_pending.isEmpty() || !m_running.isEmpty(); }
    int total() const { return m_total; }
    int done() const { return m_done; }
    QStringList running() const { return m_running.values(); }
    const QStringList &pending() const { return m_pending; }
    const QStringList &failed() const { return m_failed; }

    void start(const QStringList &ids); // the ones that are not there yet and not already queued
    void cancel();                      // nothing more starts, and what runs is stopped (kept for the next try)

signals:
    void changed();
    void finished(); // the queue is empty: all done, or the rest failed or was stopped

private:
    void pump();
    void onChanged(const QString &id);

    ModelStore *m_store;
    QStringList m_pending, m_failed;
    QSet<QString> m_running, m_stopping;
    int m_total = 0, m_done = 0;
};
