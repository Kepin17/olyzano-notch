#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

// Text cut into pieces that can be searched and read as the evidence of an answer: a page into passages of about 900 characters that overlap a
// little (a sentence that falls on a border is whole in one of them), a transcript into about a minute of talk with its time and its speakers.
// Pure.
namespace Knowledge {

struct Chunk {
    int page = 0;           // PDF page (1-based), 0 when it is not a page
    double startSec = -1;   // recording: where the passage begins, -1 when it is not a recording
    QString who;            // recording: who spoke first
    QString text;
};

inline QStringList sentences(const QString &text)
{
    static const QRegularExpression split(QStringLiteral(R"((?<=[.!?。！？])\s+|\n{2,})"));
    QStringList out;
    for (const QString &s : text.split(split, Qt::SkipEmptyParts)) {
        const QString t = s.simplified();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

inline QList<Chunk> chunkPage(int page, const QString &text, int target = 900, int overlap = 150)
{
    QList<Chunk> out;
    QStringList cur;
    int curLen = 0, fresh = 0; // fresh: sentences in `cur` that no earlier chunk has
    const auto begin = [&](const QStringList &prev) { // the next chunk starts with the last whole sentences of this one, up to `overlap` characters
        cur.clear();
        curLen = fresh = 0;
        QStringList tail;
        int len = 0;
        for (int i = int(prev.size()) - 1; i >= 0 && len + prev[i].size() <= overlap; --i) {
            tail.prepend(prev[i]);
            len += prev[i].size() + 1;
        }
        for (const QString &s : std::as_const(tail)) {
            cur << s;
            curLen += s.size() + 1;
        }
    };
    const auto flush = [&] {
        if (fresh <= 0)
            return;
        out.append({page, -1, QString(), cur.join(u' ')});
        const QStringList prev = cur; // a copy: begin() rebuilds `cur`
        begin(prev);
    };
    for (const QString &s : sentences(text)) {
        QStringList pieces; // a "sentence" with no end in sight (a table, a list) is cut
        for (QString rest = s; !rest.isEmpty();) {
            if (rest.size() > target * 2) {
                pieces << rest.left(target);
                rest = rest.mid(target);
            } else {
                pieces << rest;
                rest.clear();
            }
        }
        for (const QString &p : std::as_const(pieces)) {
            if (fresh > 0 && curLen + p.size() > target)
                flush();
            cur << p;
            curLen += p.size() + 1;
            ++fresh;
        }
    }
    flush();
    return out;
}

struct Utterance {
    double start = 0;
    QString who, text;
};

inline QList<Chunk> chunkTranscript(const QList<Utterance> &lines, double maxSeconds = 60, int maxChars = 1200)
{
    QList<Chunk> out;
    Chunk cur;
    double from = 0;
    QString last;
    const auto flush = [&] {
        if (!cur.text.isEmpty())
            out.append(cur);
        cur = {};
        last.clear();
    };
    for (const Utterance &u : lines) {
        const QString t = u.text.simplified();
        if (t.isEmpty())
            continue;
        if (!cur.text.isEmpty() && (u.start - from >= maxSeconds || cur.text.size() + t.size() > maxChars))
            flush();
        if (cur.text.isEmpty()) {
            cur.startSec = u.start;
            cur.who = u.who;
            from = u.start;
        }
        if (!u.who.isEmpty() && u.who != last) {
            cur.text += (cur.text.isEmpty() ? QString() : QStringLiteral("\n")) + u.who + QStringLiteral(": ");
            last = u.who;
        } else if (!cur.text.isEmpty()) {
            cur.text += u' ';
        }
        cur.text += t;
    }
    flush();
    return out;
}

} // namespace Knowledge
