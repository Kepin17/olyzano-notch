#pragma once

#include "ModelCatalog.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QElapsedTimer>
#include <QPointer>

class QFile;
class QNetworkReply;

// The files Audio Intelligence needs on disk: downloaded with the person's say-so, resumed where they stopped (HTTP Range), checked against
// their SHA-256, and for a helper program unpacked and found. State per item: missing | partial | downloading | verifying | ready.
// Files live under `root`: models/, helpers/<id>/, downloads/ (the .part files).
class ModelStore : public QObject
{
    Q_OBJECT
public:
    explicit ModelStore(const QString &root, QObject *parent = nullptr, const QList<Audio::CatalogItem> &items = Audio::catalog());

    QList<Audio::CatalogItem> items() const { return m_items; }
    Audio::CatalogItem item(const QString &id) const;
    QString root() const { return m_root; }
    QString state(const QString &id) const;
    // The model file, or the helper's program, when it is ready; else "".
    QString path(const QString &id) const;
    // A helper's folder (its libraries are next to the program): needed to start it.
    QString folder(const QString &id) const;
    double progress(const QString &id) const; // 0..1 while downloading
    qint64 partialBytes(const QString &id) const;
    double speed(const QString &id) const; // bytes per second right now while downloading (smoothed over a few seconds), else 0
    qint64 receivedBytes(const QString &id) const; // bytes of the file in hand while downloading (including what an earlier try kept)
    QString error(const QString &id) const { return m_error.value(id); }
    qint64 diskBytes(const QString &id) const; // what it takes on disk now

    void download(const QString &id);
    void cancel(const QString &id); // stops; what was downloaded is kept for the next try
    bool remove(const QString &id); // the file, the helper's folder and any partial download

signals:
    void changed(const QString &id);

private:
    QString finalPath(const Audio::CatalogItem &i) const;
    QString partPath(const Audio::CatalogItem &i) const;
    void fail(const QString &id, const QString &why);
    void verifyThenInstall(const QString &id);
    void setDownloading(const QString &id, bool on);

    struct Job {
        QPointer<QNetworkReply> reply;
        QFile *file = nullptr;
        qint64 start = 0, got = 0;
        bool cancelled = false;
        QElapsedTimer clock;     // for the speed: bytes seen at the last sample, and the smoothed rate
        qint64 sampleAt = 0, sampleGot = 0;
        double rate = 0;
    };
    QString m_root;
    QList<Audio::CatalogItem> m_items;
    QNetworkAccessManager m_net;
    QHash<QString, Job> m_jobs;
    QHash<QString, QString> m_error;
    QHash<QString, bool> m_verifying;
    QHash<QString, int> m_lastPercent;
};
