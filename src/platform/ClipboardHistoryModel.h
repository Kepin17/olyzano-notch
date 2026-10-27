#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

// What the clipboard history keeps, without any clipboard or disk so the tests can drive it: newest first, the same thing
// copied twice is one entry that moves up, a pinned entry stays, old entries expire, and what is too big is not kept.
namespace ClipboardHistoryModel {

constexpr int MaxEntries = 50;
constexpr int MaxTextBytes = 20 * 1024;
constexpr int MaxImageBytes = 5 * 1024 * 1024;

struct Entry {
    QString id;
    bool image = false;
    QString text;        // the text itself (kind text)
    QString file;        // the image file name inside the history folder (kind image)
    QByteArray hash;     // of the content, to tell the same thing copied again
    QDateTime time;
    bool pinned = false;
};

// One line of preview: whitespace folded, cut with an ellipsis.
inline QString preview(const QString &text, int max = 160)
{
    QString s = text.simplified();
    return s.size() > max ? s.left(max - 1) + QChar(0x2026) : s;
}

inline bool tooBig(bool image, qsizetype bytes) { return bytes > (image ? MaxImageBytes : MaxTextBytes); }

// Adds `e` (a new entry, or the same content again: that one moves to the top, keeping its pin). Returns false when the content
// is empty or too big and nothing was kept.
inline bool add(QList<Entry> &list, Entry e, qsizetype bytes)
{
    if (e.hash.isEmpty() || bytes <= 0 || tooBig(e.image, bytes) || (!e.image && e.text.trimmed().isEmpty()))
        return false;
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).hash == e.hash) {
            Entry same = list.takeAt(i);
            same.time = e.time;
            list.prepend(same);
            return true;
        }
    }
    list.prepend(e);
    // over the limit: the oldest that is not pinned goes
    while (list.size() > MaxEntries) {
        int drop = -1;
        for (int i = list.size() - 1; i >= 0; --i)
            if (!list.at(i).pinned) { drop = i; break; }
        if (drop < 0)
            break; // everything is pinned
        list.removeAt(drop);
    }
    return true;
}

// Entries older than `maxAgeHours` (0 = keep forever) that are not pinned. Returns the ones removed.
inline QList<Entry> expire(QList<Entry> &list, int maxAgeHours, const QDateTime &now)
{
    QList<Entry> gone;
    if (maxAgeHours <= 0)
        return gone;
    for (int i = list.size() - 1; i >= 0; --i) {
        if (!list.at(i).pinned && list.at(i).time.secsTo(now) > qint64(maxAgeHours) * 3600) {
            gone.append(list.takeAt(i));
        }
    }
    return gone;
}

// Case-insensitive match on the text (an image has none, so a search hides images).
inline bool matches(const Entry &e, const QString &query)
{
    const QString q = query.trimmed();
    return q.isEmpty() || (!e.image && e.text.contains(q, Qt::CaseInsensitive));
}

// Sources that ask not to be recorded say so with this hint (KDE's password manager convention).
inline bool isSecret(const QByteArray &passwordManagerHint) { return passwordManagerHint.trimmed() == "secret"; }

}
