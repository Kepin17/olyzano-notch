#pragma once

#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <functional>

namespace Audio {

// A POST whose answer is handed over as it arrives (the language model writing, a piece at a time): `onBytes` is called with each piece, on this thread, while
// it waits. "" when it ended well, "cancelled" when `cancel` was set, else why not.
inline QString postJsonStreaming(const QUrl &url, const QByteArray &token, const QByteArray &body, int timeoutMs, const std::atomic_bool *cancel, const std::function<void(const QByteArray &)> &onBytes, int *status)
{
    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(0);
    if (!token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + token);
    QNetworkReply *r = nam.post(req, body);
    QEventLoop loop;
    QElapsedTimer clock;
    clock.start();
    QTimer poll;
    poll.setInterval(200);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if ((cancel && cancel->load()) || clock.elapsed() > timeoutMs)
            r->abort();
    });
    poll.start();
    QObject::connect(r, &QNetworkReply::readyRead, &loop, [&] { onBytes(r->readAll()); });
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (const QByteArray rest = r->readAll(); !rest.isEmpty())
        onBytes(rest);
    *status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool aborted = cancel && cancel->load();
    const QString err = aborted ? QStringLiteral("cancelled") : r->error() == QNetworkReply::NoError ? QString() : r->errorString();
    delete r;
    return err;
}

} // namespace Audio
