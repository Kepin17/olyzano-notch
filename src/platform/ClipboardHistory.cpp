#include "ClipboardHistory.h"

#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusMessage>
#endif

using namespace ClipboardHistoryModel;

namespace {
QString baseFolder() { return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/notch"); }
QByteArray digest(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex(); }
}

ClipboardHistory::ClipboardHistory(QObject *parent)
    : QAbstractListModel(parent)
    , m_settings(baseFolder() + QStringLiteral("/clipboard.conf"), QSettings::IniFormat)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(600);
    connect(&m_saveTimer, &QTimer::timeout, this, &ClipboardHistory::save);
    m_expireTimer.setInterval(30 * 60 * 1000);
    connect(&m_expireTimer, &QTimer::timeout, this, &ClipboardHistory::expireNow);
    m_expireTimer.start();
    load();
    expireNow();
    rebuildView();
    connect(qApp, &QCoreApplication::aboutToQuit, this, &ClipboardHistory::save);
    listen(enabled());
}

QString ClipboardHistory::folder() const { return baseFolder() + QStringLiteral("/clipboard"); }

void ClipboardHistory::setEnabled(bool on)
{
    if (on == enabled())
        return;
    m_settings.setValue(QStringLiteral("enabled"), on);
    listen(on);
    emit settingsChanged();
}

void ClipboardHistory::setMaxAgeHours(int hours)
{
    hours = hours == 24 || hours == 0 ? hours : 168;
    if (hours == maxAgeHours())
        return;
    m_settings.setValue(QStringLiteral("maxAgeHours"), hours);
    expireNow();
    emit settingsChanged();
}

void ClipboardHistory::setFilter(const QString &f)
{
    if (f == m_filter)
        return;
    m_filter = f;
    rebuildView();
    emit filterChanged();
}

QHash<int, QByteArray> ClipboardHistory::roleNames() const
{
    return {{IdRole, "id"}, {ImageRole, "isImage"}, {TextRole, "text"}, {PreviewRole, "preview"},
            {UrlRole, "url"}, {TimeRole, "time"}, {PinnedRole, "pinned"}};
}

QVariant ClipboardHistory::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_view.size())
        return {};
    const Entry &e = m_all.at(m_view.at(index.row()));
    switch (role) {
    case IdRole: return e.id;
    case ImageRole: return e.image;
    case TextRole: return e.text;
    case PreviewRole: return e.image ? tr("Picture") : preview(e.text);
    case UrlRole: return e.image ? QUrl::fromLocalFile(folder() + u'/' + e.file) : QUrl();
    case TimeRole: return e.time;
    case PinnedRole: return e.pinned;
    }
    return {};
}

int ClipboardHistory::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_all.size(); ++i)
        if (m_all.at(i).id == id)
            return i;
    return -1;
}

void ClipboardHistory::rebuildView()
{
    beginResetModel();
    m_view.clear();
    for (int i = 0; i < m_all.size(); ++i)
        if (matches(m_all.at(i), m_filter))
            m_view.append(i);
    endResetModel();
    emit countChanged();
}

void ClipboardHistory::changedList()
{
    rebuildView();
    m_saveTimer.start();
}

void ClipboardHistory::listen(bool on)
{
    if (on == m_listening)
        return;
    m_listening = on;
    QClipboard *cb = QGuiApplication::clipboard();
    if (on) {
        connect(cb, &QClipboard::dataChanged, this, [this] { ingestMime(QGuiApplication::clipboard()->mimeData()); });
#ifdef Q_OS_LINUX
        // Plasma: Klipper sees every copy, even while Notch has no focus.
        QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.klipper"), QStringLiteral("/klipper"), QStringLiteral("org.kde.klipper.klipper"),
                                              QStringLiteral("clipboardHistoryUpdated"), this, SLOT(onKlipper()));
#endif
    } else {
        disconnect(cb, &QClipboard::dataChanged, this, nullptr);
#ifdef Q_OS_LINUX
        QDBusConnection::sessionBus().disconnect(QStringLiteral("org.kde.klipper"), QStringLiteral("/klipper"), QStringLiteral("org.kde.klipper.klipper"),
                                                 QStringLiteral("clipboardHistoryUpdated"), this, SLOT(onKlipper()));
#endif
    }
}

// A slot, for the D-Bus signal above (it is looked up by name).
#ifndef Q_OS_LINUX
void ClipboardHistory::onKlipper() {}
#else
void ClipboardHistory::onKlipper()
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.klipper"), QStringLiteral("/klipper"),
                                                       QStringLiteral("org.kde.klipper.klipper"), QStringLiteral("getClipboardHistoryItem"));
    call << 0;
    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        const QDBusPendingReply<QString> reply = *w;
        w->deleteLater();
        if (reply.isValid())
            ingestText(reply.value());
    });
}
#endif

void ClipboardHistory::ingestMime(const QMimeData *md)
{
    if (!md || !enabled())
        return;
    if (isSecret(md->data(QStringLiteral("x-kde-passwordManagerHint"))))
        return;
    if (md->hasImage()) {
        ingestImage(qvariant_cast<QImage>(md->imageData()));
        return;
    }
    if (md->hasText())
        ingestText(md->text());
}

void ClipboardHistory::ingestText(const QString &text)
{
    if (!enabled())
        return;
    const QByteArray bytes = text.toUtf8();
    const QByteArray hash = digest(bytes);
    m_ownCopy.clear(); // (what copy() put back is not new: add() moves the same content up)
    Entry e;
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.text = text;
    e.hash = hash;
    e.time = QDateTime::currentDateTimeUtc();
    if (add(m_all, e, bytes.size()))
        changedList();
}

void ClipboardHistory::ingestImage(const QImage &image)
{
    if (!enabled() || image.isNull())
        return;
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    if (!image.save(&buf, "PNG"))
        return;
    Entry e;
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.image = true;
    e.hash = digest(png);
    e.time = QDateTime::currentDateTimeUtc();
    e.file = e.id + QStringLiteral(".png");
    QDir().mkpath(folder());
    if (!tooBig(true, png.size()) && !QFile::exists(folder() + u'/' + e.file)) {
        QFile f(folder() + u'/' + e.file);
        if (f.open(QIODevice::WriteOnly))
            f.write(png);
    }
    if (add(m_all, e, png.size()))
        changedList();
    else
        QFile::remove(folder() + u'/' + e.file);
}

void ClipboardHistory::expireNow()
{
    const QList<Entry> gone = expire(m_all, maxAgeHours(), QDateTime::currentDateTimeUtc());
    for (const Entry &e : gone)
        if (e.image)
            QFile::remove(folder() + u'/' + e.file);
    if (!gone.isEmpty())
        changedList();
}

void ClipboardHistory::copy(const QString &id)
{
    const int i = indexOfId(id);
    if (i < 0)
        return;
    const Entry &e = m_all.at(i);
    m_ownCopy = e.hash;
    if (e.image)
        QGuiApplication::clipboard()->setImage(QImage(folder() + u'/' + e.file));
    else
        QGuiApplication::clipboard()->setText(e.text);
}

void ClipboardHistory::keep(const QString &id)
{
    const int i = indexOfId(id);
    if (i < 0)
        return;
    const Entry &e = m_all.at(i);
    const QString stamp = tr("Clipboard") + u' ' + e.time.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"));
    if (e.image) {
        QFile f(folder() + u'/' + e.file);
        if (f.open(QIODevice::ReadOnly))
            emit keepRequested(stamp + QStringLiteral(".png"), QStringLiteral("image/png"), f.readAll());
    } else {
        emit keepRequested(stamp + QStringLiteral(".txt"), QStringLiteral("text/plain"), e.text.toUtf8());
    }
}

void ClipboardHistory::pin(const QString &id, bool pinned)
{
    const int i = indexOfId(id);
    if (i < 0 || m_all.at(i).pinned == pinned)
        return;
    m_all[i].pinned = pinned;
    changedList();
}

void ClipboardHistory::remove(const QString &id)
{
    const int i = indexOfId(id);
    if (i < 0)
        return;
    if (m_all.at(i).image)
        QFile::remove(folder() + u'/' + m_all.at(i).file);
    m_all.removeAt(i);
    changedList();
}

void ClipboardHistory::clear()
{
    m_all.clear();
    QDir(folder()).removeRecursively();
#ifdef Q_OS_LINUX
    // Klipper keeps its own copy of the history: clearing here would leave it filled, so it is cleared there too.
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.klipper"), QStringLiteral("/klipper"),
                                                       QStringLiteral("org.kde.klipper.klipper"), QStringLiteral("clearClipboardHistory"));
    QDBusConnection::sessionBus().asyncCall(call);
#endif
    changedList();
}

void ClipboardHistory::load()
{
    QFile f(baseFolder() + QStringLiteral("/clipboard.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.id = o.value(QStringLiteral("id")).toString();
        e.image = o.value(QStringLiteral("image")).toBool();
        e.text = o.value(QStringLiteral("text")).toString();
        e.file = o.value(QStringLiteral("file")).toString();
        e.pinned = o.value(QStringLiteral("pinned")).toBool();
        e.time = QDateTime::fromSecsSinceEpoch(o.value(QStringLiteral("time")).toVariant().toLongLong(), Qt::UTC);
        e.hash = e.image ? o.value(QStringLiteral("hash")).toString().toLatin1() : digest(e.text.toUtf8());
        if (!e.id.isEmpty() && !e.hash.isEmpty() && (e.image ? !e.file.isEmpty() && QFile::exists(folder() + u'/' + e.file) : !e.text.isEmpty()))
            m_all.append(e);
    }
}

void ClipboardHistory::save()
{
    QJsonArray list;
    for (const Entry &e : std::as_const(m_all)) {
        QJsonObject o{{QStringLiteral("id"), e.id}, {QStringLiteral("time"), e.time.toSecsSinceEpoch()}, {QStringLiteral("pinned"), e.pinned}};
        if (e.image) {
            o.insert(QStringLiteral("image"), true);
            o.insert(QStringLiteral("file"), e.file);
            o.insert(QStringLiteral("hash"), QString::fromLatin1(e.hash));
        } else {
            o.insert(QStringLiteral("text"), e.text);
        }
        list.append(o);
    }
    QDir().mkpath(baseFolder());
    QFile f(baseFolder() + QStringLiteral("/clipboard.json"));
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner); // the history is private
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(list).toJson(QJsonDocument::Compact));
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
