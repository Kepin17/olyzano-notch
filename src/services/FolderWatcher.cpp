#include "FolderWatcher.h"

#include "services/FileService.h"

#include <QDir>
#include <QFileInfo>

FolderWatcher::FolderWatcher(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &FolderWatcher::scan);
    connect(&m_settle, &QTimer::timeout, this, &FolderWatcher::checkPending);
}

void FolderWatcher::watch(const QString &dir, int settleMs)
{
    if (!m_dir.isEmpty())
        m_watcher.removePath(m_dir);
    m_dir = dir;
    m_known.clear();
    m_pending.clear();
    m_settle.stop();
    if (dir.isEmpty() || !QDir(dir).exists())
        return;
    m_settle.setInterval(settleMs);
    for (const QString &name : QDir(dir).entryList(QDir::Files | QDir::Hidden))
        m_known.insert(name);
    m_watcher.addPath(dir);
}

void FolderWatcher::scan()
{
    const QStringList names = QDir(m_dir).entryList(QDir::Files | QDir::Hidden);
    const QSet<QString> present(names.cbegin(), names.cend());
    m_known.intersect(present); // forget deleted files, so a re-created name counts as new
    for (const QString &name : names) {
        if (m_known.contains(name) || m_pending.contains(name) || FileService::isPartialDownload(name))
            continue;
        m_pending.insert(name, -1);
    }
    if (!m_pending.isEmpty() && !m_settle.isActive())
        m_settle.start();
}

void FolderWatcher::checkPending()
{
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        const QFileInfo fi(m_dir + u'/' + it.key());
        if (!fi.exists()) { // renamed away (e.g. a partial file got its final name: that one is new)
            it = m_pending.erase(it);
            continue;
        }
        if (fi.size() == it.value() && fi.size() > 0) {
            m_known.insert(it.key());
            emit fileReady(fi.absoluteFilePath());
            it = m_pending.erase(it);
            continue;
        }
        it.value() = fi.size();
        ++it;
    }
    if (m_pending.isEmpty())
        m_settle.stop();
}
