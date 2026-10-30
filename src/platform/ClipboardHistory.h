#pragma once

#include <QAbstractListModel>
#include <QSettings>
#include <QTimer>

#include "platform/ClipboardHistoryModel.h"

// What was copied lately, to copy again: text and pictures, newest first. Off until the person turns it on (a clipboard often holds
// a password); then it listens: through KDE's Klipper on Plasma (text, also while Notch is not in front: Wayland only lets the
// program in front read the clipboard) and through Qt's own clipboard signal elsewhere (Windows, X11) and for pictures.
// Sources that mark their copy as secret are not recorded. Kept on disk next to the other settings, with a limit and an age.
class ClipboardHistory : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int maxAgeHours READ maxAgeHours WRITE setMaxAgeHours NOTIFY settingsChanged) // 24, 168 or 0 (never)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)   // entries kept, whatever the search shows
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role { IdRole = Qt::UserRole + 1, ImageRole, TextRole, PreviewRole, UrlRole, TimeRole, PinnedRole };
    explicit ClipboardHistory(QObject *parent = nullptr);

    bool enabled() const { return m_settings.value(QStringLiteral("enabled"), false).toBool(); }
    void setEnabled(bool on);
    int maxAgeHours() const { return m_settings.value(QStringLiteral("maxAgeHours"), 168).toInt(); }
    void setMaxAgeHours(int hours);
    QString filter() const { return m_filter; }
    void setFilter(const QString &f);
    int total() const { return int(m_all.size()); }

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_view.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void copy(const QString &id);              // back onto the clipboard
    Q_INVOKABLE void keep(const QString &id);              // into the Files shelf (asks the controller, see keepRequested)
    Q_INVOKABLE void pin(const QString &id, bool pinned);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void clear();                              // everything, pinned too

signals:
    void settingsChanged();
    void filterChanged();
    void countChanged();
    void keepRequested(const QString &name, const QString &mimeType, const QByteArray &bytes);

private slots:
    void onKlipper(); // Klipper's clipboardHistoryUpdated (Linux; empty elsewhere)

private:
    void ingestMime(const class QMimeData *md);
    void ingestText(const QString &text);
    void ingestImage(const class QImage &image);
    void listen(bool on);
    void changedList();
    void rebuildView();
    void expireNow();
    void load();
    void save();
    QString folder() const;
    int indexOfId(const QString &id) const;

    QSettings m_settings;
    QList<ClipboardHistoryModel::Entry> m_all;
    QList<int> m_view; // indices into m_all that the search lets through
    QString m_filter;
    QTimer m_saveTimer, m_expireTimer;
    bool m_listening = false;
    QByteArray m_ownCopy; // the hash of what copy() just put back, so it is not recorded as new
};
