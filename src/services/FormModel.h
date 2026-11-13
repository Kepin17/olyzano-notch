#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QHash>
#include <QMap>
#include <QPair>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUuid>

#include <cmath>

// Forms: a set of questions that people answer on a page you share (text, numbers, choices, dates, files), the rules an answer must follow,
// and the table of answers (CSV). This file knows nothing of networks or disks. Pure.
namespace Forms {

constexpr int MaxFields = 40, MaxOptions = 50, MaxLabel = 200, MaxHelp = 500, MaxTitle = 120, MaxDescription = 2000;
constexpr int MaxShort = 300, MaxLong = 5000;
constexpr int MaxResponses = 1000;
constexpr int MaxFilesPerField = 20;
constexpr int MaxSections = 20;
constexpr int MaxEndTitle = 120, MaxEndMessage = 1000, MaxButtonLabel = 40, MaxUrl = 500;
constexpr qint64 MaxSpanSeconds = 90 * 86400; // the longest a scheduled form may stay open

struct Field {
    QString id, type, label, help; // type: short | long | number | choice | multi | date | email | file
    bool required = false;
    bool isName = false;           // its answer is the name used when somebody answers ("Budi answered …")
    QStringList options;           // choice | multi
    int maxFiles = 1;              // file
    QString showField;             // shown only when the answer to this earlier choice/multi question …
    QStringList showValues;        // … is (or includes) one of these
    QStringList endOn;             // choice: answering one of these ends the form (what comes after is not asked)
    int scaleMax = 5;              // rating | scale: the highest number (3..10)
    QString lowLabel, highLabel;   // scale: what the ends mean ("Not at all", "Very much")
    QMap<QString, int> optionLimits; // choice | multi: how many answers each choice takes (a choice that is not here takes as many as come)
};

// What a person reads after sending: a title, a message and one button that opens a link (to join a group, to a page…).
struct Ending {
    QString title, message, buttonLabel, buttonUrl;
    bool hasButton() const { return !buttonLabel.trimmed().isEmpty() && !buttonUrl.trimmed().isEmpty(); }
};

struct Limits {
    int maxResponses = 100;  // answers taken, then the form closes (1..MaxResponses)
    int maxPeople = 0;       // different devices that may answer (0 = no limit)
    int ttlSeconds = 7 * 86400; // how long the link works
    int maxFileMb = 25;      // each file
    int maxTotalMb = 100;    // all the files of one answer
    qint64 opensAt = 0;      // when it opens (epoch seconds; 0 = at once) …
    qint64 closesAt = 0;     // … and closes (0 = after ttlSeconds). With a closing time, ttlSeconds is not used.
};

struct Form {
    QString id, title, description;
    QList<Field> fields;
    Limits limits;
    QString layout = QStringLiteral("single"); // single | sections (a section starts a new slide) | slides (one question on each)
    Ending ending;
    bool filesToShelf = false; // the files that come in go to the Files shelf by themselves, in a category named like the form
    bool oneAnswer = false; // one answer from each device (counted by network address); false = as many as they like
    bool allowEdit = false; // people may change their answer later, from a personal link
    QString status = QStringLiteral("draft"); // draft | open | closed
    qint64 created = 0;
};

inline const QStringList &types()
{
    static const QStringList t = {QStringLiteral("short"), QStringLiteral("long"), QStringLiteral("number"), QStringLiteral("choice"), QStringLiteral("multi"),
                                  QStringLiteral("date"), QStringLiteral("email"), QStringLiteral("file"), QStringLiteral("section"), QStringLiteral("rating"), QStringLiteral("scale"), QStringLiteral("signature")};
    return t;
}

inline bool isQuestion(const Field &f) { return f.type != QLatin1String("section"); } // a section is a heading: it asks nothing
// A question whose answer is a file: the files of a "file" question, and the picture a person draws for a "signature".
inline bool takesFiles(const Field &f) { return f.type == QLatin1String("file") || f.type == QLatin1String("signature"); }
constexpr qint64 MaxSignatureBytes = 300 * 1024;
constexpr int MaxScaleLabel = 60;

inline const QStringList &layouts()
{
    static const QStringList l = {QStringLiteral("single"), QStringLiteral("sections"), QStringLiteral("slides")};
    return l;
}

// A link a button may open: http or https, with a host, no spaces or control characters.
inline bool validUrl(const QString &s)
{
    if (s.isEmpty() || s.size() > MaxUrl)
        return false;
    for (const QChar c : s)
        if (c.unicode() <= 0x20 || c.unicode() == 0x7F)
            return false;
    const QUrl u(s, QUrl::StrictMode);
    return u.isValid() && (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https")) && !u.host().isEmpty();
}

// The token behind a personal link: what the page makes up and keeps; the app and the server keep only its hash.
inline bool validEditId(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]{16,64}$"));
    return re.match(s).hasMatch();
}
inline QString editHash(const QString &editId) { return QString::fromLatin1(QCryptographicHash::hash(editId.toUtf8(), QCryptographicHash::Sha256).toHex()); }

inline QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces).left(12); }

// ---- JSON ----
inline QJsonObject toJson(const Form &f)
{
    QJsonArray fields;
    for (const Field &x : f.fields)
        fields.append(QJsonObject{{QStringLiteral("id"), x.id}, {QStringLiteral("type"), x.type}, {QStringLiteral("label"), x.label}, {QStringLiteral("help"), x.help},
                                  {QStringLiteral("required"), x.required}, {QStringLiteral("isName"), x.isName}, {QStringLiteral("options"), QJsonArray::fromStringList(x.options)},
                                  {QStringLiteral("maxFiles"), x.maxFiles}, {QStringLiteral("showIf"), QJsonObject{{QStringLiteral("field"), x.showField}, {QStringLiteral("values"), QJsonArray::fromStringList(x.showValues)}}},
                                  {QStringLiteral("endOn"), QJsonArray::fromStringList(x.endOn)}, {QStringLiteral("scaleMax"), x.scaleMax},
                                  {QStringLiteral("lowLabel"), x.lowLabel}, {QStringLiteral("highLabel"), x.highLabel}, {QStringLiteral("optionLimits"), [&] { QJsonObject l; for (auto it = x.optionLimits.cbegin(); it != x.optionLimits.cend(); ++it) l.insert(it.key(), it.value()); return l; }()}});
    return {{QStringLiteral("id"), f.id}, {QStringLiteral("title"), f.title}, {QStringLiteral("description"), f.description}, {QStringLiteral("fields"), fields},
            {QStringLiteral("status"), f.status}, {QStringLiteral("created"), double(f.created)}, {QStringLiteral("layout"), f.layout}, {QStringLiteral("allowEdit"), f.allowEdit}, {QStringLiteral("oneAnswer"), f.oneAnswer}, {QStringLiteral("filesToShelf"), f.filesToShelf},
            {QStringLiteral("ending"), QJsonObject{{QStringLiteral("title"), f.ending.title}, {QStringLiteral("message"), f.ending.message},
                                                  {QStringLiteral("buttonLabel"), f.ending.buttonLabel}, {QStringLiteral("buttonUrl"), f.ending.buttonUrl}}},
            {QStringLiteral("limits"), QJsonObject{{QStringLiteral("maxResponses"), f.limits.maxResponses}, {QStringLiteral("maxPeople"), f.limits.maxPeople},
                                                  {QStringLiteral("ttlSeconds"), f.limits.ttlSeconds}, {QStringLiteral("maxFileMb"), f.limits.maxFileMb},
                                                  {QStringLiteral("maxTotalMb"), f.limits.maxTotalMb}, {QStringLiteral("opensAt"), double(f.limits.opensAt)},
                                                  {QStringLiteral("closesAt"), double(f.limits.closesAt)}}}};
}

inline Form fromJson(const QJsonObject &o)
{
    Form f;
    f.id = o.value(QStringLiteral("id")).toString();
    f.title = o.value(QStringLiteral("title")).toString();
    f.description = o.value(QStringLiteral("description")).toString();
    f.status = o.value(QStringLiteral("status")).toString(QStringLiteral("draft"));
    f.created = o.value(QStringLiteral("created")).toVariant().toLongLong();
    for (const QJsonValue &v : o.value(QStringLiteral("fields")).toArray()) {
        const QJsonObject x = v.toObject();
        Field fl;
        fl.id = x.value(QStringLiteral("id")).toString();
        fl.type = x.value(QStringLiteral("type")).toString();
        fl.label = x.value(QStringLiteral("label")).toString();
        fl.help = x.value(QStringLiteral("help")).toString();
        fl.required = x.value(QStringLiteral("required")).toBool();
        fl.isName = x.value(QStringLiteral("isName")).toBool();
        for (const QJsonValue &op : x.value(QStringLiteral("options")).toArray())
            fl.options << op.toString();
        fl.maxFiles = x.value(QStringLiteral("maxFiles")).toInt(1);
        const QJsonObject si = x.value(QStringLiteral("showIf")).toObject();
        fl.showField = si.value(QStringLiteral("field")).toString();
        for (const QJsonValue &op : si.value(QStringLiteral("values")).toArray())
            fl.showValues << op.toString();
        for (const QJsonValue &op : x.value(QStringLiteral("endOn")).toArray())
            fl.endOn << op.toString();
        fl.scaleMax = x.value(QStringLiteral("scaleMax")).toInt(5);
        fl.lowLabel = x.value(QStringLiteral("lowLabel")).toString();
        fl.highLabel = x.value(QStringLiteral("highLabel")).toString();
        const QJsonObject lim = x.value(QStringLiteral("optionLimits")).toObject();
        for (auto it = lim.constBegin(); it != lim.constEnd(); ++it)
            fl.optionLimits.insert(it.key(), it.value().toInt());
        f.fields.append(fl);
    }
    const QJsonObject l = o.value(QStringLiteral("limits")).toObject();
    f.limits.maxResponses = l.value(QStringLiteral("maxResponses")).toInt(f.limits.maxResponses);
    f.limits.maxPeople = l.value(QStringLiteral("maxPeople")).toInt(0);
    f.limits.ttlSeconds = l.value(QStringLiteral("ttlSeconds")).toInt(f.limits.ttlSeconds);
    f.limits.maxFileMb = l.value(QStringLiteral("maxFileMb")).toInt(f.limits.maxFileMb);
    f.limits.maxTotalMb = l.value(QStringLiteral("maxTotalMb")).toInt(f.limits.maxTotalMb);
    f.limits.opensAt = l.value(QStringLiteral("opensAt")).toVariant().toLongLong();
    f.limits.closesAt = l.value(QStringLiteral("closesAt")).toVariant().toLongLong();
    f.allowEdit = o.value(QStringLiteral("allowEdit")).toBool();
    f.oneAnswer = o.value(QStringLiteral("oneAnswer")).toBool();
    f.filesToShelf = o.value(QStringLiteral("filesToShelf")).toBool();
    f.layout = o.value(QStringLiteral("layout")).toString(QStringLiteral("single"));
    if (!layouts().contains(f.layout))
        f.layout = QStringLiteral("single");
    const QJsonObject e = o.value(QStringLiteral("ending")).toObject();
    f.ending = {e.value(QStringLiteral("title")).toString(), e.value(QStringLiteral("message")).toString(), e.value(QStringLiteral("buttonLabel")).toString(),
                e.value(QStringLiteral("buttonUrl")).toString()};
    return f;
}

// "" when the definition can be published, else why not (in the words a person who builds forms reads).
inline QString validateDefinition(const Form &f)
{
    if (f.title.trimmed().isEmpty())
        return QStringLiteral("Give the form a title.");
    if (f.title.size() > MaxTitle || f.description.size() > MaxDescription)
        return QStringLiteral("The title or the description is too long.");
    if (!std::any_of(f.fields.begin(), f.fields.end(), [](const Field &x) { return isQuestion(x); }))
        return QStringLiteral("Add at least one question.");
    if (!layouts().contains(f.layout))
        return QStringLiteral("The layout is not known.");
    if (std::count_if(f.fields.begin(), f.fields.end(), [](const Field &x) { return !isQuestion(x); }) > MaxSections)
        return QStringLiteral("A form can have up to %1 sections.").arg(MaxSections);
    if (f.ending.title.size() > MaxEndTitle || f.ending.message.size() > MaxEndMessage || f.ending.buttonLabel.size() > MaxButtonLabel)
        return QStringLiteral("The closing words are too long.");
    if (f.ending.hasButton() && !validUrl(f.ending.buttonUrl.trimmed()))
        return QStringLiteral("The button needs an address that starts with http:// or https://.");
    if (!f.ending.buttonLabel.trimmed().isEmpty() && f.ending.buttonUrl.trimmed().isEmpty())
        return QStringLiteral("The button needs an address.");
    if (!f.ending.buttonUrl.trimmed().isEmpty() && f.ending.buttonLabel.trimmed().isEmpty())
        return QStringLiteral("The button needs a label.");
    if (f.fields.size() > MaxFields)
        return QStringLiteral("A form can have up to %1 questions.").arg(MaxFields);
    QStringList ids;
    for (const Field &x : f.fields) {
        if (!types().contains(x.type))
            return QStringLiteral("A question has a kind that is not known.");
        if (x.label.trimmed().isEmpty())
            return isQuestion(x) ? QStringLiteral("Every question needs a label.") : QStringLiteral("Every section needs a title.");
        if (x.label.size() > MaxLabel || x.help.size() > MaxHelp)
            return QStringLiteral("A label or a hint is too long.");
        if (x.id.isEmpty() || ids.contains(x.id))
            return QStringLiteral("Two questions have the same id.");
        ids << x.id;
        if (x.type == QLatin1String("choice") || x.type == QLatin1String("multi")) {
            QStringList seen;
            for (const QString &o : x.options) {
                if (o.trimmed().isEmpty() || o.size() > MaxLabel)
                    return QStringLiteral("\"%1\" has an empty or too long choice.").arg(x.label);
                if (seen.contains(o.trimmed()))
                    return QStringLiteral("\"%1\" has the same choice twice.").arg(x.label);
                seen << o.trimmed();
            }
            if (x.options.size() < 2 || x.options.size() > MaxOptions)
                return QStringLiteral("\"%1\" needs between 2 and %2 choices.").arg(x.label).arg(MaxOptions);
        }
        if ((x.type == QLatin1String("rating") || x.type == QLatin1String("scale")) && (x.scaleMax < 3 || x.scaleMax > 10))
            return QStringLiteral("\"%1\": the highest number is 3 to 10.").arg(x.label);
        if (x.lowLabel.size() > MaxScaleLabel || x.highLabel.size() > MaxScaleLabel)
            return QStringLiteral("\"%1\": the words at the ends of the scale are too long.").arg(x.label);
        if (!x.optionLimits.isEmpty()) {
            if (x.type != QLatin1String("choice") && x.type != QLatin1String("multi"))
                return QStringLiteral("Only a question with choices can limit its choices.");
            for (auto it = x.optionLimits.cbegin(); it != x.optionLimits.cend(); ++it)
                if (!x.options.contains(it.key()) || it.value() < 1 || it.value() > MaxResponses)
                    return QStringLiteral("\"%1\": a limit is for a choice that is not there, or not between 1 and %2.").arg(x.label).arg(MaxResponses);
        }
        if (x.type == QLatin1String("file") && (x.maxFiles < 1 || x.maxFiles > MaxFilesPerField))
            return QStringLiteral("\"%1\": the number of files is 1 to %2.").arg(x.label).arg(MaxFilesPerField);
        if (!x.showField.isEmpty()) { // shown only if an earlier choice has one of these answers
            const auto src = std::find_if(f.fields.begin(), f.fields.end(), [&](const Field &y) { return y.id == x.showField; });
            if (src == f.fields.end() || !ids.contains(x.showField) || x.showField == x.id)
                return QStringLiteral("\"%1\" is shown only after a question that comes before it.").arg(x.label);
            if (src->type != QLatin1String("choice") && src->type != QLatin1String("multi"))
                return QStringLiteral("\"%1\" can depend only on a question with choices.").arg(x.label);
            if (x.showValues.isEmpty())
                return QStringLiteral("\"%1\": pick the answers that show it.").arg(x.label);
            for (const QString &v : x.showValues)
                if (!src->options.contains(v))
                    return QStringLiteral("\"%1\" waits for a choice that is not there any more.").arg(x.label);
        }
        if (!x.endOn.isEmpty()) {
            if (x.type != QLatin1String("choice"))
                return QStringLiteral("Only a question with one choice can end the form.");
            for (const QString &v : x.endOn)
                if (!x.options.contains(v))
                    return QStringLiteral("\"%1\" ends the form on a choice that is not there any more.").arg(x.label);
        }
    }
    if (f.limits.maxResponses < 1 || f.limits.maxResponses > MaxResponses)
        return QStringLiteral("Answers allowed: 1 to %1.").arg(MaxResponses);
    if (f.limits.maxPeople < 0 || f.limits.maxPeople > MaxResponses)
        return QStringLiteral("People allowed: 0 (no limit) to %1.").arg(MaxResponses);
    if (f.limits.opensAt < 0 || f.limits.closesAt < 0)
        return QStringLiteral("The dates are not right.");
    if (f.limits.closesAt > 0 && f.limits.opensAt >= f.limits.closesAt)
        return QStringLiteral("The form must close after it opens.");
    if (f.limits.closesAt > 0 && f.limits.opensAt > 0 && f.limits.closesAt - f.limits.opensAt > MaxSpanSeconds)
        return QStringLiteral("A form can stay open for up to 90 days.");
    if (f.limits.closesAt == 0 && (f.limits.ttlSeconds < 60 || f.limits.ttlSeconds > 30 * 86400))
        return QStringLiteral("The link can work from 1 minute to 30 days.");
    if (f.limits.maxFileMb < 1 || f.limits.maxFileMb > 2048 || f.limits.maxTotalMb < f.limits.maxFileMb || f.limits.maxTotalMb > 4096)
        return QStringLiteral("The size limits for files are not right.");
    return {};
}

// When the form may be put on a network: its definition is right, and a closing time (if it has one) has not passed and is not too far off.
inline QString validateForPublish(const Form &f, qint64 now)
{
    if (const QString why = validateDefinition(f); !why.isEmpty())
        return why;
    if (f.limits.closesAt > 0) {
        if (f.limits.closesAt <= now)
            return QStringLiteral("The closing time has passed. Choose a later date.");
        if (f.limits.closesAt - now > MaxSpanSeconds)
            return QStringLiteral("A form can stay open for up to 90 days.");
    }
    return {};
}

// Seconds from `now` until the form closes: the closing time if there is one, else the time of the link.
inline int effectiveTtl(const Form &f, qint64 now)
{
    return f.limits.closesAt > 0 ? int(qBound<qint64>(60, f.limits.closesAt - now, qint64(90) * 86400)) : f.limits.ttlSeconds;
}

// ---- templates ----
inline Field field(const QString &type, const QString &label, bool required = false, const QStringList &options = {})
{
    Field f;
    f.id = QStringLiteral("q") + newId().left(6);
    f.type = type;
    f.label = label;
    f.required = required;
    f.options = options;
    return f;
}

inline Form blank()
{
    Form f;
    f.id = newId();
    f.created = QDateTime::currentSecsSinceEpoch();
    f.title = QStringLiteral("Untitled form");
    f.filesToShelf = true; // a form made now keeps its files in order; one written before this keeps what it did
    return f;
}

// "send" | "contact" | "rsvp" | "blank"
inline Form templateFor(const QString &which)
{
    Form f = blank();
    if (which == QLatin1String("send")) {
        f.title = QStringLiteral("Send me files");
        f.description = QStringLiteral("Add your name and the files, then press Send.");
        Field name = field(QStringLiteral("short"), QStringLiteral("Your name"), true);
        name.isName = true;
        Field files = field(QStringLiteral("file"), QStringLiteral("Files"), true);
        files.maxFiles = 10;
        f.fields = {name, files, field(QStringLiteral("long"), QStringLiteral("A message (optional)"))};
        f.limits.maxResponses = 20;
    } else if (which == QLatin1String("contact")) {
        f.title = QStringLiteral("Contact");
        Field name = field(QStringLiteral("short"), QStringLiteral("Name"), true);
        name.isName = true;
        f.fields = {name, field(QStringLiteral("email"), QStringLiteral("Email"), true), field(QStringLiteral("long"), QStringLiteral("Message"), true)};
    } else if (which == QLatin1String("rsvp")) {
        f.title = QStringLiteral("RSVP");
        f.description = QStringLiteral("Will you come?");
        Field name = field(QStringLiteral("short"), QStringLiteral("Name"), true);
        name.isName = true;
        f.fields = {name, field(QStringLiteral("choice"), QStringLiteral("Will you come?"), true, {QStringLiteral("Yes"), QStringLiteral("No"), QStringLiteral("Maybe")}),
                    field(QStringLiteral("number"), QStringLiteral("How many people (with you)?")), field(QStringLiteral("long"), QStringLiteral("Anything we should know?"))};
        f.limits.maxResponses = 200;
    }
    return f;
}

// ---- an answer ----
struct Upload {
    QString field, name, path; // `name` is what the sender called it; `path` where it is held until the answer is kept
    qint64 size = 0;
};

inline QString sanitizeFileName(const QString &name)
{
    QString n = name.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1);
    QString out;
    for (const QChar c : n)
        out += (c.unicode() < 0x20 || c == QLatin1Char(':') || c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('"') || c == QLatin1Char('<')
                || c == QLatin1Char('>') || c == QLatin1Char('|')) ? QLatin1Char('_') : c;
    out = out.trimmed();
    while (out.startsWith(QLatin1Char('.')))
        out.remove(0, 1);
    if (out.size() > 120) {
        const int dot = out.lastIndexOf(QLatin1Char('.'));
        const QString ext = dot > 0 && out.size() - dot <= 10 ? out.mid(dot) : QString();
        out = out.left(120 - ext.size()) + ext;
    }
    return out.isEmpty() ? QStringLiteral("file") : out;
}

inline bool validEmail(const QString &s)
{
    static const QRegularExpression re(QStringLiteral(R"(^[^\s@]{1,64}@[^\s@]{1,190}\.[^\s@.]{2,24}$)"));
    return s.size() <= 254 && re.match(s).hasMatch();
}

inline bool validDate(const QString &s) { return QDate::fromString(s, Qt::ISODate).isValid() && s.size() == 10; }

// The answers of a question as plain trimmed strings (a choice is one, a "multi" is several).
inline QStringList answerValues(const QJsonValue &v)
{
    QStringList out;
    if (v.isArray()) {
        for (const QJsonValue &p : v.toArray())
            out << p.toString().trimmed();
    } else if (v.isString() && !v.toString().trimmed().isEmpty()) {
        out << v.toString().trimmed();
    }
    return out;
}

// The ids of the questions and sections that are asked, given the answers so far. A field with a rule is asked only when the question it waits for
// is asked itself and was answered with one of the wanted choices; a choice that ends the form hides everything after it. The page has the same
// rules in JavaScript (visibleIds in form.html): keep them in step.
inline QSet<QString> visibleIds(const Form &f, const QJsonObject &answers)
{
    QSet<QString> shown;
    bool ended = false, sectionHidden = false; // a section that is not asked takes its questions with it, up to the next section
    for (const Field &x : f.fields) {
        if (ended)
            break;
        if (!isQuestion(x))
            sectionHidden = false;
        bool rule = true;
        if (!x.showField.isEmpty()) {
            const QStringList got = answerValues(answers.value(x.showField));
            rule = shown.contains(x.showField) && std::any_of(x.showValues.begin(), x.showValues.end(), [&](const QString &v) { return got.contains(v); });
        }
        if (!isQuestion(x) && !rule)
            sectionHidden = true;
        if (!rule || sectionHidden)
            continue;
        shown.insert(x.id);
        if (!x.endOn.isEmpty() && x.endOn.contains(answerValues(answers.value(x.id)).value(0)))
            ended = true;
    }
    return shown;
}

// Checks what somebody sent against the form. `clean` gets the answers as they are kept (text trimmed, one value per question; a "multi" is an
// array, a file question is the list of names) without anything the form did not ask for. "" or why it cannot be taken (the page shows it).
inline QString validateSubmission(const Form &f, const QJsonObject &answers, const QList<Upload> &uploads, QJsonObject *clean = nullptr, const QJsonObject &keptFiles = {})
{
    QJsonObject out;
    qint64 total = 0;
    const QSet<QString> shown = visibleIds(f, answers);
    for (const Upload &u : uploads)
        if (!shown.contains(u.field))
            return QStringLiteral("A file came with a question that is not asked.");
    for (const Field &x : f.fields) {
        if (!isQuestion(x) || !shown.contains(x.id))
            continue; // a section is a heading; a question that is not asked needs no answer and keeps none
        const QJsonValue v = answers.value(x.id);
        const auto missing = [&] { return QStringLiteral("\"%1\" needs an answer.").arg(x.label); };
        if (takesFiles(x)) {
            QJsonArray names;
            const bool sign = x.type == QLatin1String("signature");
            for (const Upload &u : uploads)
                if (u.field == x.id) {
                    if (sign) { // a drawing: one small PNG
                        QFile pic(u.path);
                        if (u.size > MaxSignatureBytes || !pic.open(QIODevice::ReadOnly) || pic.read(8) != QByteArray("\x89PNG\r\n\x1a\n", 8))
                            return QStringLiteral("\"%1\": the signature must be a small picture (PNG).").arg(x.label);
                    }
                    names.append(sanitizeFileName(u.name));
                    total += u.size;
                    if (u.size > qint64(f.limits.maxFileMb) * 1048576)
                        return QStringLiteral("\"%1\" is bigger than %2 MB.").arg(u.name).arg(f.limits.maxFileMb);
                }
            if (names.size() > (x.type == QLatin1String("signature") ? 1 : x.maxFiles))
                return QStringLiteral("\"%1\": at most %2 files.").arg(x.label).arg(x.type == QLatin1String("signature") ? 1 : x.maxFiles);
            if (names.isEmpty() && keptFiles.value(x.id).isArray()) // changing an answer: the files it already has count
                names = keptFiles.value(x.id).toArray();
            if (x.required && names.isEmpty())
                return missing();
            out.insert(x.id, names);
        } else if (x.type == QLatin1String("multi")) {
            QJsonArray picked;
            for (const QJsonValue &p : v.toArray()) {
                const QString s = p.toString().trimmed();
                if (!x.options.contains(s))
                    return QStringLiteral("\"%1\": that choice is not one of the choices.").arg(x.label);
                if (!picked.contains(s))
                    picked.append(s);
            }
            if (x.required && picked.isEmpty())
                return missing();
            out.insert(x.id, picked);
        } else {
            const QString s = v.isString() ? v.toString().trimmed() : v.isDouble() ? QString::number(v.toDouble(), 'g', 15) : QString();
            if (s.isEmpty()) {
                if (x.required)
                    return missing();
                out.insert(x.id, QString());
                continue;
            }
            if (x.type == QLatin1String("short") && s.size() > MaxShort)
                return QStringLiteral("\"%1\" is too long (up to %2 characters).").arg(x.label).arg(MaxShort);
            if (x.type == QLatin1String("long") && s.size() > MaxLong)
                return QStringLiteral("\"%1\" is too long (up to %2 characters).").arg(x.label).arg(MaxLong);
            if (x.type == QLatin1String("rating") || x.type == QLatin1String("scale")) {
                bool ok = false;
                const int n = s.toInt(&ok);
                if (!ok || n < 1 || n > x.scaleMax)
                    return QStringLiteral("\"%1\" must be a number from 1 to %2.").arg(x.label).arg(x.scaleMax);
                out.insert(x.id, QString::number(n));
                continue;
            }
            if (x.type == QLatin1String("number")) {
                bool ok = false;
                const double d = QString(s).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
                if (!ok || !std::isfinite(d) || std::fabs(d) > 1e15)
                    return QStringLiteral("\"%1\" must be a number.").arg(x.label);
            }
            if (x.type == QLatin1String("choice") && !x.options.contains(s))
                return QStringLiteral("\"%1\": that choice is not one of the choices.").arg(x.label);
            if (x.type == QLatin1String("date") && !validDate(s))
                return QStringLiteral("\"%1\" must be a date (year-month-day).").arg(x.label);
            if (x.type == QLatin1String("email") && !validEmail(s))
                return QStringLiteral("\"%1\" must be an email address.").arg(x.label);
            out.insert(x.id, s);
        }
    }
    if (total > qint64(f.limits.maxTotalMb) * 1048576)
        return QStringLiteral("The files are more than %1 MB together.").arg(f.limits.maxTotalMb);
    if (clean)
        *clean = out;
    return {};
}

// The name to say for an answer: the answer of the question marked as the name, else "".
inline QString nameOf(const Form &f, const QJsonObject &clean)
{
    for (const Field &x : f.fields)
        if (x.isName && clean.value(x.id).isString() && !clean.value(x.id).toString().isEmpty())
            return clean.value(x.id).toString().left(60);
    return {};
}

// ---- the table ----
struct Response {
    int n = 0;
    qint64 time = 0;
    QString device;
    QJsonObject answers;
    QString edit;        // the hash of its personal link's token ("" = none)
    qint64 edited = 0;   // when it was last changed (0 = never)
    int revisions = 0;
};

// How many answers each limited choice has taken: key "<question id>|<choice>". `exclude` is the number of an answer that is being changed (its own
// choice is not counted against it).
inline QHash<QString, int> optionCounts(const Form &f, const QList<Response> &responses, int exclude = 0)
{
    QHash<QString, int> out;
    for (const Field &x : f.fields) {
        if (x.optionLimits.isEmpty())
            continue;
        for (const Response &r : responses)
            if (r.n != exclude)
                for (const QString &g : answerValues(r.answers.value(x.id)))
                    if (x.optionLimits.contains(g))
                        ++out[x.id + QLatin1Char('|') + g];
    }
    return out;
}

// The choices that have taken all they may: {question id: [choices]}, for the page to switch them off.
inline QJsonObject fullOptions(const Form &f, const QList<Response> &responses)
{
    QJsonObject out;
    const QHash<QString, int> taken = optionCounts(f, responses);
    for (const Field &x : f.fields) {
        QJsonArray full;
        for (auto it = x.optionLimits.cbegin(); it != x.optionLimits.cend(); ++it)
            if (taken.value(x.id + QLatin1Char('|') + it.key()) >= it.value())
                full.append(it.key());
        if (!full.isEmpty())
            out.insert(x.id, full);
    }
    return out;
}

// The first choice in `answers` that is full ({question, choice}; empty when there is none). Only questions that are asked count.
inline QPair<QString, QString> optionFull(const Form &f, const QJsonObject &answers, const QHash<QString, int> &taken)
{
    const QSet<QString> shown = visibleIds(f, answers);
    for (const Field &x : f.fields)
        if (shown.contains(x.id))
            for (const QString &g : answerValues(answers.value(x.id)))
                if (x.optionLimits.contains(g) && taken.value(x.id + QLatin1Char('|') + g) >= x.optionLimits.value(g))
                    return {x.id, g};
    return {};
}

// What the answers add up to, for the summary page: for each question the answers it got (a choice: how many picked each, with the share of
// those who answered; a number: count, sum, average, smallest, largest; the rest: how many answered), and the answers received on each of the
// last days. `today` is the local day to count back from (the newest answer's day when 0).
inline QJsonObject summarize(const Form &f, const QList<Response> &responses, const QDate &today = QDate())
{
    QJsonArray questions;
    for (const Field &x : f.fields) {
        if (!isQuestion(x))
            continue;
        int answered = 0;
        QMap<QString, int> counts;
        double sum = 0, lo = 0, hi = 0;
        for (const Response &r : responses) {
            const QJsonValue v = r.answers.value(x.id);
            if (x.type == QLatin1String("multi") || x.type == QLatin1String("choice") || x.type == QLatin1String("rating") || x.type == QLatin1String("scale")) {
                const QStringList got = answerValues(v);
                if (got.isEmpty())
                    continue;
                ++answered;
                for (const QString &g : got)
                    ++counts[g];
            } else if (x.type == QLatin1String("number")) {
                bool ok = false;
                const double d = v.toString().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
                if (!ok)
                    continue;
                lo = answered == 0 ? d : std::min(lo, d);
                hi = answered == 0 ? d : std::max(hi, d);
                sum += d;
                ++answered;
            } else if (v.isArray() ? !v.toArray().isEmpty() : !v.toString().isEmpty()) {
                ++answered;
            }
        }
        QJsonObject q{{QStringLiteral("id"), x.id}, {QStringLiteral("label"), x.label}, {QStringLiteral("type"), x.type}, {QStringLiteral("answered"), answered}};
        const bool numbered = x.type == QLatin1String("rating") || x.type == QLatin1String("scale"); // 1..N, each a bar
        if (x.type == QLatin1String("multi") || x.type == QLatin1String("choice") || numbered) {
            QJsonArray bars;
            QStringList opts = x.options;
            if (numbered) {
                opts.clear();
                for (int k = 1; k <= x.scaleMax; ++k)
                    opts << QString::number(k);
                double total = 0;
                int n = 0;
                for (int k = 1; k <= x.scaleMax; ++k) {
                    total += k * counts.value(QString::number(k));
                    n += counts.value(QString::number(k));
                }
                if (n > 0)
                    q.insert(QStringLiteral("stats"), QJsonObject{{QStringLiteral("avg"), total / n}, {QStringLiteral("min"), 1}, {QStringLiteral("max"), x.scaleMax}});
            }
            for (const QString &o : opts)
                bars.append(QJsonObject{{QStringLiteral("label"), o}, {QStringLiteral("count"), counts.value(o)},
                                        {QStringLiteral("pct"), answered > 0 ? std::round(1000.0 * counts.value(o) / answered) / 10 : 0.0}});
            q.insert(QStringLiteral("bars"), bars);
        } else if (x.type == QLatin1String("number") && answered > 0) {
            q.insert(QStringLiteral("stats"), QJsonObject{{QStringLiteral("sum"), sum}, {QStringLiteral("avg"), sum / answered}, {QStringLiteral("min"), lo}, {QStringLiteral("max"), hi}});
        }
        questions.append(q);
    }
    QJsonArray days;
    if (!responses.isEmpty()) {
        QMap<QDate, int> perDay;
        QDate newest;
        for (const Response &r : responses) {
            const QDate d = QDateTime::fromSecsSinceEpoch(r.time).date();
            ++perDay[d];
            if (!newest.isValid() || d > newest)
                newest = d;
        }
        const QDate last = today.isValid() ? std::max(today, newest) : newest;
        const QDate first = std::max(perDay.firstKey(), last.addDays(-29)); // at most a month, from the first day anything came
        for (QDate d = first; d <= last; d = d.addDays(1))
            days.append(QJsonObject{{QStringLiteral("day"), d.toString(Qt::ISODate)}, {QStringLiteral("count"), perDay.value(d)}});
    }
    return {{QStringLiteral("total"), responses.size()}, {QStringLiteral("questions"), questions}, {QStringLiteral("days"), days}};
}

inline bool isImageName(const QString &name)
{
    static const QStringList ext = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("bmp")};
    return ext.contains(QFileInfo(name).suffix().toLower());
}

// The name a file gets on the shelf: who sent it, which question (when the form asks for files in more than one place), and what it was called.
inline QString shelfName(const Form &f, const Response &r, const QString &fieldId, const QString &original)
{
    const QString who = nameOf(f, r.answers);
    int fileQuestions = 0;
    QString label;
    for (const Field &x : f.fields)
        if (takesFiles(x)) {
            ++fileQuestions;
            if (x.id == fieldId)
                label = x.label;
        }
    QString name = who.isEmpty() ? QStringLiteral("#%1").arg(r.n) : who;
    if (fileQuestions > 1 && !label.isEmpty())
        name += QStringLiteral(" - ") + label;
    return sanitizeFileName(name + QStringLiteral(" - ") + original);
}

inline QString csvCell(QString s)
{
    // a cell that begins like a formula would run in a spreadsheet: it is written as text
    if (!s.isEmpty() && (s[0] == QLatin1Char('=') || s[0] == QLatin1Char('+') || s[0] == QLatin1Char('-') || s[0] == QLatin1Char('@') || s[0] == QLatin1Char('\t') || s[0] == QLatin1Char('\r')))
        s.prepend(QLatin1Char('\''));
    if (s.contains(QLatin1Char('"')) || s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('\n')) || s.contains(QLatin1Char('\r')))
        return QLatin1Char('"') + s.replace(QLatin1Char('"'), QLatin1String("\"\"")) + QLatin1Char('"');
    return s;
}

inline QString cellText(const Field &x, const QJsonValue &v)
{
    if (v.isArray()) {
        QStringList parts;
        for (const QJsonValue &p : v.toArray())
            parts << p.toString();
        return parts.join(QStringLiteral("; "));
    }
    return v.toString();
}

// What an answer says, for searching it: a line "Question: answer" for each question that was answered (a file or a signature by the name it is kept
// under), the form's title first so that a search for the form finds its answers too. Sections and unanswered questions are left out.
inline QString answerText(const Form &f, const Response &r)
{
    QStringList lines;
    for (const Field &x : f.fields) {
        if (!isQuestion(x))
            continue;
        const QString v = cellText(x, r.answers.value(x.id)).simplified();
        if (!v.isEmpty())
            lines << x.label.simplified() + QStringLiteral(": ") + v;
    }
    return lines.join(QLatin1Char('\n'));
}

inline QString toCsv(const Form &f, const QList<Response> &responses)
{
    const bool anyEdited = std::any_of(responses.begin(), responses.end(), [](const Response &r) { return r.edited > 0; });
    QStringList head{csvCell(QStringLiteral("#")), csvCell(QStringLiteral("Received")), csvCell(QStringLiteral("Device"))};
    for (const Field &x : f.fields)
        if (isQuestion(x))
            head << csvCell(x.label);
    const bool shelved = f.filesToShelf && std::any_of(f.fields.begin(), f.fields.end(), [](const Field &x) { return takesFiles(x); });
    if (shelved)
        head << csvCell(QStringLiteral("Names in Files"));
    if (anyEdited)
        head << csvCell(QStringLiteral("Changed"));
    QString out = head.join(QLatin1Char(',')) + QLatin1String("\r\n");
    for (const Response &r : responses) {
        QStringList row{QString::number(r.n), csvCell(QDateTime::fromSecsSinceEpoch(r.time).toString(Qt::ISODate)), csvCell(r.device)};
        for (const Field &x : f.fields)
            if (isQuestion(x))
                row << csvCell(cellText(x, r.answers.value(x.id)));
        if (shelved) {
            QStringList names;
            for (const Field &x : f.fields)
                if (takesFiles(x))
                    for (const QJsonValue &v : r.answers.value(x.id).toArray())
                        names << shelfName(f, r, x.id, v.toString());
            row << csvCell(names.join(QStringLiteral("; ")));
        }
        if (anyEdited)
            row << csvCell(r.edited > 0 ? QDateTime::fromSecsSinceEpoch(r.edited).toString(Qt::ISODate) : QString());
        out += row.join(QLatin1Char(',')) + QLatin1String("\r\n");
    }
    return out;
}

} // namespace Forms
