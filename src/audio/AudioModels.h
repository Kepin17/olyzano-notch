#pragma once

#include "AudioStore.h"
#include "SoundEvents.h"

#include <QAbstractListModel>
#include <QLocale>

// What the Audio page lists: the meetings, and the lines of one transcript. Updated row by row, so a list that is being scrolled or a line
// that is being edited is not rebuilt under the person's hands.
class MeetingListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { IdRole = Qt::UserRole + 1, TitleRole, DurationRole, DateRole, LanguageRole, SpeakersRole, StatusRole, SnippetRole, ProgressRole, StageRole, SourceRole, SegmentsRole };
    explicit MeetingListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{IdRole, "meetingId"}, {TitleRole, "title"}, {DurationRole, "durationText"}, {DateRole, "dateText"}, {LanguageRole, "language"}, {SpeakersRole, "speakers"},
                {StatusRole, "status"}, {SnippetRole, "snippet"}, {ProgressRole, "progress"}, {StageRole, "stage"}, {SourceRole, "source"}, {SegmentsRole, "segments"}};
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_rows.size())
            return {};
        const AudioStore::Meeting &m = m_rows.at(index.row());
        switch (role) {
        case IdRole: return m.id;
        case TitleRole: return m.title;
        case DurationRole: return m.duration > 0 ? Audio::clock(m.duration) : QString();
        case DateRole: return QLocale().toString(m.created.toLocalTime(), QLocale::ShortFormat);
        case LanguageRole: return m.language;
        case SpeakersRole: return m.speakers;
        case StatusRole: return m.status;
        case SnippetRole: return m.snippet;
        case ProgressRole: return m.id == m_busyId ? m_progress : (m.status == QLatin1String("completed") ? 1.0 : 0.0);
        case StageRole: return m.id == m_busyId ? m_stage : QString();
        case SourceRole: return m.source;
        case SegmentsRole: return m.segments;
        }
        return {};
    }
    void setAll(const QList<AudioStore::Meeting> &rows)
    {
        beginResetModel();
        m_rows = rows;
        endResetModel();
    }
    const QList<AudioStore::Meeting> &rows() const { return m_rows; }
    void setProgress(qint64 id, const QString &stage, double progress)
    {
        m_busyId = id;
        m_stage = stage;
        m_progress = progress;
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].id == id)
                emit dataChanged(index(i), index(i), {ProgressRole, StageRole, StatusRole});
    }
    void clearProgress()
    {
        const qint64 old = m_busyId;
        m_busyId = 0;
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].id == old)
                emit dataChanged(index(i), index(i), {ProgressRole, StageRole});
    }

private:
    QList<AudioStore::Meeting> m_rows;
    qint64 m_busyId = 0;
    QString m_stage;
    double m_progress = 0;
};

class TranscriptModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role { IdRole = Qt::UserRole + 1, StartRole, EndRole, StartTextRole, WhoRole, LabelRole, TextRole, OriginalRole, EditedRole, ApproxRole, SpeakerIndexRole, SameAsPrevRole };
    explicit TranscriptModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{IdRole, "segmentId"}, {StartRole, "start"}, {EndRole, "end"}, {StartTextRole, "startText"}, {WhoRole, "who"}, {LabelRole, "speakerLabel"}, {TextRole, "text"},
                {OriginalRole, "original"}, {EditedRole, "edited"}, {ApproxRole, "approx"}, {SpeakerIndexRole, "speakerIndex"}, {SameAsPrevRole, "sameAsPrev"}};
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_rows.size())
            return {};
        const AudioStore::SegmentRow &r = m_rows.at(index.row());
        switch (role) {
        case IdRole: return r.id;
        case StartRole: return r.start;
        case EndRole: return r.end;
        case StartTextRole: return Audio::clock(r.start);
        case WhoRole: return r.who();
        case LabelRole: return r.speakerLabel;
        case TextRole: return r.text();
        case OriginalRole: return r.original;
        case EditedRole: return r.isEdited();
        case ApproxRole: return r.approx;
        case SpeakerIndexRole: return m_labels.indexOf(r.speakerLabel);
        case SameAsPrevRole: return index.row() > 0 && m_rows.at(index.row() - 1).speakerLabel == r.speakerLabel; // the same person goes on: no new header
        }
        return {};
    }
    void setAll(const QList<AudioStore::SegmentRow> &rows)
    {
        beginResetModel();
        m_rows = rows;
        m_labels.clear();
        for (const AudioStore::SegmentRow &r : m_rows)
            if (r.speakerLabel != Audio::Sound::Speaker && !m_labels.contains(r.speakerLabel)) // a line that is music or noise is no speaker
                m_labels << r.speakerLabel;
        m_labels.sort();
        endResetModel();
    }
    const QList<AudioStore::SegmentRow> &rows() const { return m_rows; }
    int rowOf(qint64 id) const
    {
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].id == id)
                return i;
        return -1;
    }
    void replaceRow(int row, const AudioStore::SegmentRow &r)
    {
        m_rows[row] = r;
        emit dataChanged(index(row), index(row));
    }
    void removeRow(int row)
    {
        beginRemoveRows({}, row, row);
        m_rows.removeAt(row);
        endRemoveRows();
        if (row < m_rows.size()) // the one now in its place may follow someone else
            emit dataChanged(index(row), index(row), {SameAsPrevRole});
    }
    // names changed for one label: its rows are told
    void refreshSpeaker(const QString &label, const QString &name)
    {
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].speakerLabel == label) {
                m_rows[i].speakerName = name;
                emit dataChanged(index(i), index(i), {WhoRole});
            }
    }
    // The row playing at `seconds` (the last that has started), or -1
    int rowAt(double seconds) const
    {
        int found = -1;
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows[i].start <= seconds)
                found = i;
            else
                break;
        }
        return found;
    }

private:
    QList<AudioStore::SegmentRow> m_rows;
    QStringList m_labels;
};
