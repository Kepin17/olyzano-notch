#pragma once

#include "Aligner.h"
#include "SummaryModel.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

// Where Audio Intelligence keeps its meetings: one SQLite file (audio.db). Meetings, who spoke, the transcript (the recognized text and the
// person's edit side by side), the summary with its decisions and action items, and the jobs that made them. Every change that touches
// several tables is one transaction; deleting a meeting removes everything of it. Blocking, on the GUI thread (small queries).
class AudioStore
{
public:
    struct Meeting {
        qint64 id = 0;
        QString title, originalFilename, audioPath, language, status = QStringLiteral("ready"), tmpl = QStringLiteral("meeting");
        QString quality = QStringLiteral("standard"), source = QStringLiteral("import"); // import | recording
        double duration = 0;
        int speakersHint = 0; // how many people there are, when the person said so (0 = find out from the voices)
        bool stereo = false; // microphone left, the computer's sound right
        QDateTime created, updated;
        // for the list
        QString snippet;
        int speakers = 0, segments = 0;
    };
    struct SegmentRow {
        qint64 id = 0;
        double start = 0, end = 0;
        QString speakerLabel, speakerName; // SPEAKER_00 and what the person called them ("" until renamed)
        QString original, edited;          // edited is "" until the person changes the text
        bool approx = false;
        QString text() const { return edited.isEmpty() ? original : edited; }
        QString who() const { return speakerName.isEmpty() ? speakerLabel : speakerName; }
        bool isEdited() const { return !edited.isEmpty() && edited != original; }
    };
    struct ActionRow {
        qint64 id = 0;
        Audio::ActionItem item;
    };
    struct Job {
        qint64 id = 0;
        QString stage, error, done; // done: the stages that finished, comma separated, in order
        double progress = 0;
        QDateTime created, completed;
    };

    AudioStore() = default;
    ~AudioStore();
    // Opens (and creates, and migrates) the database; a copy of the old file is kept as audio.db.bak-vN before a migration. "" on success.
    QString open(const QString &path);
    bool isOpen() const { return !m_conn.isEmpty(); }
    QString error() const { return m_error; }
    // A meeting that was being processed when Notch stopped is "failed" (its files stay, so it can be continued).
    int markInterrupted();

    qint64 addMeeting(const Meeting &m); // -> id, 0 on failure
    QList<Meeting> meetings(const QString &search = {}) const; // newest first; search looks in the title and the transcript
    Meeting meeting(qint64 id) const;
    bool updateMeeting(const Meeting &m); // title, language, duration, template, quality, status, stereo, audio path
    bool setStatus(qint64 id, const QString &status);
    bool deleteMeeting(qint64 id); // the rows only; the caller deletes the files it chooses to

    // The recognized transcript replaces the old one (names given to speakers with the same label are kept; edited text of the old one is not).
    bool replaceTranscript(qint64 meetingId, const QList<Audio::Segment> &segments);
    QList<SegmentRow> segments(qint64 meetingId) const;
    bool editSegment(qint64 segmentId, const QString &text); // "" or the original text = back to the original
    bool deleteSegment(qint64 segmentId);
    bool renameSpeaker(qint64 meetingId, const QString &label, const QString &name);
    QStringList speakerLabels(qint64 meetingId) const;

    bool saveSummary(qint64 meetingId, const Audio::Summary &s, const QString &tmpl);
    bool hasSummary(qint64 meetingId) const;
    Audio::Summary summary(qint64 meetingId, QList<qint64> *actionIds = nullptr) const;
    QList<ActionRow> actions(qint64 meetingId) const;
    bool setActionDone(qint64 actionId, bool done);

    qint64 startJob(qint64 meetingId);
    bool updateJob(qint64 jobId, const QString &stage, double progress, const QString &doneStages);
    bool finishJob(qint64 jobId, const QString &error); // "" = it completed
    Job lastJob(qint64 meetingId) const;
    struct HistoryRow {
        qint64 jobId = 0, meetingId = 0;
        QString title, error;
        QDateTime started, finished;
    };
    bool clearHistory(); // forgets the jobs that ended; the recordings stay
    QList<HistoryRow> history(int limit = 50) const; // finished and running jobs, newest first (a deleted meeting's jobs go with it)

private:
    bool exec(const QString &sql, const QVariantList &args = {}) const;
    QString m_conn, m_error;
};
