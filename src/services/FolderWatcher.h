#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

// Reports files that newly appear in one directory, once they are complete: not a browser's
// partial download, and the size has stopped changing for `settleMs`. Files already there when
// watching starts are ignored. Not recursive.
class FolderWatcher : public QObject
{
    Q_OBJECT

public:
    explicit FolderWatcher(QObject *parent = nullptr);

    void watch(const QString &dir, int settleMs = 1500); // empty dir stops watching
    QString dir() const { return m_dir; }

signals:
    void fileReady(const QString &path);

private:
    void scan();
    void checkPending();

    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QString m_dir;
    QSet<QString> m_known;            // names already seen (reported or pre-existing)
    QHash<QString, qint64> m_pending; // name -> size at last check
};
