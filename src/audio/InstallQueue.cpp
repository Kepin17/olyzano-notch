#include "InstallQueue.h"

#include <QTimer>

InstallQueue::InstallQueue(ModelStore *store, QObject *parent) : QObject(parent), m_store(store)
{
    connect(store, &ModelStore::changed, this, &InstallQueue::onChanged);
}

void InstallQueue::start(const QStringList &ids)
{
    if (!active()) { // a new round: the counts start again
        m_total = 0;
        m_done = 0;
        m_failed.clear();
        m_stopping.clear();
    }
    for (const QString &id : ids) {
        if (m_store->item(id).id.isEmpty() || m_store->state(id) == QLatin1String("ready") || m_running.contains(id) || m_pending.contains(id))
            continue;
        m_pending << id;
        ++m_total;
    }
    pump();
    emit changed();
}

void InstallQueue::cancel()
{
    m_total -= int(m_pending.size());
    m_pending.clear();
    for (const QString &id : std::as_const(m_running)) {
        m_stopping.insert(id);
        m_store->cancel(id);
    }
    emit changed();
    if (!active())
        emit finished();
}

void InstallQueue::pump()
{
    while (m_running.size() < Parallel && !m_pending.isEmpty()) {
        const QString id = m_pending.takeFirst();
        m_running.insert(id);
        m_store->download(id);
        const QString st = m_store->state(id);
        if (st != QLatin1String("downloading") && st != QLatin1String("verifying")) { // it did not start (no room, cannot write): or it is already there
            m_running.remove(id);
            if (st == QLatin1String("ready"))
                ++m_done;
            else
                m_failed << id;
        }
    }
}

void InstallQueue::onChanged(const QString &id)
{
    if (!m_running.contains(id))
        return;
    const QString st = m_store->state(id);
    if (st == QLatin1String("downloading") || st == QLatin1String("verifying")) {
        emit changed(); // progress
        return;
    }
    m_running.remove(id);
    if (st == QLatin1String("ready"))
        ++m_done;
    else if (!m_stopping.contains(id))
        m_failed << id;
    m_stopping.remove(id);
    pump();
    emit changed();
    if (!active())
        emit finished();
}
