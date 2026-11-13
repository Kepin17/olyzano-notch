#include "FormStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

namespace {
bool writeJson(const QString &path, const QJsonObject &o)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

QJsonObject readJson(const QString &path, bool *ok)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        return {};
    }
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &err);
    if (ok) *ok = err.error == QJsonParseError::NoError && d.isObject();
    return d.object();
}
}

bool FormStore::validId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9-]{1,40}$"));
    return re.match(id).hasMatch();
}

QList<Forms::Form> FormStore::list() const
{
    QList<Forms::Form> out;
    for (const QString &name : QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const Forms::Form f = load(name, &ok);
        if (ok)
            out.append(f);
    }
    std::sort(out.begin(), out.end(), [](const Forms::Form &a, const Forms::Form &b) { return a.created > b.created; });
    return out;
}

Forms::Form FormStore::load(const QString &id, bool *ok) const
{
    if (!validId(id)) {
        if (ok) *ok = false;
        return {};
    }
    bool good = false;
    Forms::Form f = Forms::fromJson(readJson(dir(id) + QStringLiteral("/form.json"), &good));
    if (good && f.id != id)
        good = false; // the folder and the form must agree
    if (ok) *ok = good;
    return f;
}

bool FormStore::save(const Forms::Form &form)
{
    if (!validId(form.id) || !QDir().mkpath(dir(form.id)))
        return false;
    return writeJson(dir(form.id) + QStringLiteral("/form.json"), Forms::toJson(form));
}

bool FormStore::remove(const QString &id) { return validId(id) && QDir(dir(id)).removeRecursively(); }

int FormStore::responseCount(const QString &formId) const
{
    return validId(formId) ? int(QDir(dir(formId) + QStringLiteral("/responses")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size()) : 0;
}

QString FormStore::tempDir(const QString &formId) const
{
    if (!validId(formId))
        return {};
    const QString d = dir(formId) + QStringLiteral("/uploading");
    return QDir().mkpath(d) ? d : QString();
}

namespace {
// Moves the uploads into `folder`/files under their own names (two of one name: "report.pdf", "report (2).pdf"). False when one could not be moved.
bool keepUploads(const QString &folder, const QList<Forms::Upload> &uploads, QStringList &used, QHash<QString, QJsonArray> &kept)
{
    for (const Forms::Upload &u : uploads) {
        const QString wanted = Forms::sanitizeFileName(u.name);
        const QFileInfo info(wanted);
        const QString stem = info.completeBaseName().isEmpty() ? wanted : info.completeBaseName();
        const QString ext = info.completeBaseName().isEmpty() || info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
        QString name = wanted;
        for (int k = 2; used.contains(name, Qt::CaseInsensitive); ++k)
            name = stem + QStringLiteral(" (%1)").arg(k) + ext;
        used << name;
        const QString target = folder + QStringLiteral("/files/") + name;
        if (!(QFile::rename(u.path, target) || QFile::copy(u.path, target)))
            return false;
        QFile::remove(u.path);
        kept[u.field].append(name);
    }
    return true;
}
}

int FormStore::addResponse(const QString &formId, const QJsonObject &answers, const QList<Forms::Upload> &uploads, const QString &device, const QString &editHash)
{
    if (!validId(formId))
        return 0;
    int n = 0;
    for (const QString &name : QDir(dir(formId) + QStringLiteral("/responses")).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        n = qMax(n, name.toInt());
    ++n;
    const QString folder = dir(formId) + QStringLiteral("/responses/") + QString::number(n);
    if (!QDir().mkpath(folder + QStringLiteral("/files")))
        return 0;
    QStringList used;
    QHash<QString, QJsonArray> kept; // question -> the names the files were kept under
    if (!keepUploads(folder, uploads, used, kept)) {
        QDir(folder).removeRecursively();
        return 0;
    }
    QJsonObject stored = answers; // which file belongs to which question, under the name it was kept as
    for (auto it = kept.cbegin(); it != kept.cend(); ++it)
        stored.insert(it.key(), it.value());
    const QJsonObject doc{{QStringLiteral("n"), n}, {QStringLiteral("time"), double(QDateTime::currentSecsSinceEpoch())}, {QStringLiteral("device"), device},
                          {QStringLiteral("answers"), stored}};
    QJsonObject withEdit = doc;
    if (!editHash.isEmpty())
        withEdit.insert(QStringLiteral("edit"), editHash);
    if (!writeJson(folder + QStringLiteral("/answers.json"), withEdit)) {
        QDir(folder).removeRecursively();
        return 0;
    }
    return n;
}

QList<Forms::Response> FormStore::responses(const QString &formId) const
{
    QList<Forms::Response> out;
    if (!validId(formId))
        return out;
    for (const QString &name : QDir(dir(formId) + QStringLiteral("/responses")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const QJsonObject o = readJson(dir(formId) + QStringLiteral("/responses/") + name + QStringLiteral("/answers.json"), &ok);
        if (!ok)
            continue;
        out.append({o.value(QStringLiteral("n")).toInt(), o.value(QStringLiteral("time")).toVariant().toLongLong(), o.value(QStringLiteral("device")).toString(),
                    o.value(QStringLiteral("answers")).toObject(), o.value(QStringLiteral("edit")).toString(), o.value(QStringLiteral("edited")).toVariant().toLongLong(),
                    o.value(QStringLiteral("revisions")).toInt()});
    }
    std::sort(out.begin(), out.end(), [](const Forms::Response &a, const Forms::Response &b) { return a.n < b.n; });
    return out;
}

int FormStore::findByEdit(const QString &formId, const QString &hash) const
{
    if (!validId(formId) || hash.size() != 64)
        return 0;
    for (const Forms::Response &r : responses(formId))
        if (r.edit == hash)
            return r.n;
    return 0;
}

QJsonObject FormStore::response(const QString &formId, int n) const
{
    if (!validId(formId) || n < 1)
        return {};
    bool ok = false;
    const QJsonObject o = readJson(dir(formId) + QStringLiteral("/responses/") + QString::number(n) + QStringLiteral("/answers.json"), &ok);
    return ok ? o : QJsonObject();
}

bool FormStore::replaceResponse(const QString &formId, int n, const QJsonObject &answers, const QList<Forms::Upload> &uploads, const QString &device)
{
    QJsonObject doc = response(formId, n);
    if (doc.isEmpty())
        return false;
    const QString folder = dir(formId) + QStringLiteral("/responses/") + QString::number(n);
    const QJsonObject old = doc.value(QStringLiteral("answers")).toObject();
    QSet<QString> replaced;
    for (const Forms::Upload &u : uploads)
        replaced.insert(u.field);
    for (const QString &field : replaced) // the new files take the place of the old ones of that question
        for (const QJsonValue &name : old.value(field).toArray())
            QFile::remove(folder + QStringLiteral("/files/") + name.toString());
    QStringList used;
    for (auto it = old.constBegin(); it != old.constEnd(); ++it)
        if (it.value().isArray() && !replaced.contains(it.key()))
            for (const QJsonValue &name : it.value().toArray())
                used << name.toString(); // the files that stay keep their names
    QHash<QString, QJsonArray> kept;
    if (!keepUploads(folder, uploads, used, kept))
        return false;
    QJsonObject stored = answers;
    // (a file question that was not touched arrives in `answers` with the names it already has)
    for (auto it = kept.cbegin(); it != kept.cend(); ++it)
        stored.insert(it.key(), it.value());
    doc.insert(QStringLiteral("answers"), stored);
    doc.insert(QStringLiteral("device"), device);
    doc.insert(QStringLiteral("edited"), double(QDateTime::currentSecsSinceEpoch()));
    doc.insert(QStringLiteral("revisions"), doc.value(QStringLiteral("revisions")).toInt() + 1);
    return writeJson(folder + QStringLiteral("/answers.json"), doc);
}

QStringList FormStore::shelved(const QString &formId, int n) const
{
    if (!validId(formId) || n < 1)
        return {};
    QFile f(dir(formId) + QStringLiteral("/responses/") + QString::number(n) + QStringLiteral("/shelved.txt"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void FormStore::addShelved(const QString &formId, int n, const QStringList &names)
{
    if (!validId(formId) || n < 1 || names.isEmpty())
        return;
    QFile f(dir(formId) + QStringLiteral("/responses/") + QString::number(n) + QStringLiteral("/shelved.txt"));
    if (f.open(QIODevice::Append))
        f.write((names.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
}

QStringList FormStore::files(const QString &formId, int n) const
{
    QStringList out;
    if (!validId(formId) || n < 1)
        return out;
    const QDir d(dir(formId) + QStringLiteral("/responses/") + QString::number(n) + QStringLiteral("/files"));
    for (const QString &f : d.entryList(QDir::Files, QDir::Name))
        out << d.absoluteFilePath(f);
    return out;
}

bool FormStore::removeResponse(const QString &formId, int n)
{
    return validId(formId) && n >= 1 && QDir(dir(formId) + QStringLiteral("/responses/") + QString::number(n)).removeRecursively();
}

QJsonObject FormStore::relay(const QString &formId) const
{
    if (!validId(formId))
        return {};
    bool ok = false;
    const QJsonObject o = readJson(dir(formId) + QStringLiteral("/relay.json"), &ok);
    return ok ? o : QJsonObject();
}

bool FormStore::saveRelay(const QString &formId, const QJsonObject &o)
{
    if (!validId(formId) || !QDir().mkpath(dir(formId)))
        return false;
    const QString path = dir(formId) + QStringLiteral("/relay.json");
    const bool ok = writeJson(path, o);
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner); // it holds the key of the form
    return ok;
}

void FormStore::removeRelay(const QString &formId)
{
    if (validId(formId))
        QFile::remove(dir(formId) + QStringLiteral("/relay.json"));
}
