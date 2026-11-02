#pragma once

#include "platform/CodingAgentModel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSaveFile>

// What the agent's tools do to the workspace folder: list, read, search, propose and apply an edit, and the file list behind `@`.
// Plain functions with the workspace as an argument, so they are tested without an agent and the agent only decides when to call them.
namespace AgentTools {

constexpr qint64 FileLimit = 256 * 1024;

inline QString readText(const QString &path, bool *ok = nullptr)
{
    QFile f(path);
    const bool good = QFileInfo(path).isFile() && f.open(QIODevice::ReadOnly) && f.size() <= FileLimit;
    const QByteArray bytes = good ? f.read(FileLimit + 1) : QByteArray();
    if (ok) *ok = good && bytes.size() <= FileLimit && !bytes.contains('\0');
    return good ? QString::fromUtf8(bytes) : QString();
}

// Folders and files the agent never reads or lists, whatever the workspace.
inline bool privatePath(const QString &relative)
{
    const QStringList parts = relative.split('/');
    for (const QString &p : parts)
        if (p == ".git" || p == "node_modules" || p == ".ssh" || p == ".env" || p.startsWith(".env.") || p == "secrets.json") return true;
    return false;
}

inline QStringList fileIndex(const QString &workspace)
{
    QStringList out;
    if (workspace.isEmpty() || !QFileInfo(workspace).isDir()) return out;
    static const QStringList skip{".git", "node_modules", "build", "dist", "target", ".next", "__pycache__", ".venv", "venv", ".cache", ".gradle", ".idea"};
    QElapsedTimer clock; clock.start();
    QStringList dirs{workspace};
    while (!dirs.isEmpty() && out.size() < 5000 && clock.elapsed() < 300) {
        const QString dir = dirs.takeFirst(); // nearest folders first
        for (const auto &f : QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
            if (f.isSymLink()) continue;
            const QString rel = QDir(workspace).relativeFilePath(f.filePath());
            if (privatePath(rel)) continue;
            if (f.isDir()) { if (!skip.contains(f.fileName())) dirs.append(f.filePath()); }
            else if (out.size() < 5000) out.append(rel);
        }
    }
    return out;
}

inline QString listFiles(const QString &workspace, const QJsonObject &args, QString *summary)
{
    const QString path = CodingAgentModel::resolveWorkspacePath(workspace, args.value("path").toString("."));
    if (path.isEmpty() || !QFileInfo(path).isDir()) return "Error: directory outside workspace or missing.";
    QStringList out;
    for (const auto &f : QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::DirsFirst | QDir::Name)) {
        if (out.size() >= 300) { out.append("[truncated]"); break; }
        if (!privatePath(QDir(workspace).relativeFilePath(f.filePath()))) out.append(f.fileName() + (f.isDir() ? "/" : ""));
    }
    *summary = "List " + args.value("path").toString("."); return out.join('\n');
}

inline QString readFile(const QString &workspace, const QJsonObject &args, QString *summary)
{
    const QString path = CodingAgentModel::resolveWorkspacePath(workspace, args.value("path").toString());
    if (path.isEmpty() || privatePath(QDir(workspace).relativeFilePath(path))) return "Error: restricted path.";
    bool ok; const QString text = readText(path, &ok); if (!ok) return "Error: file unreadable, binary or larger than 256 KiB.";
    const auto lines = text.split('\n'); const int start = qMax(1, args.value("start_line").toInt(1));
    const int end = qMin(int(lines.size()), qMin(start + 499, args.value("end_line").toInt(start + 499)));
    QStringList out; for (int i = start; i <= end; ++i) out.append(QString::number(i) + "\t" + lines[i - 1]);
    if (end < lines.size()) out.append("[more lines available]");
    *summary = "Read " + args.value("path").toString(); return out.join('\n').left(64000);
}

inline QString searchFiles(const QString &workspace, const QJsonObject &args, QString *summary)
{
    const QString path = CodingAgentModel::resolveWorkspacePath(workspace, args.value("path").toString("."));
    const QString pattern = args.value("pattern").toString();
    if (path.isEmpty() || pattern.isEmpty()) return "Error: invalid search path or empty pattern.";
    // Literal search avoids untrusted regular expressions blocking the GUI thread.
    QStringList out; int visited = 0; QElapsedTimer clock; clock.start();
    QStringList dirs{path};
    while (!dirs.isEmpty() && visited < 2000 && clock.elapsed() < 400 && out.size() < 100) {
        const QString dir = dirs.takeLast();
        for (const auto &f : QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
            if (++visited > 2000 || clock.elapsed() >= 400 || out.size() >= 100) break;
            const QString rel = QDir(workspace).relativeFilePath(f.filePath());
            if (f.isSymLink() || privatePath(rel) || rel.startsWith("build/")) continue;
            if (f.isDir()) { dirs.append(f.filePath()); continue; }
            bool ok; const auto text = readText(f.filePath(), &ok); if (!ok) continue;
            const auto lines = text.split('\n');
            for (int i = 0; i < lines.size() && out.size() < 100; ++i) if (lines[i].contains(pattern, Qt::CaseInsensitive)) out.append(rel + ":" + QString::number(i + 1) + ": " + lines[i].left(300));
        }
    }
    *summary = "Search " + pattern; out.append("[bounded search: up to 2000 entries, 100 matches, 1.5 seconds]"); return out.join('\n');
}

// Fills the tool call with the edit as a diff to approve, or marks it as an error. Nothing is written.
inline void proposeEdit(const QString &workspace, CodingAgentModel::ToolCall &tc, const QJsonObject &args)
{
    tc.filePath = args.value("path").toString(); const QString path = CodingAgentModel::resolveWorkspacePath(workspace, tc.filePath);
    auto fail = [&tc](const QString &s) { tc.status = "error"; tc.output = "Error: " + s; };
    if (path.isEmpty() || privatePath(QDir(workspace).relativeFilePath(path))) { fail("restricted path"); return; }
    bool ok = true; QString old;
    if (QFileInfo::exists(path)) { old = readText(path, &ok); if (!ok) { fail("file unreadable, binary or too large"); return; } }
    tc.proposedContent = args.value("new_text").toString();
    if (!args.contains("new_text")) { fail("new_text is required"); return; }
    if (args.contains("old_text")) {
        const QString needle = args.value("old_text").toString();
        if (needle.isEmpty() || old.count(needle) != 1) { fail("old_text must match exactly once"); return; }
        tc.proposedContent = old; tc.proposedContent.replace(needle, args.value("new_text").toString());
    }
    if (tc.proposedContent.toUtf8().size() > FileLimit) { fail("proposed file too large"); return; }
    tc.originalHash = QCryptographicHash::hash(old.toUtf8(), QCryptographicHash::Sha256).toHex();
    tc.originalExists = QFileInfo::exists(path);
    const auto diff = CodingAgentModel::computeUnifiedDiff(tc.filePath, old, tc.proposedContent);
    tc.diffText = diff.unified; tc.addedLines = diff.added; tc.removedLines = diff.removed; tc.summary = args.value("description").toString();
}

// Writes the approved edit if the file is still what the proposal saw. "Error: ..." on failure, "Applied <path>" otherwise.
inline QString applyEdit(const QString &workspace, const CodingAgentModel::ToolCall &tc)
{
    const QString path = CodingAgentModel::resolveWorkspacePath(workspace, tc.filePath);
    if (path.isEmpty() || privatePath(QDir(workspace).relativeFilePath(path))) return "Error: restricted path.";
    bool ok = true; const bool exists = QFileInfo::exists(path); const QString old = exists ? readText(path, &ok) : QString();
    if (!ok || exists != tc.originalExists || QCryptographicHash::hash(old.toUtf8(), QCryptographicHash::Sha256).toHex() != tc.originalHash) return "Error: file changed since proposal; request a fresh edit.";
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return "Error: cannot create parent folder.";
    QSaveFile f(path); const QByteArray bytes = tc.proposedContent.toUtf8();
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) return "Error: cannot save file.";
    return "Applied " + tc.filePath;
}

} // namespace AgentTools
