#include "AudioStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include "AudioJson.h"

namespace {
// One entry per schema version. Never edit one that has shipped: add the next.
const QStringList Migrations{
    QStringLiteral(R"(
CREATE TABLE meetings(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    original_filename TEXT NOT NULL DEFAULT '',
    audio_path TEXT NOT NULL,
    duration REAL NOT NULL DEFAULT 0,
    language TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL DEFAULT 'ready' CHECK (status IN ('ready','queued','processing','completed','failed')),
    template TEXT NOT NULL DEFAULT 'meeting',
    quality TEXT NOT NULL DEFAULT 'standard',
    source TEXT NOT NULL DEFAULT 'import',
    stereo INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE TABLE speakers(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meeting_id INTEGER NOT NULL REFERENCES meetings(id) ON DELETE CASCADE,
    speaker_label TEXT NOT NULL,
    display_name TEXT NOT NULL DEFAULT '',
    UNIQUE (meeting_id, speaker_label)
);
CREATE TABLE transcript_segments(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meeting_id INTEGER NOT NULL REFERENCES meetings(id) ON DELETE CASCADE,
    speaker_id INTEGER REFERENCES speakers(id) ON DELETE SET NULL,
    start_time REAL NOT NULL,
    end_time REAL NOT NULL,
    original_text TEXT NOT NULL,
    edited_text TEXT NOT NULL DEFAULT '',
    approx INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX segments_by_meeting ON transcript_segments(meeting_id, start_time);
CREATE TABLE summaries(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meeting_id INTEGER NOT NULL UNIQUE REFERENCES meetings(id) ON DELETE CASCADE,
    executive_summary TEXT NOT NULL DEFAULT '',
    key_points TEXT NOT NULL DEFAULT '[]',
    open_questions TEXT NOT NULL DEFAULT '[]',
    notes TEXT NOT NULL DEFAULT '[]',
    template TEXT NOT NULL DEFAULT 'meeting',
    created_at TEXT NOT NULL
);
CREATE TABLE decisions(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meeting_id INTEGER NOT NULL REFERENCES meetings(id) ON DELETE CASCADE,
    description TEXT NOT NULL,
    timestamp REAL NOT NULL DEFAULT -1,
    quote TEXT NOT NULL DEFAULT '',
    needs_review INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE action_items(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meeting_id INTEGER NOT NULL REFERENCES meetings(id) ON DELETE CASCADE,
    task TEXT NOT NULL,
    assignee TEXT NOT NULL DEFAULT '',
    deadline TEXT NOT NULL DEFAULT '',
    priority TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','done')),
    timestamp REAL NOT NULL DEFAULT -1,
    quote TEXT NOT NULL DEFAULT '',
    needs_review INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE processing_jobs(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meeting_id INTEGER NOT NULL REFERENCES meetings(id) ON DELETE CASCADE,
    current_stage TEXT NOT NULL DEFAULT '',
    progress REAL NOT NULL DEFAULT 0,
    done_stages TEXT NOT NULL DEFAULT '',
    error_message TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL,
    completed_at TEXT NOT NULL DEFAULT ''
);
)"),
    // 2: how many people there are (when the person said so), the parts of a summary the first schema had no column for (topics, risks, next steps...),
    // and the reason and owner of a decision
    QStringLiteral(R"(
ALTER TABLE meetings ADD COLUMN speakers_hint INTEGER NOT NULL DEFAULT 0;
ALTER TABLE summaries ADD COLUMN extra TEXT NOT NULL DEFAULT '{}';
ALTER TABLE decisions ADD COLUMN rationale TEXT NOT NULL DEFAULT '';
ALTER TABLE decisions ADD COLUMN owner TEXT NOT NULL DEFAULT ''
)")};

// An empty QString is a null one for Qt, and a null binds as SQL NULL: columns that must hold text get "" instead.
QVariant bindable(const QVariant &v)
{
    return v.metaType().id() == QMetaType::QString && v.toString().isNull() ? QVariant(QString::fromLatin1("")) : v;
}

QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QDateTime when(const QString &s) { return QDateTime::fromString(s, Qt::ISODateWithMs); }

QString toJson(const QStringList &l) { return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(l)).toJson(QJsonDocument::Compact)); }
QStringList fromJson(const QString &s)
{
    QStringList out;
    for (const QJsonValue &v : QJsonDocument::fromJson(s.toUtf8()).array())
        out << v.toString();
    return out;
}
}

AudioStore::~AudioStore()
{
    if (m_conn.isEmpty())
        return;
    { QSqlDatabase::database(m_conn, false).close(); }
    QSqlDatabase::removeDatabase(m_conn);
}

bool AudioStore::exec(const QString &sql, const QVariantList &args) const
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(sql);
    for (const QVariant &a : args)
        q.addBindValue(bindable(a));
    return q.exec();
}

QString AudioStore::open(const QString &path)
{
    const QString conn = QStringLiteral("audio-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(path);
    if (!db.open()) {
        m_error = QStringLiteral("cannot open the audio database: ") + db.lastError().text();
        db.close();
        QSqlDatabase::removeDatabase(conn);
        return m_error;
    }
    m_conn = conn;
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    int version = 0;
    if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
        version = q.value(0).toInt();
    if (version > Migrations.size()) { // made by a newer Notch: do not touch it
        m_error = QStringLiteral("the audio database is from a newer version of Notch");
        db.close();
        QSqlDatabase::removeDatabase(conn);
        m_conn.clear();
        return m_error;
    }
    if (version > 0 && version < Migrations.size())
        QFile::copy(path, path + QStringLiteral(".bak-v") + QString::number(version));
    for (int i = version; i < Migrations.size(); ++i) {
        db.transaction();
        bool ok = true;
        for (const QString &stmt : Migrations.at(i).split(u';', Qt::SkipEmptyParts)) {
            if (stmt.trimmed().isEmpty())
                continue;
            if (!q.exec(stmt)) {
                ok = false;
                m_error = QStringLiteral("migration %1 failed: %2").arg(i + 1).arg(q.lastError().text());
                break;
            }
        }
        if (ok)
            ok = q.exec(QStringLiteral("PRAGMA user_version=%1").arg(i + 1));
        if (!ok) {
            db.rollback();
            db.close();
            QSqlDatabase::removeDatabase(conn);
            m_conn.clear();
            return m_error;
        }
        db.commit();
    }
    return {};
}

int AudioStore::markInterrupted()
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    if (!q.exec(QStringLiteral("SELECT id FROM meetings WHERE status IN ('processing','queued')")))
        return 0;
    QList<qint64> ids;
    while (q.next())
        ids << q.value(0).toLongLong();
    for (qint64 id : ids) {
        exec(QStringLiteral("UPDATE meetings SET status='failed', updated_at=? WHERE id=?"), {now(), id});
        exec(QStringLiteral("UPDATE processing_jobs SET error_message='Notch was closed while this was being processed', completed_at=? "
                            "WHERE meeting_id=? AND completed_at=''"), {now(), id});
    }
    return int(ids.size());
}

qint64 AudioStore::addMeeting(const Meeting &m)
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("INSERT INTO meetings(title, original_filename, audio_path, duration, language, status, template, quality, source, stereo, "
                             "created_at, updated_at, speakers_hint) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    const QString t = now();
    for (const QVariant &v : QVariantList{m.title, m.originalFilename, m.audioPath, m.duration, m.language, m.status, m.tmpl, m.quality, m.source,
                                          int(m.stereo), t, t, m.speakersHint})
        q.addBindValue(bindable(v));
    return q.exec() ? q.lastInsertId().toLongLong() : 0;
}

static AudioStore::Meeting meetingFrom(QSqlQuery &q)
{
    AudioStore::Meeting m;
    m.id = q.value(0).toLongLong();
    m.title = q.value(1).toString();
    m.originalFilename = q.value(2).toString();
    m.audioPath = q.value(3).toString();
    m.duration = q.value(4).toDouble();
    m.language = q.value(5).toString();
    m.status = q.value(6).toString();
    m.tmpl = q.value(7).toString();
    m.quality = q.value(8).toString();
    m.source = q.value(9).toString();
    m.stereo = q.value(10).toInt() != 0;
    m.created = when(q.value(11).toString());
    m.updated = when(q.value(12).toString());
    m.speakersHint = q.value(13).toInt();
    return m;
}

static const char *MeetingColumns = "id, title, original_filename, audio_path, duration, language, status, template, quality, source, stereo, created_at, updated_at, speakers_hint";

QList<AudioStore::Meeting> AudioStore::meetings(const QString &search) const
{
    QList<Meeting> out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    QString sql = QStringLiteral("SELECT %1, (SELECT substr(executive_summary,1,200) FROM summaries s WHERE s.meeting_id=meetings.id), "
                                 "(SELECT COUNT(*) FROM speakers p WHERE p.meeting_id=meetings.id AND p.speaker_label<>'SOUND'), "
                                 "(SELECT COUNT(*) FROM transcript_segments t WHERE t.meeting_id=meetings.id) FROM meetings").arg(QLatin1String(MeetingColumns));
    const QString like = QLatin1Char('%') + QString(search).replace(u'\\', QStringLiteral("\\\\")).replace(u'%', QStringLiteral("\\%")).replace(u'_', QStringLiteral("\\_")) + QLatin1Char('%');
    if (!search.trimmed().isEmpty())
        sql += QStringLiteral(" WHERE title LIKE :s ESCAPE '\\' OR id IN (SELECT meeting_id FROM transcript_segments WHERE original_text LIKE :s ESCAPE '\\' "
                              "OR edited_text LIKE :s ESCAPE '\\') OR id IN (SELECT meeting_id FROM summaries WHERE executive_summary LIKE :s ESCAPE '\\')");
    sql += QStringLiteral(" ORDER BY created_at DESC, id DESC");
    q.prepare(sql);
    if (!search.trimmed().isEmpty())
        q.bindValue(QStringLiteral(":s"), like);
    if (!q.exec())
        return out;
    while (q.next()) {
        Meeting m = meetingFrom(q);
        m.snippet = q.value(14).toString();
        m.speakers = q.value(15).toInt();
        m.segments = q.value(16).toInt();
        out.append(m);
    }
    return out;
}

AudioStore::Meeting AudioStore::meeting(qint64 id) const
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT %1 FROM meetings WHERE id=?").arg(QLatin1String(MeetingColumns)));
    q.addBindValue(id);
    return q.exec() && q.next() ? meetingFrom(q) : Meeting();
}

bool AudioStore::updateMeeting(const Meeting &m)
{
    return exec(QStringLiteral("UPDATE meetings SET title=?, audio_path=?, duration=?, language=?, status=?, template=?, quality=?, stereo=?, speakers_hint=?, updated_at=? WHERE id=?"),
                {m.title, m.audioPath, m.duration, m.language, m.status, m.tmpl, m.quality, int(m.stereo), m.speakersHint, now(), m.id});
}

bool AudioStore::setStatus(qint64 id, const QString &status)
{
    return exec(QStringLiteral("UPDATE meetings SET status=?, updated_at=? WHERE id=?"), {status, now(), id});
}

bool AudioStore::deleteMeeting(qint64 id)
{
    return exec(QStringLiteral("DELETE FROM meetings WHERE id=?"), {id});
}

bool AudioStore::replaceTranscript(qint64 meetingId, const QList<Audio::Segment> &segments)
{
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    // names the person already gave to a label survive a new transcript
    QHash<QString, QString> names;
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT speaker_label, display_name FROM speakers WHERE meeting_id=? AND display_name<>''"));
        q.addBindValue(meetingId);
        if (q.exec())
            while (q.next())
                names.insert(q.value(0).toString(), q.value(1).toString());
    }
    db.transaction();
    bool ok = exec(QStringLiteral("DELETE FROM transcript_segments WHERE meeting_id=?"), {meetingId})
              && exec(QStringLiteral("DELETE FROM speakers WHERE meeting_id=?"), {meetingId});
    QHash<QString, qint64> ids;
    for (const Audio::Segment &s : segments) {
        if (!ok)
            break;
        const QString label = s.speaker.isEmpty() ? QStringLiteral("SPEAKER_00") : s.speaker;
        if (!ids.contains(label)) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("INSERT INTO speakers(meeting_id, speaker_label, display_name) VALUES(?,?,?)"));
            q.addBindValue(meetingId);
            q.addBindValue(label);
            q.addBindValue(bindable(names.value(label)));
            ok = q.exec();
            ids.insert(label, q.lastInsertId().toLongLong());
        }
        ok = ok && exec(QStringLiteral("INSERT INTO transcript_segments(meeting_id, speaker_id, start_time, end_time, original_text, approx) VALUES(?,?,?,?,?,?)"),
                        {meetingId, ids.value(label), s.start, s.end, s.text, int(s.approx)});
    }
    if (!ok) {
        db.rollback();
        return false;
    }
    return db.commit();
}

QList<AudioStore::SegmentRow> AudioStore::segments(qint64 meetingId) const
{
    QList<SegmentRow> out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT t.id, t.start_time, t.end_time, COALESCE(p.speaker_label,''), COALESCE(p.display_name,''), t.original_text, t.edited_text, t.approx "
                             "FROM transcript_segments t LEFT JOIN speakers p ON p.id=t.speaker_id WHERE t.meeting_id=? ORDER BY t.start_time, t.id"));
    q.addBindValue(meetingId);
    if (!q.exec())
        return out;
    while (q.next())
        out.append({q.value(0).toLongLong(), q.value(1).toDouble(), q.value(2).toDouble(), q.value(3).toString(), q.value(4).toString(), q.value(5).toString(),
                    q.value(6).toString(), q.value(7).toInt() != 0});
    return out;
}

bool AudioStore::editSegment(qint64 segmentId, const QString &text)
{
    return exec(QStringLiteral("UPDATE transcript_segments SET edited_text = CASE WHEN ?=original_text THEN '' ELSE ? END WHERE id=?"), {text, text, segmentId});
}

bool AudioStore::deleteSegment(qint64 segmentId)
{
    return exec(QStringLiteral("DELETE FROM transcript_segments WHERE id=?"), {segmentId});
}

bool AudioStore::renameSpeaker(qint64 meetingId, const QString &label, const QString &name)
{
    return exec(QStringLiteral("UPDATE speakers SET display_name=? WHERE meeting_id=? AND speaker_label=?"), {name.simplified().left(60), meetingId, label});
}

QStringList AudioStore::speakerLabels(qint64 meetingId) const
{
    QStringList out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT speaker_label FROM speakers WHERE meeting_id=? AND speaker_label<>'SOUND' ORDER BY speaker_label"));
    q.addBindValue(meetingId);
    if (q.exec())
        while (q.next())
            out << q.value(0).toString();
    return out;
}

bool AudioStore::saveSummary(qint64 meetingId, const Audio::Summary &s, const QString &tmpl)
{
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    db.transaction();
    bool ok = exec(QStringLiteral("DELETE FROM summaries WHERE meeting_id=?"), {meetingId})
              && exec(QStringLiteral("DELETE FROM decisions WHERE meeting_id=?"), {meetingId})
              && exec(QStringLiteral("DELETE FROM action_items WHERE meeting_id=?"), {meetingId})
              && exec(QStringLiteral("INSERT INTO summaries(meeting_id, executive_summary, key_points, open_questions, notes, template, created_at, extra) VALUES(?,?,?,?,?,?,?,?)"),
                      {meetingId, s.executive, toJson(s.keyPoints), toJson(s.openQuestions), toJson(s.notes), tmpl, now(),
                       QString::fromUtf8(QJsonDocument(Audio::Json::summaryExtra(s)).toJson(QJsonDocument::Compact))});
    for (const Audio::Decision &d : s.decisions)
        ok = ok && exec(QStringLiteral("INSERT INTO decisions(meeting_id, description, timestamp, quote, needs_review, rationale, owner) VALUES(?,?,?,?,?,?,?)"),
                        {meetingId, d.description, d.timestamp, d.quote, int(d.needsReview), d.rationale, d.owner});
    for (const Audio::ActionItem &a : s.actions)
        ok = ok && exec(QStringLiteral("INSERT INTO action_items(meeting_id, task, assignee, deadline, priority, status, timestamp, quote, needs_review) VALUES(?,?,?,?,?,?,?,?,?)"),
                        {meetingId, a.task, a.assignee, a.deadline, a.priority, a.done ? QStringLiteral("done") : QStringLiteral("open"), a.timestamp, a.quote, int(a.needsReview)});
    if (!ok) {
        db.rollback();
        return false;
    }
    return db.commit();
}

bool AudioStore::hasSummary(qint64 meetingId) const
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT 1 FROM summaries WHERE meeting_id=?"));
    q.addBindValue(meetingId);
    return q.exec() && q.next();
}

Audio::Summary AudioStore::summary(qint64 meetingId, QList<qint64> *actionIds) const
{
    Audio::Summary s;
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT executive_summary, key_points, open_questions, notes, extra FROM summaries WHERE meeting_id=?"));
    q.addBindValue(meetingId);
    if (q.exec() && q.next()) {
        s.executive = q.value(0).toString();
        s.keyPoints = fromJson(q.value(1).toString());
        s.openQuestions = fromJson(q.value(2).toString());
        s.notes = fromJson(q.value(3).toString());
        Audio::Json::applySummaryExtra(s, QJsonDocument::fromJson(q.value(4).toString().toUtf8()).object());
    }
    QSqlQuery d(db);
    d.prepare(QStringLiteral("SELECT description, timestamp, quote, needs_review, rationale, owner FROM decisions WHERE meeting_id=? ORDER BY timestamp, id"));
    d.addBindValue(meetingId);
    if (d.exec())
        while (d.next()) {
            Audio::Decision x;
            x.description = d.value(0).toString();
            x.timestamp = d.value(1).toDouble();
            x.quote = d.value(2).toString();
            x.needsReview = d.value(3).toInt() != 0;
            x.rationale = d.value(4).toString();
            x.owner = d.value(5).toString();
            s.decisions.append(x);
        }
    for (const ActionRow &a : actions(meetingId)) {
        s.actions.append(a.item);
        if (actionIds)
            actionIds->append(a.id);
    }
    return s;
}

QList<AudioStore::ActionRow> AudioStore::actions(qint64 meetingId) const
{
    QList<ActionRow> out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT id, task, assignee, deadline, priority, status, timestamp, quote, needs_review FROM action_items WHERE meeting_id=? ORDER BY timestamp, id"));
    q.addBindValue(meetingId);
    if (!q.exec())
        return out;
    while (q.next()) {
        Audio::ActionItem a;
        a.task = q.value(1).toString();
        a.assignee = q.value(2).toString();
        a.deadline = q.value(3).toString();
        a.priority = q.value(4).toString();
        a.done = q.value(5).toString() == QLatin1String("done");
        a.timestamp = q.value(6).toDouble();
        a.quote = q.value(7).toString();
        a.needsReview = q.value(8).toInt() != 0;
        out.append({q.value(0).toLongLong(), a});
    }
    return out;
}

bool AudioStore::setActionDone(qint64 actionId, bool done)
{
    return exec(QStringLiteral("UPDATE action_items SET status=? WHERE id=?"), {done ? QStringLiteral("done") : QStringLiteral("open"), actionId});
}

qint64 AudioStore::startJob(qint64 meetingId)
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("INSERT INTO processing_jobs(meeting_id, created_at) VALUES(?,?)"));
    q.addBindValue(meetingId);
    q.addBindValue(now());
    return q.exec() ? q.lastInsertId().toLongLong() : 0;
}

bool AudioStore::updateJob(qint64 jobId, const QString &stage, double progress, const QString &doneStages)
{
    return exec(QStringLiteral("UPDATE processing_jobs SET current_stage=?, progress=?, done_stages=? WHERE id=?"), {stage, progress, doneStages, jobId});
}

bool AudioStore::finishJob(qint64 jobId, const QString &error)
{
    return exec(QStringLiteral("UPDATE processing_jobs SET error_message=?, completed_at=?, progress=CASE WHEN ?='' THEN 1 ELSE progress END WHERE id=?"),
                {error, now(), error, jobId});
}

bool AudioStore::clearHistory()
{
    return exec(QStringLiteral("DELETE FROM processing_jobs WHERE completed_at IS NOT NULL AND completed_at<>''"));
}

QList<AudioStore::HistoryRow> AudioStore::history(int limit) const
{
    QList<HistoryRow> out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT j.id, j.meeting_id, m.title, j.error_message, j.created_at, j.completed_at "
                             "FROM processing_jobs j JOIN meetings m ON m.id=j.meeting_id ORDER BY j.id DESC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec())
        return out;
    while (q.next()) {
        HistoryRow r;
        r.jobId = q.value(0).toLongLong();
        r.meetingId = q.value(1).toLongLong();
        r.title = q.value(2).toString();
        r.error = q.value(3).toString();
        r.started = when(q.value(4).toString());
        r.finished = when(q.value(5).toString());
        out.append(r);
    }
    return out;
}

AudioStore::Job AudioStore::lastJob(qint64 meetingId) const
{
    Job j;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT id, current_stage, progress, done_stages, error_message, created_at, completed_at FROM processing_jobs WHERE meeting_id=? ORDER BY id DESC LIMIT 1"));
    q.addBindValue(meetingId);
    if (q.exec() && q.next()) {
        j.id = q.value(0).toLongLong();
        j.stage = q.value(1).toString();
        j.progress = q.value(2).toDouble();
        j.done = q.value(3).toString();
        j.error = q.value(4).toString();
        j.created = when(q.value(5).toString());
        j.completed = when(q.value(6).toString());
    }
    return j;
}
