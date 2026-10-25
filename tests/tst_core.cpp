#include "repositories/FileRepository.h"
#include "services/CleanupService.h"
#include "services/FileService.h"
#include "services/FolderWatcher.h"
#include "services/TextTools.h"
#include "services/DocTools.h"
#include "services/Invite.h"
#include "services/NearbyCrypto.h"
#include "services/NearbyService.h"
#include "services/QrImage.h"
#include "services/ShareCrypto.h"
#include "services/ShareServer.h"
#include "services/ZipWriter.h"
#include "services/Vault.h"
#include "platform/BatteryThreshold.h"
#include "platform/PlayerSelection.h"
#include "platform/WinPrivacy.h"
#include "platform/MediaNames.h"
#include "platform/NotificationFilter.h"
#include "platform/SoundsModel.h"
#include "platform/AudioBands.h"
#include "platform/DockModel.h"
#include "platform/WellbeingModel.h"

#include <QDir>
#include <QDirIterator>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QFile>
#include <QSqlDatabase>
#include <QSignalSpy>
#include <memory>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <QUuid>

#ifdef Q_OS_WIN
// File modes are not POSIX on Windows: a read-only folder still takes new files.
static bool permissionsEnforced() { return false; }
#else
#include <unistd.h>
static bool permissionsEnforced() { return ::geteuid() != 0; } // root ignores them
#endif

struct Http {
    int status = 0;
    QByteArray body;
    QByteArray contentRange, disposition;
};
static Http httpGet(const QUrl &url, const QByteArray &range = {}, const QByteArray &pin = {})
{
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    if (!range.isEmpty())
        req.setRawHeader("Range", range);
    if (!pin.isEmpty())
        req.setRawHeader("X-Pin", pin);
    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    Http h;
    h.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    h.body = reply->readAll();
    h.contentRange = reply->rawHeader("Content-Range");
    h.disposition = reply->rawHeader("Content-Disposition");
    reply->deleteLater();
    return h;
}

static QByteArray pattern(int size, int salt)
{
    QByteArray data;
    data.reserve(size);
    for (int i = 0; i < size; ++i)
        data.append(char(i * 31 + i / 251 + salt));
    return data;
}

// ---- helpers for the Nearby tests: two services in one process, talking over loopback ----
struct NearbyPair {
    std::unique_ptr<NearbyService> receiver, sender;
    NearbyService::Device device;
};

static NearbyPair makeNearbyPair(const QString &dir)
{
    NearbyPair p;
    p.receiver.reset(new NearbyService(dir + QStringLiteral("/nb-recv")));
    p.sender.reset(new NearbyService(dir + QStringLiteral("/nb-send")));
    p.receiver->setDevice(QStringLiteral("recv-id"), QStringLiteral("Alice's PC"));
    p.sender->setDevice(QStringLiteral("send-id"), QStringLiteral("Bob's laptop"));
    if (!p.receiver->startReceiving(60, 0).isEmpty())
        return p;
    QSignalSpy found(p.sender.get(), &NearbyService::deviceFound);
    p.sender->probe({QHostAddress::LocalHost}, p.receiver->discoveryPort());
    if (!found.wait(5000))
        return p;
    const QVariantMap d = found.first().first().toMap();
    p.device = {d.value(QStringLiteral("id")).toString(), d.value(QStringLiteral("name")).toString(), d.value(QStringLiteral("os")).toString(),
                QHostAddress(d.value(QStringLiteral("address")).toString()), quint16(d.value(QStringLiteral("port")).toInt())};
    return p;
}

static QList<ShareCrypto::SourceFile> makeNearbyFiles(const QString &dir, QMap<QString, QByteArray> *expected)
{
    QList<ShareCrypto::SourceFile> files;
    const QList<QPair<QString, QByteArray>> spec{{QStringLiteral("hello.txt"), QByteArrayLiteral("hi there")},
                                                 {QStringLiteral("empty.bin"), {}},
                                                 {QStringLiteral("big file.bin"), pattern(2 * 1024 * 1024 + 321, 9)}};
    int i = 0;
    for (const auto &s : spec) {
        expected->insert(s.first, s.second);
        const QString path = dir + QStringLiteral("/nbsrc%1").arg(i++);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(s.second);
        files.append({path, s.first, QStringLiteral("application/octet-stream")});
    }
    return files;
}

static QJsonObject takeFrame(QTcpSocket &s)
{
    const QByteArray head = s.read(4);
    const quint32 len = qFromBigEndian<quint32>(head.constData());
    return QJsonDocument::fromJson(s.read(len)).object();
}

class TestCore : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_files;
    FileRepository *m_repo = nullptr;

    QString writeSource(const QString &relPath, const QByteArray &content)
    {
        const QString path = m_tmp.filePath(QStringLiteral("src/") + relPath);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            qFatal("cannot write source");
        f.write(content);
        return path;
    }

    // Adds an item the same way the controller does: row first, then copy.
    FileItem addItem(const QString &src, const QDateTime &addedAt)
    {
        FileItem item;
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        item.originalName = QFileInfo(src).fileName();
        item.storedName = item.id + u'/' + item.originalName;
        item.sourcePath = src;
        item.addedAt = addedAt;
        item.status = Status::Copying;
        item.sizeBytes = QFileInfo(src).size();
        if (!m_repo->insert(item))
            qFatal("insert failed");
        const auto r = FileService::copyInto(src, m_files + u'/' + item.id, item.originalName);
        if (!r.ok || !m_repo->markAvailable(item.id, r.bytes))
            qFatal("copy failed");
        item.status = Status::Available;
        return item;
    }

    QString path(const FileItem &item) { return FileService::storedPath(m_files, item.storedName); }

private slots:
    void init()
    {
        QVERIFY(m_tmp.isValid());
        m_files = m_tmp.filePath(QStringLiteral("files"));
        QDir().mkpath(m_files);
        m_repo = new FileRepository;
        QVERIFY2(m_repo->open(m_tmp.filePath(QStringLiteral("notch.db"))), qPrintable(m_repo->errorString()));
    }

    void cleanup()
    {
        delete m_repo;
        m_repo = nullptr;
        m_tmp.remove();
        m_tmp = QTemporaryDir();
    }

    void migrationSetsVersionAndReopens()
    {
        delete m_repo;
        m_repo = new FileRepository;
        QVERIFY(m_repo->open(m_tmp.filePath(QStringLiteral("notch.db"))));
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("check"));
        db.setDatabaseName(m_tmp.filePath(QStringLiteral("notch.db")));
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("PRAGMA user_version")) && q.next());
        QCOMPARE(q.value(0).toInt(), 5);
        q = QSqlQuery();
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(QStringLiteral("check"));
    }

    void upgradesV1DatabaseWithBackup()
    {
        const QString path = m_tmp.filePath(QStringLiteral("v1.db"));
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v1"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QFile sql(QStringLiteral(":/migrations/001_initial.sql"));
            QVERIFY(sql.open(QIODevice::ReadOnly));
            QSqlQuery q(db);
            for (const QString &stmt : QString::fromUtf8(sql.readAll()).split(u';', Qt::SkipEmptyParts))
                if (!stmt.trimmed().isEmpty())
                    QVERIFY(q.exec(stmt));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO file_items (id, original_name, stored_name, size_bytes, added_at, status) "
                                          "VALUES ('a', 'old.txt', 'a/old.txt', 1, '2026-01-01T00:00:00.000Z', 'available')")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 1")));
            q = QSqlQuery();
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v1"));

        FileRepository repo;
        QVERIFY2(repo.open(path), qPrintable(repo.errorString()));
        QVERIFY(QFile::exists(path + QStringLiteral(".bak-v1")));
        const QList<FileItem> items = repo.withStatus(Status::Available);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().originalName, QStringLiteral("old.txt"));
        QVERIFY(items.first().categoryId.isEmpty());
    }

    void corruptDatabaseRefusesToOpen()
    {
        const QString bad = writeSource(QStringLiteral("bad.db"), QByteArray(4096, 'x'));
        FileRepository repo;
        QVERIFY(!repo.open(bad));
        QVERIFY(!repo.errorString().isEmpty());
        QCOMPARE(QFileInfo(bad).size(), 4096); // untouched
    }

    void copyKeepsSourceAndLeavesNoTmp()
    {
        const QString src = writeSource(QStringLiteral("a.txt"), "hello");
        const FileItem item = addItem(src, QDateTime::currentDateTimeUtc());
        QFile f(path(item));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("hello"));
        QVERIFY(QFile::exists(src));
        QVERIFY(!QFile::exists(m_files + u'/' + item.id + u'/' + FileService::TmpName));
    }

    void copyLargeFile()
    {
        const QString src = m_tmp.filePath(QStringLiteral("big.bin"));
        QFile f(src);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.resize(64ll << 20)); // sparse, 64 MiB
        f.close();
        const auto r = FileService::copyInto(src, m_files + QStringLiteral("/big"), QStringLiteral("big.bin"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.bytes, 64ll << 20);
        QCOMPARE(QFileInfo(m_files + QStringLiteral("/big/big.bin")).size(), 64ll << 20);
    }

    void duplicateNamesDoNotOverwrite()
    {
        const FileItem a = addItem(writeSource(QStringLiteral("x/report.pdf"), "one"), QDateTime::currentDateTimeUtc());
        const FileItem b = addItem(writeSource(QStringLiteral("y/report.pdf"), "two"), QDateTime::currentDateTimeUtc());
        QVERIFY(a.storedName != b.storedName);
        QFile fa(path(a)), fb(path(b));
        QVERIFY(fa.open(QIODevice::ReadOnly) && fb.open(QIODevice::ReadOnly));
        QCOMPARE(fa.readAll(), QByteArray("one"));
        QCOMPARE(fb.readAll(), QByteArray("two"));
        QCOMPARE(m_repo->visible().size(), 2);
    }

    void invalidSourcesRejected()
    {
        QVERIFY(!FileService::validateSource(m_tmp.filePath(QStringLiteral("nope"))).isEmpty());
        QVERIFY(!FileService::validateSource(m_tmp.path()).isEmpty());
        QVERIFY(!FileService::validateSource(QStringLiteral("/dev/null")).isEmpty());
        const QString locked = writeSource(QStringLiteral("locked.txt"), "x");
        QFile::setPermissions(locked, {});
        if (permissionsEnforced())
            QVERIFY(!FileService::validateSource(locked).isEmpty());
        QVERIFY(FileService::validateSource(writeSource(QStringLiteral("ok.txt"), "x")).isEmpty());
    }

    void failedCopyLeavesNothing()
    {
        if (!permissionsEnforced())
            QSKIP("permissions are not enforced here (root, or Windows)");
        const QString ro = m_tmp.filePath(QStringLiteral("ro"));
        QDir().mkpath(ro + QStringLiteral("/item"));
        QFile::setPermissions(ro + QStringLiteral("/item"), QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        const auto r = FileService::copyInto(writeSource(QStringLiteral("c.txt"), "data"), ro + QStringLiteral("/item"),
                                             QStringLiteral("c.txt"));
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(QDir(ro + QStringLiteral("/item")).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));
        QFile::setPermissions(ro + QStringLiteral("/item"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    void cleanupRespectsRetention()
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QString srcOld = writeSource(QStringLiteral("old.txt"), "old");
        const FileItem old = addItem(srcOld, now.addDays(-7));
        const FileItem fresh = addItem(writeSource(QStringLiteral("fresh.txt"), "new"), now.addDays(-7).addSecs(60));

        const QStringList removed = CleanupService::purge(*m_repo, m_files, now, DefaultRetentionDays);
        QCOMPARE(removed, QStringList{old.id});
        QVERIFY(!QFile::exists(path(old)));
        QVERIFY(!QDir(m_files + u'/' + old.id).exists());
        QVERIFY(QFile::exists(path(fresh)));
        QVERIFY(QFile::exists(srcOld));
        QCOMPARE(m_repo->visible().size(), 1);
        QVERIFY(m_repo->withStatus(Status::PendingCleanup).isEmpty());
    }

    void cleanupSkipsBusyItems()
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const FileItem item = addItem(writeSource(QStringLiteral("d.txt"), "d"), now.addDays(-10));
        QVERIFY(CleanupService::purge(*m_repo, m_files, now, DefaultRetentionDays, {item.id}).isEmpty());
        QVERIFY(QFile::exists(path(item)));
        QCOMPARE(CleanupService::purge(*m_repo, m_files, now, DefaultRetentionDays), QStringList{item.id});
    }

    void cleanupUsesConfiguredRetention()
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const FileItem item = addItem(writeSource(QStringLiteral("e.txt"), "e"), now.addDays(-2));
        QVERIFY(CleanupService::purge(*m_repo, m_files, now, 3).isEmpty());
        QCOMPARE(CleanupService::purge(*m_repo, m_files, now, 1), QStringList{item.id});
        QVERIFY(!QFile::exists(path(item)));
    }

    void categoriesAssignAndDelete()
    {
        const FileItem item = addItem(writeSource(QStringLiteral("k.txt"), "k"), QDateTime::currentDateTimeUtc());
        QVERIFY(m_repo->insertCategory({QStringLiteral("c1"), QStringLiteral("Docs")}));
        QVERIFY(!m_repo->insertCategory({QStringLiteral("c2"), QStringLiteral("docs")})); // names are case-insensitive unique
        QCOMPARE(m_repo->categoryIdByName(QStringLiteral("DOCS")), QStringLiteral("c1"));
        QVERIFY(m_repo->setCategory(item.id, QStringLiteral("c1")));
        QCOMPARE(m_repo->visible().first().categoryId, QStringLiteral("c1"));
        QVERIFY(m_repo->renameCategory(QStringLiteral("c1"), QStringLiteral("Papers")));
        QCOMPARE(m_repo->categories().first().name, QStringLiteral("Papers"));
        QVERIFY(m_repo->deleteCategory(QStringLiteral("c1")));
        QVERIFY(m_repo->categories().isEmpty());
        QCOMPARE(m_repo->visible().size(), 1); // the file stays
        QVERIFY(m_repo->visible().first().categoryId.isEmpty());
        QVERIFY(QFile::exists(path(item)));
    }

    void expandFolderCategoriesByParent()
    {
        writeSource(QStringLiteral("Proj/a.txt"), "a");
        writeSource(QStringLiteral("Proj/sub/b.txt"), "b");
        writeSource(QStringLiteral("Proj/sub/deep/c.txt"), "c");
        writeSource(QStringLiteral("Proj/.git/config"), "hidden dir");
        writeSource(QStringLiteral("Proj/.env"), "hidden file");
        const QString root = m_tmp.filePath(QStringLiteral("src/Proj"));

        bool truncated = true;
        const auto entries = FileService::expandFolder(root, 100, &truncated);
        QVERIFY(!truncated);
        QCOMPARE(entries.size(), 3);
        QCOMPARE(QFileInfo(entries[0].path).fileName(), QStringLiteral("a.txt"));
        QCOMPARE(entries[0].category, QStringLiteral("Proj"));
        QCOMPARE(entries[1].category, QStringLiteral("sub"));  // Proj/sub/b.txt
        QCOMPARE(entries[2].category, QStringLiteral("deep")); // Proj/sub/deep/c.txt

        QCOMPARE(FileService::expandFolder(root, 2, &truncated).size(), 2);
        QVERIFY(truncated);
    }

    void namingHelpers()
    {
        QCOMPARE(FileService::safeFileName(QStringLiteral("../a/b")), QStringLiteral("_a_b"));
        QCOMPARE(FileService::safeFileName(QStringLiteral("...")), QStringLiteral("file"));
        QCOMPARE(FileService::safeFileName(FileService::TmpName), QStringLiteral("notch-tmp"));
        QCOMPARE(FileService::downloadName(QUrl(QStringLiteral("https://x.org/img/cat.jpg?s=1")), QStringLiteral("image/jpeg")),
                 QStringLiteral("cat.jpg"));
        QCOMPARE(FileService::downloadName(QUrl(QStringLiteral("https://x.org/photo")), QStringLiteral("image/png")),
                 QStringLiteral("photo.png"));
        QCOMPARE(FileService::downloadName(QUrl(QStringLiteral("data:image/png;base64,AAAA")), QStringLiteral("image/png")),
                 QStringLiteral("download.png"));
        QVERIFY(FileService::isValidStoredName(QUuid::createUuid().toString(QUuid::WithoutBraces) + u'/'
                                               + FileService::safeFileName(QStringLiteral("a/../b"))));

        const QUrl page(QStringLiteral("https://id.jobstreet.com/career-advice/logo"));
        QCOMPARE(FileService::imageFromHtml(QStringLiteral("<a href=\"x\"><IMG alt='a' SRC='/img/logo.png?w=1&amp;h=2'></a>"), page),
                 QUrl(QStringLiteral("https://id.jobstreet.com/img/logo.png?w=1&h=2")));
        QCOMPARE(FileService::imageFromHtml(QStringLiteral("<img src=\"data:image/png;base64,AAAA\">"), page).scheme(),
                 QStringLiteral("data"));
        QVERIFY(!FileService::imageFromHtml(QStringLiteral("<a href=\"https://x.org\">Title</a>"), page).isValid());
        QVERIFY(!FileService::imageFromHtml(QStringLiteral("<img src=\"javascript:alert(1)\">"), page).isValid());

        const QByteArray body = FileService::linkFile(QUrl(QStringLiteral("https://example.org/a b")));
        QVERIFY(body.contains("Type=Link\n"));
        QVERIFY(body.contains("Name=example.org/a b\n"));
        QVERIFY(body.contains("URL=https://example.org/a%20b\n"));
        const QString dir = m_tmp.filePath(QStringLiteral("w"));
        QVERIFY(FileService::writeFile(dir, QStringLiteral("x.bin"), QByteArray("\0\1", 2)));
        QCOMPARE(QFileInfo(dir + QStringLiteral("/x.bin")).size(), 2);

        QCOMPARE(FileService::textExtension(QStringLiteral("print(1)"), QStringLiteral("python")), QStringLiteral("py"));
        QCOMPARE(FileService::textExtension(QStringLiteral(" {\"a\": [1, 2]} ")), QStringLiteral("json"));
        QCOMPARE(FileService::textExtension(QStringLiteral("{not json")), QStringLiteral("txt"));
        QCOMPARE(FileService::textExtension(QStringLiteral("hello")), QStringLiteral("txt"));
    }

    void keptItemsNeverExpire()
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const FileItem kept = addItem(writeSource(QStringLiteral("keep.txt"), "k"), now.addDays(-30));
        const FileItem old = addItem(writeSource(QStringLiteral("old.txt"), "o"), now.addDays(-30));
        QVERIFY(m_repo->setPinned(kept.id, true));
        QCOMPARE(CleanupService::purge(*m_repo, m_files, now, DefaultRetentionDays), QStringList{old.id});
        QVERIFY(QFile::exists(path(kept)));
        QVERIFY(m_repo->visible().first().pinned);
        QVERIFY(m_repo->setPinned(kept.id, false));
        QCOMPARE(CleanupService::purge(*m_repo, m_files, now, DefaultRetentionDays), QStringList{kept.id});
    }

    void consentStoreAppNames()
    {
        QCOMPARE(WinPrivacy::appName(QStringLiteral("C:#Program Files#Zoom#bin#Zoom.exe")), QStringLiteral("Zoom"));
        QCOMPARE(WinPrivacy::appName(QStringLiteral("C:#Users#me#AppData#Local#Discord#app-1.0#Discord.EXE")), QStringLiteral("Discord"));
        QCOMPARE(WinPrivacy::appName(QStringLiteral("Microsoft.WindowsCamera_8wekyb3d8bbwe")), QStringLiteral("WindowsCamera"));
        QCOMPARE(WinPrivacy::appName(QStringLiteral("5319275A.WhatsAppDesktop_cv1g1gvanyjgm")), QStringLiteral("WhatsAppDesktop"));
    }

    void mediaSessionNames()
    {
        QCOMPARE(MediaNames::displayName(QStringLiteral("Chrome")), QStringLiteral("Chrome"));
        QCOMPARE(MediaNames::displayName(QStringLiteral("Spotify.exe")), QStringLiteral("Spotify"));
        QCOMPARE(MediaNames::displayName(QStringLiteral("308046B0AF4A39CB")), QStringLiteral("Firefox"));
        QCOMPARE(MediaNames::displayName(QStringLiteral("Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic")),
                 QStringLiteral("Media Player"));
        QCOMPARE(MediaNames::displayName(QStringLiteral("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify")), QStringLiteral("Spotify"));
        QCOMPARE(MediaNames::displayName(QStringLiteral("C:\\Program Files\\foobar2000\\foobar2000.exe")), QStringLiteral("Foobar2000"));
        QCOMPARE(MediaNames::processName(QStringLiteral("MSEdge")), QStringLiteral("msedge"));
        QCOMPARE(MediaNames::processName(QStringLiteral("308046B0AF4A39CB")), QStringLiteral("firefox"));
        QCOMPARE(MediaNames::processName(QStringLiteral("Spotify.exe")), QStringLiteral("spotify"));
    }

    void soundRules()
    {
        using namespace SoundsModel;
        QCOMPARE(groupOf(QStringLiteral("drop")), Shelf);
        QCOMPARE(groupOf(QStringLiteral("break-over")), Timer);
        QCOMPARE(groupOf(QStringLiteral("ring")), Phone);
        QCOMPARE(groupOf(QStringLiteral("plug-out")), System);
        QCOMPARE(groupOf(QStringLiteral("nope")), Unknown);

        State s;
        QVERIFY(shouldPlay(QStringLiteral("drop"), s));
        QVERIFY(!shouldPlay(QStringLiteral("nope"), s));
        s.enabled = false;
        QVERIFY(!shouldPlay(QStringLiteral("drop"), s));
        s = State();
        s.groupOn = false;
        QVERIFY(!shouldPlay(QStringLiteral("drop"), s));
        s = State();
        s.sinceSameMs = 120; // the same event seen by a second screen's notch
        QVERIFY(!shouldPlay(QStringLiteral("notify"), s));
        s.sinceSameMs = 900;
        QVERIFY(shouldPlay(QStringLiteral("notify"), s));
        s = State();
        s.micInUse = true; // in a call: nothing, not even another call
        QVERIFY(!shouldPlay(QStringLiteral("notify"), s));
        QVERIFY(!shouldPlay(QStringLiteral("ring"), s));
        s = State();
        s.fullScreen = true; // a video: only a call gets through
        QVERIFY(!shouldPlay(QStringLiteral("timer"), s));
        QVERIFY(shouldPlay(QStringLiteral("ring"), s));
        s.quietWhenBusy = false;
        QVERIFY(shouldPlay(QStringLiteral("timer"), s));
    }

    // The files tools/make_sounds.py wrote: one per sound, short mono 16-bit PCM.
    void soundFiles()
    {
        const QStringList names = {"drop", "receive", "send", "ring", "notify", "timer", "stretch", "break-over",
                                   "plug-in", "plug-out", "device", "warning", "trash", "pick"};
        for (const QString &name : names) {
            QVERIFY2(SoundsModel::groupOf(name) != SoundsModel::Unknown, qPrintable(name));
            QFile f(QStringLiteral(NOTCH_SOURCE_DIR "/src/qml/sounds/") + name + QStringLiteral(".wav"));
            QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(name));
            const QByteArray wav = f.readAll();
            QVERIFY(wav.size() > 44);
            QCOMPARE(wav.mid(0, 4), QByteArray("RIFF"));
            QCOMPARE(wav.mid(8, 4), QByteArray("WAVE"));
            QCOMPARE(qFromLittleEndian<quint16>(wav.constData() + 20), quint16(1));  // PCM
            QCOMPARE(qFromLittleEndian<quint16>(wav.constData() + 22), quint16(1));  // mono
            QCOMPARE(qFromLittleEndian<quint16>(wav.constData() + 34), quint16(16)); // bits
            const quint32 rate = qFromLittleEndian<quint32>(wav.constData() + 24);
            const qsizetype data = wav.indexOf("data", 12);
            QVERIFY(data > 0);
            const quint32 bytes = qFromLittleEndian<quint32>(wav.constData() + data + 4);
            const double seconds = bytes / 2.0 / rate;
            QVERIFY2(seconds > 0.05 && seconds < 1.5, qPrintable(name));
        }
    }

    void notificationFilter()
    {
        NotificationFilter f;
        QVERIFY(f.accept(QStringLiteral("Slack"), QStringLiteral("John"), 1000));
        QVERIFY(!f.accept(QStringLiteral("Slack"), QStringLiteral("John"), 1500)); // posted twice
        QVERIFY(f.accept(QStringLiteral("Slack"), QStringLiteral("John"), 4000));  // a new one later
        QVERIFY(f.accept(QStringLiteral("Slack"), QStringLiteral("Mary"), 4100));
        QVERIFY(!f.accept(QStringLiteral("Notch"), QStringLiteral("x"), 5000));
        QVERIFY(!f.accept(QStringLiteral("Olyzano Notch"), QStringLiteral("x"), 5000));
        QVERIFY(!f.accept(QStringLiteral("KDE Connect"), QStringLiteral("Mom"), 5000));
        QVERIFY(!f.accept(QStringLiteral("kdeconnect.app"), QStringLiteral("Mom"), 5000));
        QVERIFY(!f.accept(QString(), QString(), 5000));
        QVERIFY(f.accept(QStringLiteral("Discord"), QStringLiteral("hi"), 5000)); // every other app comes through
    }

    void lowBatteryThresholds()
    {
        QCOMPARE(crossedLowBattery(21, 19), 20);
        QCOMPARE(crossedLowBattery(19, 18), 0);  // already warned at 20
        QCOMPARE(crossedLowBattery(11, 10), 10);
        QCOMPARE(crossedLowBattery(12, 4), 5);   // lowest crossed wins
        QCOMPARE(crossedLowBattery(-1, 15), 0);  // unknown before: no warning on first reading
        QCOMPARE(crossedLowBattery(15, 30), 0);  // charging back up
    }

    void playerSelection()
    {
        const QString chrome = QStringLiteral("org.mpris.MediaPlayer2.chromium.instance1");
        const QString spotify = QStringLiteral("org.mpris.MediaPlayer2.spotify");
        const QString elisa = QStringLiteral("org.mpris.MediaPlayer2.elisa");
        const QString pbi = QStringLiteral("org.mpris.MediaPlayer2.plasma-browser-integration");
        const QString playing = QStringLiteral("Playing"), paused = QStringLiteral("Paused"), stopped = QStringLiteral("Stopped");

        QVERIFY(pickPlayer({}).isEmpty());
        QVERIFY(pickPlayer({{elisa, stopped, 9}}).isEmpty());                       // stopped never shows
        QCOMPARE(pickPlayer({{spotify, paused, 9}, {chrome, playing, 1}}), chrome);  // playing beats newer paused
        QCOMPARE(pickPlayer({{spotify, playing, 5}, {chrome, playing, 7}}), chrome); // most recent playing
        QCOMPARE(pickPlayer({{spotify, paused, 5}, {elisa, paused, 7}}), elisa);     // else most recent paused
        QCOMPARE(pickPlayer({{spotify, paused, 5}, {chrome, playing, 7}}, spotify), spotify); // manual choice
        QCOMPARE(pickPlayer({{chrome, playing, 7}}, elisa), chrome);                 // manual gone: back to rule
        QCOMPARE(pickPlayer({{chrome, playing, 9}, {pbi, playing, 1}}), pbi);        // browser shown once
        QCOMPARE(visiblePlayers({{chrome, playing, 9}, {pbi, playing, 1}, {spotify, paused, 3}}).size(), 2);
    }

    void audioBands()
    {
        constexpr int n = 512, rate = 11025;
        auto sine = [&](double hz) {
            std::vector<float> s(n);
            for (int i = 0; i < n; ++i)
                s[i] = float(0.5 * std::sin(2 * M_PI * hz * i / rate));
            return s;
        };
        auto peak = [](const QList<double> &l) { return int(std::max_element(l.begin(), l.end()) - l.begin()); };

        const auto low = bandLevels(sine(250).data(), n, rate);
        QCOMPARE(peak(low), 1);
        QVERIFY(low[1] > 0.85);              // ~-6 dBFS
        const auto high = bandLevels(sine(5000).data(), n, rate);
        QCOMPARE(peak(high), BandCount - 1);
        const std::vector<float> silence(n, 0.0f);
        for (double v : bandLevels(silence.data(), n, rate))
            QCOMPARE(v, 0.0);
    }

    void renameAndNamingHelpers()
    {
        const FileItem item = addItem(writeSource(QStringLiteral("png.png"), "img"), QDateTime::currentDateTimeUtc());
        const QString newStored = item.id + QStringLiteral("/Cat photo.png");
        QVERIFY(QFile::rename(path(item), FileService::storedPath(m_files, newStored)));
        QVERIFY(m_repo->renameItem(item.id, QStringLiteral("Cat photo.png"), newStored, QStringLiteral("image/png")));
        QCOMPARE(m_repo->visible().first().mimeType, QStringLiteral("image/png"));
        QCOMPARE(m_repo->visible().first().originalName, QStringLiteral("Cat photo.png"));
        QVERIFY(FileService::removeManaged(m_files, newStored)); // still a valid managed name

        const QString dir = m_tmp.filePath(QStringLiteral("dest"));
        QDir().mkpath(dir);
        QCOMPARE(FileService::uniqueName(dir, QStringLiteral("a.txt")), QStringLiteral("a.txt"));
        QVERIFY(QFile(dir + QStringLiteral("/a.txt")).open(QIODevice::WriteOnly));
        QVERIFY(QFile(dir + QStringLiteral("/a (1).txt")).open(QIODevice::WriteOnly));
        QCOMPARE(FileService::uniqueName(dir, QStringLiteral("a.txt")), QStringLiteral("a (2).txt"));
        QVERIFY(QFile(dir + QStringLiteral("/README")).open(QIODevice::WriteOnly));
        QCOMPARE(FileService::uniqueName(dir, QStringLiteral("README")), QStringLiteral("README (1)"));

        const QSet<QString> shelf{QStringLiteral("report.pdf"), QStringLiteral("report (1).pdf")};
        auto taken = [&](const QString &n) { return shelf.contains(n); };
        QCOMPARE(FileService::uniqueAmong(QStringLiteral("report.pdf"), taken), QStringLiteral("report (2).pdf"));
        QCOMPARE(FileService::uniqueAmong(QStringLiteral("other.pdf"), taken), QStringLiteral("other.pdf"));

        QVERIFY(FileService::isPartialDownload(QStringLiteral("movie.mp4.crdownload")));
        QVERIFY(FileService::isPartialDownload(QStringLiteral("x.part")));
        QVERIFY(FileService::isPartialDownload(QStringLiteral(".hidden")));
        QVERIFY(!FileService::isPartialDownload(QStringLiteral("report.pdf")));
    }

    void folderWatcherReportsFinishedFiles()
    {
        const QString dir = m_tmp.filePath(QStringLiteral("watched"));
        QDir().mkpath(dir);
        QFile old(dir + QStringLiteral("/old.txt"));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("already here");
        old.close();

        FolderWatcher watcher;
        watcher.watch(dir, 300);
        QSignalSpy spy(&watcher, &FolderWatcher::fileReady);

        QFile partial(dir + QStringLiteral("/movie.mp4.crdownload"));
        QVERIFY(partial.open(QIODevice::WriteOnly));
        partial.write("half");
        partial.close();
        QFile fresh(dir + QStringLiteral("/shot.png"));
        QVERIFY(fresh.open(QIODevice::WriteOnly));
        fresh.write("png bytes");
        fresh.close();

        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 5000);
        QCOMPARE(QFileInfo(spy.first().first().toString()).fileName(), QStringLiteral("shot.png"));
        QVERIFY(QFile::rename(dir + QStringLiteral("/movie.mp4.crdownload"), dir + QStringLiteral("/movie.mp4")));
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 2, 5000); // the finished download
        QCOMPARE(QFileInfo(spy.last().first().toString()).fileName(), QStringLiteral("movie.mp4"));
        QTest::qWait(800);
        QCOMPARE(spy.size(), 2); // reported once each, old.txt never
    }

    void recentlyRemovedLastsADay()
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const FileItem old = addItem(writeSource(QStringLiteral("old.txt"), "o"), now.addDays(-30)); // past retention
        const FileItem fresh = addItem(writeSource(QStringLiteral("fresh.txt"), "f"), now);
        const FileItem gone = addItem(writeSource(QStringLiteral("gone.txt"), "g"), now);
        QVERIFY(m_repo->setRemoved(old.id, now.addSecs(-3600)));        // removed 1 h ago
        QVERIFY(m_repo->setRemoved(gone.id, now.addSecs(-25 * 3600)));  // removed 25 h ago

        // Retention skips removed items (old.txt waits for its 24 h), the 25 h one is emptied.
        QCOMPARE(CleanupService::purge(*m_repo, m_files, now, DefaultRetentionDays), QStringList{gone.id});
        QVERIFY(QFile::exists(path(old)));
        QVERIFY(!QFile::exists(path(gone)));
        QVERIFY(QFile::exists(path(fresh)));

        QVERIFY(m_repo->setRemoved(old.id, QDateTime())); // restore: back under normal retention
        QCOMPARE(CleanupService::purge(*m_repo, m_files, now, DefaultRetentionDays), QStringList{old.id});
    }

    void textTools()
    {
        QCOMPARE(transformText(QStringLiteral("json"), QStringLiteral("{\"a\":[1,2]}")).text,
                 QStringLiteral("{\n    \"a\": [\n        1,\n        2\n    ]\n}"));
        QVERIFY(!transformText(QStringLiteral("json"), QStringLiteral("{oops")).ok);
        // Key order is kept as written, strings (with : , { and escaped quotes) untouched, {} stays inline.
        const QString messy = QStringLiteral("{\"z\":1, \"a\":\"x: {y}, \\\"q\\\"\", \"e\":{}, \"l\":[]}");
        const QString pretty = QStringLiteral("{\n    \"z\": 1,\n    \"a\": \"x: {y}, \\\"q\\\"\",\n    \"e\": {},\n    \"l\": []\n}");
        QCOMPARE(transformText(QStringLiteral("json"), messy).text, pretty);
        QCOMPARE(transformText(QStringLiteral("base64-encode"), QStringLiteral("héllo")).text, QStringLiteral("aMOpbGxv"));
        QCOMPARE(transformText(QStringLiteral("base64-decode"), QStringLiteral("aMOpbGxv")).text, QStringLiteral("héllo"));
        QVERIFY(!transformText(QStringLiteral("base64-decode"), QStringLiteral("not base64!!")).ok);
        QCOMPARE(transformText(QStringLiteral("url-encode"), QStringLiteral("a b&c=é")).text, QStringLiteral("a%20b%26c%3D%C3%A9"));
        QCOMPARE(transformText(QStringLiteral("url-decode"), QStringLiteral("a%20b%26c%3D%C3%A9")).text, QStringLiteral("a b&c=é"));
        QCOMPARE(transformText(QStringLiteral("upper"), QStringLiteral("Notch é")).text, QStringLiteral("NOTCH É"));
        QCOMPARE(transformText(QStringLiteral("lower"), QStringLiteral("Notch É")).text, QStringLiteral("notch é"));
    }

    void pageRanges()
    {
        QCOMPARE(DocTools::parsePageRanges(QStringLiteral("2-5, 8, 10-"), 12), (QList<int>{2, 3, 4, 5, 8, 10, 11, 12}));
        QCOMPARE(DocTools::parsePageRanges(QStringLiteral("3,3,1"), 5), (QList<int>{3, 1}));
        QCOMPARE(DocTools::parsePageRanges(QStringLiteral("-2"), 5), (QList<int>{1, 2}));
        QString err;
        QVERIFY(DocTools::parsePageRanges(QStringLiteral("0-2"), 5, &err).isEmpty());
        QVERIFY(!err.isEmpty());
        QVERIFY(DocTools::parsePageRanges(QStringLiteral("abc"), 5).isEmpty());
        QVERIFY(DocTools::parsePageRanges(QStringLiteral("7"), 5).isEmpty());
    }

    void pdfTools()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("pdfunite")).isEmpty())
            QSKIP("poppler-utils not installed");
        // Two pictures (one landscape) -> 2-page PDF.
        QImage portrait(600, 900, QImage::Format_RGB32), landscape(900, 600, QImage::Format_RGB32);
        portrait.fill(Qt::red);
        landscape.fill(Qt::blue);
        const QString a = m_tmp.filePath(QStringLiteral("a.png")), b = m_tmp.filePath(QStringLiteral("b.jpg"));
        QVERIFY(portrait.save(a) && landscape.save(b));
        const QString two = m_tmp.filePath(QStringLiteral("two.pdf"));
        QCOMPARE(DocTools::imagesToPdf({a, b}, two), QString());
        QCOMPARE(DocTools::pdfPageCount(two), 2);

        const QString four = m_tmp.filePath(QStringLiteral("four.pdf"));
        QCOMPARE(DocTools::mergePdfs({two, two}, four), QString());
        QCOMPARE(DocTools::pdfPageCount(four), 4);
        QVERIFY(!DocTools::mergePdfs({two}, four).isEmpty()); // needs two

        const QString some = m_tmp.filePath(QStringLiteral("some.pdf"));
        QCOMPARE(DocTools::extractPages(four, {4, 1}, some), QString());
        QCOMPARE(DocTools::pdfPageCount(some), 2);

        const QString dir = m_tmp.filePath(QStringLiteral("split"));
        QDir().mkpath(dir);
        QStringList pages;
        QCOMPARE(DocTools::splitPdf(two, dir, QStringLiteral("two"), &pages), QString());
        QCOMPARE(pages.size(), 2);
        for (const QString &page : pages)
            QCOMPARE(DocTools::pdfPageCount(page), 1);
    }

    void toolRunner()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("pdfunite")).isEmpty())
            QSKIP("poppler-utils not installed");
        QImage img(800, 400, QImage::Format_RGB32);
        img.fill(Qt::darkGreen);
        const QString png = m_tmp.filePath(QStringLiteral("card.png"));
        QVERIFY(img.save(png));
        const QString work = m_tmp.filePath(QStringLiteral("work"));
        QDir().mkpath(work);
        const DocTools::ToolInput card{png, QStringLiteral("card"), QStringLiteral("card.png")};

        auto pdf = DocTools::runTool(QStringLiteral("images-pdf"), {card}, {}, work);
        QCOMPARE(pdf.error, QString());
        QCOMPARE(pdf.outputs.size(), 1);
        QCOMPARE(pdf.outputs.first().second, QStringLiteral("card.pdf"));

        const DocTools::ToolInput doc{pdf.outputs.first().first, QStringLiteral("card"), QStringLiteral("card.pdf")};
        auto merged = DocTools::runTool(QStringLiteral("merge"), {doc, doc}, {}, work);
        QCOMPARE(merged.error, QString());
        QCOMPARE(DocTools::pdfPageCount(merged.outputs.first().first), 2);
        QCOMPARE(merged.summary, QStringLiteral("Merged 2 PDFs"));

        auto resized = DocTools::runTool(QStringLiteral("resize"), {card}, QStringLiteral("400"), work);
        QCOMPARE(resized.outputs.first().second, QStringLiteral("card (400px).png"));
        QCOMPARE(QImageReader(resized.outputs.first().first).size().width(), 400);

        QVERIFY(!DocTools::runTool(QStringLiteral("nonsense"), {card}, {}, work).error.isEmpty());
        QVERIFY(!DocTools::runTool(QStringLiteral("extract"), {doc}, QStringLiteral("9"), work).error.isEmpty()); // 1 page only
    }

    void compressPdf()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("gs")).isEmpty())
            QSKIP("ghostscript not installed");
        QImage noise(1800, 1800, QImage::Format_RGB32); // incompressible enough to be large
        for (int y = 0; y < noise.height(); ++y)
            for (int x = 0; x < noise.width(); ++x)
                noise.setPixel(x, y, QRandomGenerator::global()->generate());
        const QString img = m_tmp.filePath(QStringLiteral("noise.png"));
        QVERIFY(noise.save(img));
        const QString big = m_tmp.filePath(QStringLiteral("big.pdf")), small = m_tmp.filePath(QStringLiteral("small.pdf"));
        QCOMPARE(DocTools::imagesToPdf({img}, big), QString());
        QCOMPARE(DocTools::compressPdf(big, small), QString());
        QVERIFY(QFileInfo(small).size() < QFileInfo(big).size());
    }

    void convertAndOcr()
    {
        QImage text(1400, 300, QImage::Format_ARGB32);
        text.fill(Qt::transparent);
        {
            QPainter p(&text);
            p.fillRect(text.rect(), Qt::white);
            QFont f;
            f.setPixelSize(90);
            p.setFont(f);
            p.setPen(Qt::black);
            p.drawText(text.rect(), Qt::AlignCenter, QStringLiteral("HELLO NOTCH"));
        }
        const QString png = m_tmp.filePath(QStringLiteral("text.png"));
        QVERIFY(text.save(png));

        const QString webp = m_tmp.filePath(QStringLiteral("small.webp"));
        QCOMPARE(DocTools::convertImage(png, QStringLiteral("webp"), 700, webp), QString());
        QImageReader back(webp);
        QCOMPARE(back.format(), QByteArray("webp"));
        QCOMPARE(back.size().width(), 700);
        const QString jpg = m_tmp.filePath(QStringLiteral("same.jpg"));
        QCOMPARE(DocTools::convertImage(png, QStringLiteral("jpg"), 5000, jpg), QString()); // never upscales
        QCOMPARE(QImageReader(jpg).size().width(), 1400);
        const QString smallerPng = m_tmp.filePath(QStringLiteral("smaller.png")); // resizing must not bloat a PNG
        QCOMPARE(DocTools::convertImage(png, QStringLiteral("png"), 700, smallerPng), QString());
        QVERIFY2(QFileInfo(smallerPng).size() <= QFileInfo(png).size(),
                 qPrintable(QStringLiteral("%1 > %2").arg(QFileInfo(smallerPng).size()).arg(QFileInfo(png).size())));

        if (QStandardPaths::findExecutable(QStringLiteral("tesseract")).isEmpty())
            QSKIP("tesseract not installed");
        QString ocr;
        QCOMPARE(DocTools::recognizeText(png, false, &ocr), QString());
        QVERIFY2(ocr.contains(QStringLiteral("HELLO")), qPrintable(ocr));
    }

    void typeGroups()
    {
        QCOMPARE(FileService::typeGroup(QStringLiteral("application/pdf")), QStringLiteral("pdf"));
        QCOMPARE(FileService::typeGroup(QStringLiteral("image/webp")), QStringLiteral("image"));
        QCOMPARE(FileService::typeGroup(QStringLiteral("video/mp4")), QStringLiteral("video"));
        QCOMPARE(FileService::typeGroup(QStringLiteral("audio/mpeg")), QStringLiteral("audio"));
        QCOMPARE(FileService::typeGroup(QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document")),
                 QStringLiteral("document"));
        QCOMPARE(FileService::typeGroup(QStringLiteral("application/vnd.oasis.opendocument.spreadsheet")), QStringLiteral("document"));
        QCOMPARE(FileService::typeGroup(QStringLiteral("application/zip")), QStringLiteral("archive"));
        QCOMPARE(FileService::typeGroup(QStringLiteral("application/x-compressed-tar")), QStringLiteral("archive"));
        QCOMPARE(FileService::typeGroup(QStringLiteral("text/x-c++src")), QStringLiteral("text")); // code inherits text/plain
        QCOMPARE(FileService::typeGroup(QStringLiteral("application/json")), QStringLiteral("text"));
        QCOMPARE(FileService::typeGroup(QStringLiteral("application/x-desktop")), QString()); // link shortcuts: no rule
        QCOMPARE(FileService::typeGroup(QStringLiteral("application/octet-stream")), QString());
    }

    // ---- Share via QR ----
    QList<ShareCrypto::SourceFile> sampleFiles(QList<QByteArray> *contents)
    {
        const QList<QPair<QString, int>> spec{{QStringLiteral("empty.txt"), 0},
                                              {QStringLiteral("Ünï report.pdf"), 5},
                                              {QStringLiteral("exact.bin"), int(ShareCrypto::ChunkSize)},
                                              {QStringLiteral("big file.bin"), int(ShareCrypto::ChunkSize) * 2 + 1234}};
        QList<ShareCrypto::SourceFile> files;
        int salt = 1;
        for (const auto &s : spec) {
            const QByteArray data = pattern(s.second, salt++);
            contents->append(data);
            files.append({writeSample(QStringLiteral("src-") + QString::number(salt), data), s.first,
                          QStringLiteral("application/octet-stream")});
        }
        return files;
    }
    void shareCryptoRoundTripsSeveralFiles()
    {
        QList<QByteArray> contents;
        const auto files = sampleFiles(&contents);
        const QString enc = m_tmp.filePath(QStringLiteral("enc.bin")), outDir = m_tmp.filePath(QStringLiteral("dec"));
        const QByteArray key = ShareCrypto::newKey();
        QCOMPARE(key.size(), ShareCrypto::KeySize);
        QCOMPARE(ShareCrypto::encryptFiles(files, enc, key), QString());
        QStringList names;
        QCOMPARE(ShareCrypto::decryptFiles(enc, outDir, key, &names), QString());
        QCOMPARE(names.size(), files.size());
        for (int i = 0; i < files.size(); ++i) {
            QCOMPARE(names[i], files[i].name);
            QFile f(QDir(outDir).filePath(names[i]));
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), contents[i]);
        }
        // The server must not see names or content.
        QFile e(enc);
        QVERIFY(e.open(QIODevice::ReadOnly));
        const QByteArray raw = e.readAll();
        QVERIFY(!raw.contains("report") && !raw.contains("big file"));
        QVERIFY(!raw.contains(contents[1]));
    }
    void shareCryptoSingleEmptyFile()
    {
        const QString enc = m_tmp.filePath(QStringLiteral("enc.bin")), outDir = m_tmp.filePath(QStringLiteral("dec"));
        const QByteArray key = ShareCrypto::newKey();
        QCOMPARE(ShareCrypto::encryptFiles({{writeSample(QStringLiteral("z"), {}), QStringLiteral("z.txt"), {}}}, enc, key), QString());
        QStringList names;
        QCOMPARE(ShareCrypto::decryptFiles(enc, outDir, key, &names), QString());
        QCOMPARE(names, QStringList{QStringLiteral("z.txt")});
        QCOMPARE(QFileInfo(QDir(outDir).filePath(QStringLiteral("z.txt"))).size(), qint64(0));
        QVERIFY(!ShareCrypto::encryptFiles({}, enc, key).isEmpty());
    }
    void shareCryptoRejectsTampering()
    {
        QList<QByteArray> contents;
        const auto files = sampleFiles(&contents);
        const QString enc = m_tmp.filePath(QStringLiteral("enc.bin")), outDir = m_tmp.filePath(QStringLiteral("dec"));
        const QByteArray key = ShareCrypto::newKey();
        QCOMPARE(ShareCrypto::encryptFiles(files, enc, key), QString());
        QFile e(enc);
        QVERIFY(e.open(QIODevice::ReadOnly));
        const QByteArray good = e.readAll();
        e.close();
        auto tryDecrypt = [&](const QByteArray &bytes, const QByteArray &k) {
            QFile w(enc);
            if (!w.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return QStringLiteral("test setup failed");
            w.write(bytes);
            w.close();
            return ShareCrypto::decryptFiles(enc, outDir, k);
        };
        QCOMPARE(tryDecrypt(good, key), QString());
        QVERIFY(!tryDecrypt(good, ShareCrypto::newKey()).isEmpty()); // wrong key
        QByteArray flipped = good;
        flipped[flipped.size() / 2] = char(flipped[flipped.size() / 2] ^ 1);
        QVERIFY(!tryDecrypt(flipped, key).isEmpty());                // bit flip
        QVERIFY(!tryDecrypt(good.left(good.size() - 100), key).isEmpty()); // cut off
        // Last record removed whole (4-byte length + (2*chunk+1234 mod chunk = 1234) bytes + 16-byte tag): the
        // remaining records authenticate alone, but the new last one was not sealed as "final".
        QVERIFY(!tryDecrypt(good.left(good.size() - (4 + 1234 + 16)), key).isEmpty());
        QVERIFY(!tryDecrypt(good + good.right(100), key).isEmpty()); // trailing junk
        QVERIFY(!tryDecrypt(QByteArray("NSH1garbage"), key).isEmpty());
    }
    void shareCryptoMatchesReferenceDecryptor()
    {
        QProcess probe;
        probe.start(QStringLiteral("python3"), {QStringLiteral("-c"), QStringLiteral("import cryptography")});
        if (!probe.waitForFinished(10000) || probe.exitCode() != 0)
            QSKIP("python3 with the cryptography package is not available");
        QList<QByteArray> contents;
        const auto files = sampleFiles(&contents);
        const QString enc = m_tmp.filePath(QStringLiteral("enc.bin")), outDir = m_tmp.filePath(QStringLiteral("ref"));
        const QByteArray key = ShareCrypto::newKey();
        QCOMPARE(ShareCrypto::encryptFiles(files, enc, key), QString());
        QProcess py;
        py.start(QStringLiteral("python3"), {QStringLiteral(NOTCH_SOURCE_DIR "/server/tests/decrypt_share.py"), enc,
                                            ShareCrypto::keyToFragment(key), outDir});
        QVERIFY(py.waitForFinished(30000));
        QCOMPARE(QString::fromUtf8(py.readAllStandardError()), QString());
        QStringList printed = QString::fromUtf8(py.readAllStandardOutput()).split(u'\n', Qt::SkipEmptyParts);
        QCOMPARE(printed.size(), files.size());
        for (int i = 0; i < files.size(); ++i) {
            QCOMPARE(printed[i], files[i].name);
            QFile f(QDir(outDir).filePath(files[i].name));
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), contents[i]);
        }
    }
    void shareCryptoPasswordWrapsTheKey()
    {
        const QByteArray key = ShareCrypto::newKey();
        const QString fragment = ShareCrypto::wrapKey(key, QStringLiteral("correct horse"));
        QVERIFY(!fragment.isEmpty());
        QVERIFY(fragment != ShareCrypto::keyToFragment(key));
        QCOMPARE(QByteArray::fromBase64(fragment.toLatin1(), QByteArray::Base64UrlEncoding).size(), 76); // not 32: the page tells them apart
        QCOMPARE(ShareCrypto::unwrapKey(fragment, QStringLiteral("correct horse")), key);
        QVERIFY(ShareCrypto::unwrapKey(fragment, QStringLiteral("wrong horse")).isNull());
        QVERIFY(ShareCrypto::unwrapKey(ShareCrypto::keyToFragment(key), QStringLiteral("x")).isNull()); // a plain key is not wrapped
        QVERIFY(ShareCrypto::wrapKey(key, QStringLiteral("correct horse")) != fragment); // fresh salt every time
        // An independent implementation (and a non-ASCII password) agrees
        QProcess probe;
        probe.start(QStringLiteral("python3"), {QStringLiteral("-c"), QStringLiteral("import cryptography")});
        if (!probe.waitForFinished(10000) || probe.exitCode() != 0)
            return;
        QList<QByteArray> contents;
        const auto files = sampleFiles(&contents);
        const QString enc = m_tmp.filePath(QStringLiteral("enc.bin")), outDir = m_tmp.filePath(QStringLiteral("refpw"));
        const QString password = QStringLiteral("pässwörd 密码");
        const QString wrapped = ShareCrypto::wrapKey(key, password);
        QCOMPARE(ShareCrypto::encryptFiles(files, enc, key), QString());
        QProcess py;
        py.start(QStringLiteral("python3"), {QStringLiteral(NOTCH_SOURCE_DIR "/server/tests/decrypt_share.py"), enc, wrapped, outDir, password});
        QVERIFY(py.waitForFinished(60000));
        QCOMPARE(QString::fromUtf8(py.readAllStandardError()), QString());
        QFile f(QDir(outDir).filePath(files.last().name));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), contents.last());
    }
    void nearbyCryptoAgreesAndBindsTheTranscript()
    {
        const auto s = NearbyCrypto::generate(), r = NearbyCrypto::generate();
        QCOMPARE(s.pub.size(), 32);
        QVERIFY(s.pub != r.pub);
        const QByteArray zs = NearbyCrypto::agree(s.priv, r.pub), zr = NearbyCrypto::agree(r.priv, s.pub);
        QVERIFY(!zs.isNull());
        QCOMPARE(zs, zr);
        const QByteArray files = QByteArrayLiteral("[{\"name\":\"a.txt\",\"size\":1}]");
        const QByteArray t = NearbyCrypto::transcript(s.pub, r.pub, files);
        const QString code = NearbyCrypto::code(zs, t);
        QCOMPARE(code.size(), 4);
        QCOMPARE(NearbyCrypto::code(zr, t), code);
        QCOMPARE(NearbyCrypto::sessionKey(zs, t), NearbyCrypto::sessionKey(zr, t));
        QCOMPARE(NearbyCrypto::sessionKey(zs, t).size(), 32);
        QVERIFY(NearbyCrypto::sessionKey(zs, t) != NearbyCrypto::acceptProof(zs, t));
        QVERIFY(NearbyCrypto::transcript(s.pub, r.pub, files + " ") != t); // another file list, another transcript
        const auto other = NearbyCrypto::generate();
        QVERIFY(NearbyCrypto::agree(other.priv, s.pub) != zs);
        QCOMPARE(NearbyCrypto::commitment(s.pub).size(), 32);
        QVERIFY(NearbyCrypto::commitment(s.pub) != NearbyCrypto::commitment(r.pub));
        QVERIFY(NearbyCrypto::agree(s.priv, QByteArray(32, 0)).isNull()); // a low-order key gives no secret
        QVERIFY(NearbyCrypto::agree(s.priv, QByteArray(5, 1)).isNull());
        QSet<QString> seen;
        for (int i = 0; i < 20; ++i) {
            const auto a = NearbyCrypto::generate(), b = NearbyCrypto::generate();
            seen << NearbyCrypto::code(NearbyCrypto::agree(a.priv, b.pub), NearbyCrypto::transcript(a.pub, b.pub, files));
        }
        QVERIFY(seen.size() > 10);
    }
    void nearbyCryptoMatchesPython()
    {
        QProcess probe;
        probe.start(QStringLiteral("python3"), {QStringLiteral("-c"), QStringLiteral("import cryptography")});
        if (!probe.waitForFinished(10000) || probe.exitCode() != 0)
            QSKIP("python3 with the cryptography package is not available");
        const auto a = NearbyCrypto::generate(), b = NearbyCrypto::generate();
        QProcess py;
        py.start(QStringLiteral("python3"), {QStringLiteral("-c"), QStringLiteral(
            "import sys, binascii\n"
            "from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey\n"
            "priv = X25519PrivateKey.from_private_bytes(binascii.unhexlify(sys.argv[1]))\n"
            "print(binascii.hexlify(priv.exchange(X25519PublicKey.from_public_bytes(binascii.unhexlify(sys.argv[2])))).decode())\n"),
            QString::fromLatin1(a.priv.toHex()), QString::fromLatin1(b.pub.toHex())});
        QVERIFY(py.waitForFinished(30000));
        QCOMPARE(QString::fromUtf8(py.readAllStandardOutput()).trimmed(), QString::fromLatin1(NearbyCrypto::agree(a.priv, b.pub).toHex()));
    }
    void nearbyFindsTheReceiverAndSendsAfterAccept()
    {
        NearbyPair p = makeNearbyPair(m_tmp.path());
        QVERIFY(p.receiver->receiving());
        QCOMPARE(p.device.name, QStringLiteral("Alice's PC"));
        QCOMPARE(p.device.id, QStringLiteral("recv-id"));
        QVERIFY(p.device.port > 0);

        QMap<QString, QByteArray> expected, got;
        const auto files = makeNearbyFiles(m_tmp.path(), &expected);
        QString fromName;
        connect(p.receiver.get(), &NearbyService::received, this, [&](const QString &folder, const QStringList &names, const QString &from) {
            fromName = from;
            for (const QString &n : names) { // the folder goes away right after this slot: read now
                QFile f(QDir(folder).filePath(n));
                if (f.open(QIODevice::ReadOnly))
                    got.insert(n, f.readAll());
            }
        });
        QSignalSpy incoming(p.receiver.get(), &NearbyService::incoming);
        const QString out = p.sender->send(p.device, files);
        QVERIFY2(incoming.wait(10000), qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(p.sender->transfer(out)).toJson(QJsonDocument::Compact))));
        const QString in = incoming.first().at(0).toString(), code = incoming.first().at(4).toString();
        QCOMPARE(incoming.first().at(1).toString(), QStringLiteral("Bob's laptop"));
        QCOMPARE(incoming.first().at(2).toList().size(), 3);
        QCOMPARE(code.size(), 4);
        QTRY_COMPARE(p.sender->transfer(out).value(QStringLiteral("state")).toString(), QStringLiteral("waiting"));
        QCOMPARE(p.sender->transfer(out).value(QStringLiteral("code")).toString(), code); // the same number on both screens
        QCOMPARE(p.receiver->transfer(in).value(QStringLiteral("code")).toString(), code);

        p.receiver->accept(in);
        QTRY_COMPARE_WITH_TIMEOUT(p.sender->transfer(out).value(QStringLiteral("state")).toString(), QStringLiteral("done"), 20000);
        QTRY_COMPARE(p.receiver->transfer(in).value(QStringLiteral("state")).toString(), QStringLiteral("done"));
        QCOMPARE(fromName, QStringLiteral("Bob's laptop"));
        QCOMPARE(got, expected);
        QVERIFY(!QDir(m_tmp.filePath(QStringLiteral("nb-recv/nearby/") + in)).exists()); // nothing left behind
    }
    void nearbyDeclineEndsItAndRepeatedRefusalsAreIgnored()
    {
        NearbyPair p = makeNearbyPair(m_tmp.path());
        QMap<QString, QByteArray> expected;
        const auto files = makeNearbyFiles(m_tmp.path(), &expected);
        QSignalSpy incoming(p.receiver.get(), &NearbyService::incoming), received(p.receiver.get(), &NearbyService::received);
        for (int i = 0; i < 3; ++i) {
            incoming.clear();
            const QString out = p.sender->send(p.device, files);
            QVERIFY(incoming.wait(10000));
            p.receiver->decline(incoming.first().at(0).toString());
            QTRY_COMPARE(p.sender->transfer(out).value(QStringLiteral("state")).toString(), QStringLiteral("declined"));
        }
        QCOMPARE(received.count(), 0);
        // Three refusals from one address: its next request is dropped, and it no longer sees the receiver.
        incoming.clear();
        const QString out = p.sender->send(p.device, files);
        QTRY_COMPARE(p.sender->transfer(out).value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
        QCOMPARE(incoming.count(), 0);
        QSignalSpy found(p.sender.get(), &NearbyService::deviceFound);
        p.sender->probe({QHostAddress::LocalHost}, p.receiver->discoveryPort());
        QTest::qWait(600);
        QCOMPARE(found.count(), 0);
    }
    // A hand-made sender: it can follow the protocol, or break it at one chosen point.
    void nearbyRejectsBrokenHandshakesAndDamagedData()
    {
        NearbyPair p = makeNearbyPair(m_tmp.path());
        auto connectRaw = [&](QTcpSocket &s) {
            s.connectToHost(QHostAddress::LocalHost, p.device.port);
            QVERIFY(s.waitForConnected(5000));
        };
        const QByteArray filesJson = QByteArrayLiteral("[{\"name\":\"a.txt\",\"size\":100,\"type\":\"text/plain\"}]");
        QSignalSpy incoming(p.receiver.get(), &NearbyService::incoming), received(p.receiver.get(), &NearbyService::received);

        // 1. Reveals another key than the one it committed to: refused before anyone is asked.
        {
            QTcpSocket s;
            connectRaw(s);
            const auto k = NearbyCrypto::generate(), other = NearbyCrypto::generate();
            s.write(NearbyService::frame({{"t", "hello"}, {"v", 1}, {"id", "x"}, {"name", "Mallory"}, {"filesJson", QString::fromUtf8(filesJson)},
                                          {"commit", QString::fromLatin1(NearbyCrypto::commitment(k.pub).toBase64())}}));
            QTRY_VERIFY(s.bytesAvailable() >= 4);
            QCOMPARE(takeFrame(s).value(QStringLiteral("t")).toString(), QStringLiteral("key"));
            s.write(NearbyService::frame({{"t", "reveal"}, {"pub", QString::fromLatin1(other.pub.toBase64())}}));
            QTest::qWait(400);
            QCOMPARE(incoming.count(), 0);
        }
        // 2. A proper handshake, accepted, but what follows is not the agreed encrypted stream: nothing arrives.
        {
            QTcpSocket s;
            connectRaw(s);
            const auto k = NearbyCrypto::generate();
            s.write(NearbyService::frame({{"t", "hello"}, {"v", 1}, {"id", "x"}, {"name", "Mallory"}, {"filesJson", QString::fromUtf8(filesJson)},
                                          {"commit", QString::fromLatin1(NearbyCrypto::commitment(k.pub).toBase64())}}));
            QTRY_VERIFY(s.bytesAvailable() >= 4);
            const QByteArray rpub = QByteArray::fromBase64(takeFrame(s).value(QStringLiteral("pub")).toString().toLatin1());
            s.write(NearbyService::frame({{"t", "reveal"}, {"pub", QString::fromLatin1(k.pub.toBase64())}}));
            QVERIFY(incoming.wait(5000));
            const QByteArray z = NearbyCrypto::agree(k.priv, rpub), tr = NearbyCrypto::transcript(k.pub, rpub, filesJson);
            QCOMPARE(incoming.first().at(4).toString(), NearbyCrypto::code(z, tr)); // the sender can compute the same code
            const QString id = incoming.first().at(0).toString();
            p.receiver->accept(id);
            QTRY_VERIFY(s.bytesAvailable() >= 4);
            QCOMPARE(takeFrame(s).value(QStringLiteral("t")).toString(), QStringLiteral("accept"));
            s.write(NearbyService::frame({{"t", "data"}, {"size", 600}}));
            s.write(pattern(600, 5)); // not an NSH1 stream
            QTRY_COMPARE(p.receiver->transfer(id).value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
            QCOMPARE(received.count(), 0);
        }
    }
    void nearbyAnswersOneRequestAtATime()
    {
        NearbyPair p = makeNearbyPair(m_tmp.path());
        QMap<QString, QByteArray> expected;
        const auto files = makeNearbyFiles(m_tmp.path(), &expected);
        QSignalSpy incoming(p.receiver.get(), &NearbyService::incoming);
        const QString first = p.sender->send(p.device, files);
        QVERIFY(incoming.wait(10000));
        const QString second = p.sender->send(p.device, files); // told "busy" while the first waits for an answer
        QTRY_COMPARE(p.sender->transfer(second).value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
        QCOMPARE(incoming.count(), 1);
        p.receiver->decline(incoming.first().at(0).toString());
        QTRY_COMPARE(p.sender->transfer(first).value(QStringLiteral("state")).toString(), QStringLiteral("declined"));
    }
    void nearbyStopsBeingVisible()
    {
        NearbyService r(m_tmp.filePath(QStringLiteral("nb-solo")));
        QVERIFY(r.startReceiving(60, 0).isEmpty());
        const quint16 port = r.discoveryPort();
        r.stopReceiving();
        QVERIFY(!r.receiving());
        NearbyService s(m_tmp.filePath(QStringLiteral("nb-solo2")));
        QSignalSpy found(&s, &NearbyService::deviceFound);
        s.probe({QHostAddress::LocalHost}, port);
        QTest::qWait(500);
        QCOMPARE(found.count(), 0); // nothing answers once receiving is off
    }
    void zipWriterMakesAValidArchive()
    {
        const QList<QPair<QString, QByteArray>> spec{{QStringLiteral("plain.txt"), pattern(1000, 1)},
                                                     {QStringLiteral("empty.txt"), {}},
                                                     {QStringLiteral("Ünï sub/rapat 密码.pdf"), pattern(5, 2)},
                                                     {QStringLiteral("big.bin"), pattern(3 * 1024 * 1024 + 17, 3)}};
        QList<ZipWriter::Entry> entries;
        QStringList expected;
        int i = 0;
        for (const auto &s : spec) {
            entries.append({writeSample(QStringLiteral("z%1").arg(i++), s.second), s.first});
            expected << s.first + u':' + QString::number(s.second.size()) + u':'
                            + QString::fromLatin1(QCryptographicHash::hash(s.second, QCryptographicHash::Sha256).toHex());
        }
        const QString zip = m_tmp.filePath(QStringLiteral("out.zip"));
        QCOMPARE(ZipWriter::write(zip, entries), QString());
        QVERIFY(!ZipWriter::write(zip, {{m_tmp.filePath(QStringLiteral("missing")), QStringLiteral("m")}}).isEmpty());
        QVERIFY(!ZipWriter::write(zip, {}).isEmpty());
        QCOMPARE(ZipWriter::write(zip, entries), QString()); // a failed attempt must not break the next
        QProcess py;
        py.start(QStringLiteral("python3"), {QStringLiteral("-c"), QStringLiteral(
            "import zipfile, hashlib, sys\n"
            "z = zipfile.ZipFile(sys.argv[1])\n"
            "assert z.testzip() is None\n"
            "for i in z.infolist():\n"
            "    assert i.flag_bits & 0x800\n"
            "    d = z.read(i)\n"
            "    print('%s:%d:%s' % (i.filename, len(d), hashlib.sha256(d).hexdigest()))\n"), zip});
        if (!py.waitForFinished(30000))
            QSKIP("python3 is not available");
        QCOMPARE(QString::fromUtf8(py.readAllStandardError()), QString());
        QCOMPARE(QString::fromUtf8(py.readAllStandardOutput()).remove(u'\r').split(u'\n', Qt::SkipEmptyParts), expected); // CRLF on Windows
    }
    void inviteLinksAreParsedStrictly()
    {
        const QString token = QStringLiteral("abcdEFGH0123456789_-xyz");
        auto ok = Invite::parse(QStringLiteral("  https://share.example.com/join#") + token + QStringLiteral("\n"));
        QCOMPARE(ok.error, QString());
        QCOMPARE(ok.server, QUrl(QStringLiteral("https://share.example.com")));
        QCOMPARE(ok.token, token);
        QCOMPARE(Invite::parse(QStringLiteral("https://team.example.com:8443/notch/join#") + token).server,
                 QUrl(QStringLiteral("https://team.example.com:8443/notch"))); // behind a path
        QCOMPARE(Invite::parse(QStringLiteral("http://127.0.0.1:18090/join#") + token).error, QString()); // local testing
        QVERIFY(!Invite::parse(QStringLiteral("http://share.example.com/join#") + token).error.isEmpty()); // key in the clear
        QVERIFY(!Invite::parse(QStringLiteral("https://share.example.com/join")).error.isEmpty());          // no code
        QVERIFY(!Invite::parse(QStringLiteral("https://share.example.com/join#short")).error.isEmpty());
        QVERIFY(!Invite::parse(QStringLiteral("https://share.example.com/s/abc#") + token).error.isEmpty()); // a share link, not an invite
        QVERIFY(!Invite::parse(QStringLiteral("just some text")).error.isEmpty());
        QCOMPARE(Invite::normalizeServer(QStringLiteral(" https://share.example.com/ ")), QUrl(QStringLiteral("https://share.example.com")));
        QCOMPARE(Invite::normalizeServer(QStringLiteral("share.example.com")), QUrl(QStringLiteral("https://share.example.com"))); // a bare name means https
        QVERIFY(!Invite::normalizeServer(QStringLiteral("http://share.example.com")).isValid());                              // never plain http
        QVERIFY(!Invite::normalizeServer(QString()).isValid());
    }
    void qrImageIsRenderedAndScannableSized()
    {
        const QImage qr = renderQr(QStringLiteral("https://share.example.com/s/abcdefghijklmnop#key"), 240);
        QVERIFY(!qr.isNull());
        QCOMPARE(qr.width(), qr.height());
        QVERIFY(qr.width() > 100 && qr.width() <= 240);
        QCOMPARE(qr.pixelColor(0, 0), QColor(Qt::white)); // quiet zone
    }

    // ---- Local share server ----
    QString writeSample(const QString &name, const QByteArray &data)
    {
        const QString path = m_tmp.filePath(name);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(data);
        return path;
    }
    void shareServerServesPageAndFileThenStops()
    {
        QByteArray data;
        for (int i = 0; i < 700 * 1024; ++i)
            data.append(char(i * 7 + i / 13)); // several pump rounds
        ShareServer server;
        QSignalSpy done(&server, &ShareServer::finished);
        QCOMPARE(server.start(QHostAddress::LocalHost, {{writeSample(QStringLiteral("s.bin"), data), QStringLiteral("Résumé <x>.bin"),
                                                         QStringLiteral("application/octet-stream"), 0}}, 60), QString());
        QVERIFY(server.running());
        const QUrl base = server.url();
        QVERIFY(base.toString().startsWith(QStringLiteral("http://127.0.0.1:")));

        const Http page = httpGet(base);
        QCOMPARE(page.status, 200);
        QVERIFY(page.body.contains("Résumé"));
        QVERIFY(page.body.contains("\\u003cx>")); // the name cannot close the script element
        QVERIFY(!page.body.contains("<x>"));
        QVERIFY(page.body.contains("\"mode\":\"local\""));
        QCOMPARE(httpGet(QUrl(base.toString() + QStringLiteral("nope"))).status, 404);
        QCOMPARE(httpGet(QUrl(base.toString().replace(base.path(), QStringLiteral("/wrongtoken/")))).status, 404);
        QCOMPARE(done.count(), 0);

        const Http part = httpGet(QUrl(base.toString() + QStringLiteral("f/0")), "bytes=100-199");
        QCOMPARE(part.status, 206);
        QCOMPARE(part.body, data.mid(100, 100));
        QCOMPARE(part.contentRange, QByteArray("bytes 100-199/") + QByteArray::number(data.size()));
        QCOMPARE(done.count(), 0); // a partial range is not the download

        const Http whole = httpGet(QUrl(base.toString() + QStringLiteral("f/0")));
        QCOMPARE(whole.status, 200);
        QCOMPARE(whole.body, data);
        QVERIFY(whole.disposition.contains("attachment"));
        QVERIFY(done.count() == 1 || done.wait(5000));
        QCOMPARE(done.first().first().toString(), QStringLiteral("downloaded"));
        QVERIFY(!server.running());
        QCOMPARE(httpGet(QUrl(base.toString() + QStringLiteral("f/0"))).status, 0); // closed
    }
    void shareServerServesSeveralFilesAndStaysUp()
    {
        const QByteArray a = pattern(1000, 1), b = pattern(300000, 2);
        ShareServer server;
        QSignalSpy done(&server, &ShareServer::finished), downloaded(&server, &ShareServer::fileDownloaded);
        QCOMPARE(server.start(QHostAddress::LocalHost,
                              {{writeSample(QStringLiteral("a.png"), a), QStringLiteral("a.png"), QStringLiteral("image/png"), 0},
                               {writeSample(QStringLiteral("b.txt"), b), QStringLiteral("b.txt"), QStringLiteral("text/plain"), 0},
                               {writeSample(QStringLiteral("c.txt"), {}), QStringLiteral("c.txt"), QStringLiteral("text/plain"), 0}},
                              60), QString());
        const QString base = server.url().toString();
        QCOMPARE(httpGet(QUrl(base + QStringLiteral("f/3"))).status, 404);
        QCOMPARE(httpGet(QUrl(base + QStringLiteral("f/x"))).status, 404);

        // A preview of an image is not a download; a non-image has none.
        const Http preview = httpGet(QUrl(base + QStringLiteral("f/0?inline=1")));
        QCOMPARE(preview.status, 200);
        QCOMPARE(preview.body, a);
        QVERIFY(preview.disposition.startsWith("inline"));
        QCOMPARE(httpGet(QUrl(base + QStringLiteral("f/1?inline=1"))).status, 404);
        QCOMPARE(downloaded.count(), 0);

        QCOMPARE(httpGet(QUrl(base + QStringLiteral("f/1"))).body, b);
        QCOMPARE(httpGet(QUrl(base + QStringLiteral("f/1")), "bytes=10-19").body, b.mid(10, 10)); // partial: not counted
        const Http empty = httpGet(QUrl(base + QStringLiteral("f/2")));
        QCOMPARE(empty.status, 200);
        QVERIFY(empty.body.isEmpty());
        QTRY_COMPARE(downloaded.count(), 2); // b.txt and the empty file
        QCOMPARE(downloaded.at(0).first().toInt(), 1);
        QCOMPARE(downloaded.at(1).first().toInt(), 2);
        QVERIFY(server.running()); // several files: stays up
        QCOMPARE(done.count(), 0);
        QCOMPARE(httpGet(QUrl(base + QStringLiteral("f/0"))).body, a);
        QTRY_COMPARE(downloaded.count(), 3);
        server.stop();
        QCOMPARE(done.count(), 1);
    }
    void shareServerGatesWithPinAndLocksOut()
    {
        const QByteArray data = pattern(5000, 3);
        ShareServer server;
        QSignalSpy downloaded(&server, &ShareServer::fileDownloaded);
        QCOMPARE(server.start(QHostAddress::LocalHost,
                              {{writeSample(QStringLiteral("p.txt"), data), QStringLiteral("secret plans.txt"), QStringLiteral("text/plain"), 0},
                               {writeSample(QStringLiteral("q.txt"), data), QStringLiteral("q.txt"), QStringLiteral("text/plain"), 0}},
                              60, QStringLiteral("pin"), QStringLiteral("4821")), QString());
        const QString base = server.url().toString();
        const Http page = httpGet(QUrl(base));
        QCOMPARE(page.status, 200);
        QVERIFY(!page.body.contains("secret plans")); // no file names before the PIN
        QVERIFY(page.body.contains("\"protection\":\"pin\""));
        QCOMPARE(httpGet(QUrl(base + QStringLiteral("list"))).status, 403);
        QCOMPARE(httpGet(QUrl(base + QStringLiteral("f/0"))).status, 403);
        QCOMPARE(httpGet(QUrl(base + QStringLiteral("f/0")), {}, "4822").status, 403);
        QCOMPARE(httpGet(QUrl(base + QStringLiteral("auth")), {}, "4821").status, 200);
        const Http list = httpGet(QUrl(base + QStringLiteral("list")), {}, "4821");
        QCOMPARE(list.status, 200);
        QVERIFY(list.body.contains("secret plans.txt"));
        const Http file = httpGet(QUrl(base + QStringLiteral("f/0")), {}, "4821");
        QCOMPARE(file.status, 200);
        QCOMPARE(file.body, data);
        QTRY_COMPARE(downloaded.count(), 1);
        // The first right answer reset the count; now five wrong ones lock this address out, even for the right PIN.
        for (int i = 0; i < 5; ++i)
            QCOMPARE(httpGet(QUrl(base + QStringLiteral("auth")), {}, "0000").status, 403);
        QCOMPARE(httpGet(QUrl(base + QStringLiteral("auth")), {}, "4821").status, 429);
        QCOMPARE(httpGet(QUrl(base + QStringLiteral("f/1")), {}, "4821").status, 429);
    }
    void shareServerExpiresAndStops()
    {
        ShareServer server;
        QSignalSpy done(&server, &ShareServer::finished);
        const QString file = writeSample(QStringLiteral("e.bin"), QByteArray(100, 'e'));
        QCOMPARE(server.start(QHostAddress::LocalHost, {{file, QStringLiteral("e.bin"), {}, 0}}, 1), QString());
        QVERIFY(done.wait(4000));
        QCOMPARE(done.first().first().toString(), QStringLiteral("expired"));

        QCOMPARE(server.start(QHostAddress::LocalHost, {{file, QStringLiteral("e.bin"), {}, 0}}, 60), QString());
        server.stop();
        QCOMPARE(done.count(), 2);
        QCOMPARE(done.last().first().toString(), QStringLiteral("stopped"));
        QVERIFY(!server.start(QHostAddress::LocalHost, {{m_tmp.filePath(QStringLiteral("missing")), QStringLiteral("m"), {}, 0}}, 60).isEmpty());
        QVERIFY(!server.start(QHostAddress::LocalHost, {}, 60).isEmpty());
    }

    void vaultLifecycle()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("gocryptfs")).isEmpty())
            QSKIP("gocryptfs not installed");
        const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (runtime.isEmpty())
            QSKIP("no XDG_RUNTIME_DIR");
        Vault vault(m_tmp.filePath(QStringLiteral("cipher")),
                    runtime + QStringLiteral("/notch-test-vault-") + QString::number(QCoreApplication::applicationPid()));
        QVERIFY(!vault.exists());
        QVERIFY(!vault.create(QStringLiteral("short")).isEmpty()); // too short
        QCOMPARE(vault.create(QStringLiteral("correct horse battery")), QString());
        QVERIFY(vault.exists());
        QVERIFY(!vault.isUnlocked());

        QCOMPARE(vault.unlock(QStringLiteral("wrong password!")), QStringLiteral("wrong password"));
        QVERIFY(!vault.isUnlocked());
        QCOMPARE(vault.unlock(QStringLiteral("correct horse battery")), QString());
        QVERIFY(vault.isUnlocked());

        QFile secret(vault.mountDir() + QStringLiteral("/ktp.txt"));
        QVERIFY(secret.open(QIODevice::WriteOnly));
        secret.write("NIK 3201234567890001");
        secret.close();
        // Nothing readable on disk: neither the content nor the file name.
        QDirIterator it(vault.cipherDir(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile f(it.next());
            QVERIFY(f.open(QIODevice::ReadOnly));
            QVERIFY(!f.readAll().contains("NIK 3201234567890001"));
            QVERIFY(!f.fileName().contains(QStringLiteral("ktp")));
        }

        QCOMPARE(vault.lock(), QString());
        QVERIFY(!vault.isUnlocked());
        QVERIFY(!QFile::exists(vault.mountDir() + QStringLiteral("/ktp.txt")));
        QCOMPARE(vault.unlock(QStringLiteral("correct horse battery")), QString()); // still there after relock
        QVERIFY(QFile::exists(vault.mountDir() + QStringLiteral("/ktp.txt")));
        QCOMPARE(vault.lock(), QString());
        QDir().rmdir(vault.mountDir());
    }

    // The engine Windows uses, run here too.
    void vaultFilesEngine()
    {
        const QString cipher = m_tmp.filePath(QStringLiteral("files-cipher"));
        const QString mount = m_tmp.filePath(QStringLiteral("files-plain"));
        auto write = [](const QString &path, const QByteArray &data) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        auto read = [](const QString &path) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        {
            Vault vault(cipher, mount, Vault::Files);
            QVERIFY(!vault.exists());
            QVERIFY(!vault.create(QStringLiteral("short")).isEmpty());
            QCOMPARE(vault.create(QStringLiteral("correct horse battery")), QString());
            QVERIFY(vault.exists());
            QVERIFY(!vault.isUnlocked());
            QCOMPARE(vault.unlock(QStringLiteral("wrong password!")), QStringLiteral("wrong password"));
            QVERIFY(!vault.isUnlocked());
            QCOMPARE(vault.unlock(QStringLiteral("correct horse battery")), QString());
            QVERIFY(vault.isUnlocked());

            write(mount + QStringLiteral("/ktp.txt"), "NIK 3201234567890001");
            write(mount + QStringLiteral("/gone.txt"), "temporary");
            write(mount + QStringLiteral("/empty"), "");
            QCOMPARE(vault.lock(), QString());
            QVERIFY(!vault.isUnlocked());
            QVERIFY(!QFileInfo::exists(mount));
            // Nothing readable on disk: neither the content nor the file name.
            QDirIterator it(cipher, QDir::Files);
            int count = 0;
            while (it.hasNext()) {
                const QString path = it.next();
                ++count;
                QVERIFY(!read(path).contains("NIK 3201234567890001"));
                QVERIFY(!path.contains(QStringLiteral("ktp")));
            }
            QCOMPARE(count, 4); // three files and the config

            QCOMPARE(vault.unlock(QStringLiteral("correct horse battery")), QString());
            QCOMPARE(read(mount + QStringLiteral("/ktp.txt")), QByteArray("NIK 3201234567890001"));
            QVERIFY(QFileInfo::exists(mount + QStringLiteral("/empty")));
            QVERIFY(QFile::remove(mount + QStringLiteral("/gone.txt")));
            write(mount + QStringLiteral("/ktp.txt"), "NIK 3201234567890002");
            QCOMPARE(vault.lock(), QString());
        }
        {
            // A session that ends without locking (a crash) leaves the plain files: the next unlock keeps them.
            Vault vault(cipher, mount, Vault::Files);
            QCOMPARE(vault.unlock(QStringLiteral("correct horse battery")), QString());
            QVERIFY(!QFileInfo::exists(mount + QStringLiteral("/gone.txt")));
            QCOMPARE(read(mount + QStringLiteral("/ktp.txt")), QByteArray("NIK 3201234567890002"));
            write(mount + QStringLiteral("/new.txt"), "added before the crash");
        }
        {
            Vault vault(cipher, mount, Vault::Files);
            QVERIFY(!vault.isUnlocked());
            QCOMPARE(vault.lock(), QString()); // nothing to do without the key
            QVERIFY(QFileInfo::exists(mount + QStringLiteral("/new.txt")));
            QCOMPARE(vault.unlock(QStringLiteral("correct horse battery")), QString());
            QCOMPARE(read(mount + QStringLiteral("/new.txt")), QByteArray("added before the crash"));
            QCOMPARE(vault.lock(), QString());
            QVERIFY(!QFileInfo::exists(mount));
            QCOMPARE(vault.unlock(QStringLiteral("correct horse battery")), QString());
            QCOMPARE(read(mount + QStringLiteral("/new.txt")), QByteArray("added before the crash"));
            QCOMPARE(QDir(mount).entryList(QDir::Files).size(), 3);
            QCOMPARE(vault.lock(), QString());
        }
    }

    void vaultItemsNeverExpire()
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const FileItem secret = addItem(writeSource(QStringLiteral("ktp.jpg"), "k"), now.addDays(-60));
        const FileItem plain = addItem(writeSource(QStringLiteral("plain.txt"), "p"), now.addDays(-60));
        QVERIFY(m_repo->setVault(secret.id, true));
        QVERIFY(m_repo->setRemoved(secret.id, now.addDays(-5))); // even "removed" long ago
        QFile::remove(path(secret));                              // and not visible in filesDir (vault closed)

        CleanupService::recover(*m_repo, m_files);
        QCOMPARE(CleanupService::purge(*m_repo, m_files, now, DefaultRetentionDays), QStringList{plain.id});
        const QList<FileItem> left = m_repo->withStatus(Status::Available);
        QCOMPARE(left.size(), 1); // not flagged as error, not expired, not emptied
        QVERIFY(left.first().vault);
    }

    void removeManagedGuards()
    {
        QVERIFY(!FileService::removeManaged(m_files, QStringLiteral("../escape")));
        QVERIFY(!FileService::removeManaged(m_files, QStringLiteral("not-a-uuid/x")));
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(!FileService::removeManaged(m_files, id + QStringLiteral("/..")));
        QVERIFY(!FileService::removeManaged(m_files, id + QStringLiteral("/a/b")));

#ifndef Q_OS_WIN // QFile::link makes a .lnk shortcut there, not a symbolic link: nothing to guard against
        // A uuid dir that is a symlink to outside: only the link goes, the target survives.
        const QString outside = writeSource(QStringLiteral("outside/keep.txt"), "keep");
        QVERIFY(QFile::link(QFileInfo(outside).absolutePath(), m_files + u'/' + id));
        QVERIFY(FileService::removeManaged(m_files, id + QStringLiteral("/keep.txt")));
        QVERIFY(QFile::exists(outside));
        QVERIFY(!QFileInfo(m_files + u'/' + id).isSymLink());

        // A stored file that is a symlink: the link is removed, never its target.
        const QString id2 = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(m_files + u'/' + id2);
        QVERIFY(QFile::link(outside, m_files + u'/' + id2 + QStringLiteral("/keep.txt")));
        QVERIFY(FileService::removeManaged(m_files, id2 + QStringLiteral("/keep.txt")));
        QVERIFY(QFile::exists(outside));
#endif
    }

    void recoveryAfterCrash()
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();

        // Interrupted copy: row says copying, tmp file on disk.
        FileItem copying;
        copying.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        copying.originalName = QStringLiteral("half.bin");
        copying.storedName = copying.id + QStringLiteral("/half.bin");
        copying.addedAt = now;
        copying.status = Status::Copying;
        QVERIFY(m_repo->insert(copying));
        QDir().mkpath(m_files + u'/' + copying.id);
        QFile tmp(m_files + u'/' + copying.id + u'/' + FileService::TmpName);
        QVERIFY(tmp.open(QIODevice::WriteOnly));
        tmp.write("partial");
        tmp.close();

        // Crash between "mark pending" and "delete file".
        const FileItem pending = addItem(writeSource(QStringLiteral("p.txt"), "p"), now);
        QVERIFY(m_repo->setStatus(pending.id, Status::PendingCleanup));

        // Stored file deleted behind our back.
        const FileItem missing = addItem(writeSource(QStringLiteral("m.txt"), "m"), now);
        QFile::remove(path(missing));

        const FileItem ok = addItem(writeSource(QStringLiteral("ok.txt"), "ok"), now);

        CleanupService::recover(*m_repo, m_files);
        CleanupService::purge(*m_repo, m_files, now, DefaultRetentionDays);

        QVERIFY(!QDir(m_files + u'/' + copying.id).exists());
        QVERIFY(!QDir(m_files + u'/' + pending.id).exists());
        const QList<FileItem> left = m_repo->visible();
        QCOMPARE(left.size(), 2);
        for (const FileItem &i : left) {
            if (i.id == missing.id)
                QCOMPARE(i.status, Status::Error);
            else
                QCOMPARE(i.id, ok.id);
        }
        QVERIFY(QFile::exists(path(ok)));
    }

    void dockParsesDesktopEntries()
    {
        const QByteArray text =
            "[Desktop Entry]\nType=Application\nName=Fire Fox\nIcon=firefox\n"
            "Exec=env MOZ_ENABLE=1 /usr/lib/firefox/Firefox --new-window %u %%x\n"
            "[Desktop Action new]\nName=Other\nExec=ignored\n";
        const DockApp app = dockmodel::parseDesktopEntry(QStringLiteral("firefox.desktop"), text);
        QVERIFY(app.valid());
        QCOMPARE(app.name, QStringLiteral("Fire Fox"));
        QCOMPARE(app.icon, QStringLiteral("firefox"));
        QCOMPARE(app.key, QStringLiteral("firefox"));
        QCOMPARE(app.command, (QStringList{"env", "MOZ_ENABLE=1", "/usr/lib/firefox/Firefox", "--new-window", "%x"}));

        QVERIFY(!dockmodel::parseDesktopEntry(QStringLiteral("a.desktop"), "[Desktop Entry]\nType=Application\nName=A\nExec=a\nNoDisplay=true\n").valid());
        QVERIFY(!dockmodel::parseDesktopEntry(QStringLiteral("b.desktop"), "[Desktop Entry]\nType=Link\nName=B\nExec=b\n").valid());
        QVERIFY(!dockmodel::parseDesktopEntry(QStringLiteral("c.desktop"), "[Desktop Entry]\nType=Application\nName=C\n").valid());
    }

    void dockMergesPinnedAndRunning()
    {
        auto app = [](const char *id, const char *name) {
            DockApp a;
            a.id = QString::fromLatin1(id);
            a.name = QString::fromLatin1(name);
            a.command = {a.id};
            return a;
        };
        const DockApp files = app("files", "Files"), mail = app("mail", "Mail"), zed = app("zed", "Zed"), alpha = app("alpha", "Alpha");
        const QHash<QString, DockApp> catalog{{"files", files}, {"mail", mail}, {"zed", zed}, {"alpha", alpha}};

        const QVariantList items = dockmodel::merge({"mail", "gone", "files"}, catalog, {zed, files, alpha}, QStringLiteral("files"));
        QStringList ids;
        for (const QVariant &v : items)
            ids << v.toMap().value("id").toString();
        // Pinned keep their saved order (even uninstalled ones); the other running apps follow by name.
        QCOMPARE(ids, (QStringList{"mail", "gone", "files", "alpha", "zed"}));
        QVERIFY(!items[0].toMap().value("running").toBool());
        QVERIFY(items[2].toMap().value("running").toBool());
        QVERIFY(items[2].toMap().value("active").toBool());
        QVERIFY(items[2].toMap().value("pinned").toBool());
        QVERIFY(!items[3].toMap().value("pinned").toBool());
        QVERIFY(items[3].toMap().value("running").toBool());
        QVERIFY(!items[3].toMap().value("active").toBool());

        // With running apps hidden, only the pinned ones stay (still marked running).
        const QVariantList pinnedOnly = dockmodel::merge({"files"}, catalog, {zed, files}, {}, false);
        QCOMPARE(pinnedOnly.size(), 1);
        QVERIFY(pinnedOnly[0].toMap().value("running").toBool());
    }

    void dockMatchesWindows()
    {
        auto app = [](const char *id, const char *key, const char *wmClass = "") {
            DockApp a;
            a.id = QString::fromLatin1(id);
            a.name = a.id;
            a.key = QString::fromLatin1(key);
            a.wmClass = QString::fromLatin1(wmClass);
            a.command = {a.key};
            return a;
        };
        QHash<QString, DockApp> catalog;
        for (const DockApp &a : {app("org.kde.dolphin.desktop", "dolphin"), app("code.desktop", "code", "Code"),
                                 app("google-chrome.desktop", "google-chrome-stable", "Google-chrome"),
                                 app("firefox.desktop", "firefox")})
            catalog.insert(a.id, a);

        // Wayland apps name their desktop file; X11 ones only carry a window class.
        QCOMPARE(dockmodel::matchWindow(catalog, "org.kde.dolphin", "dolphin"), QStringLiteral("org.kde.dolphin.desktop"));
        QCOMPARE(dockmodel::matchWindow(catalog, "/usr/share/applications/code.desktop", ""), QStringLiteral("code.desktop"));
        QCOMPARE(dockmodel::matchWindow(catalog, "", "Google-chrome"), QStringLiteral("google-chrome.desktop"));
        QCOMPARE(dockmodel::matchWindow(catalog, "", "dolphin"), QStringLiteral("org.kde.dolphin.desktop"));
        QCOMPARE(dockmodel::matchWindow(catalog, "", "firefox"), QStringLiteral("firefox.desktop"));
        QCOMPARE(dockmodel::matchWindow(catalog, "", "xterm"), QString());

        // A pinned app the catalog doesn't know (uninstalled) can't be launched; running ones keep their name.
        DockApp adHoc;
        adHoc.id = QStringLiteral("window:xterm");
        adHoc.name = QStringLiteral("Xterm");
        const QVariantList items = dockmodel::merge({"window:xterm", "code.desktop"}, catalog, {adHoc}, {});
        QCOMPARE(items[0].toMap().value("name").toString(), QStringLiteral("Xterm"));
        QVERIFY(!items[0].toMap().value("canLaunch").toBool());
        QVERIFY(items[1].toMap().value("canLaunch").toBool());
    }

    void dockOpensFilesWithApps()
    {
        const QStringList files{"/tmp/a b.txt", "/tmp/c.png"};
        // %F: every path; %u: the first as a URL; no code: paths appended; other codes dropped.
        QCOMPARE(dockmodel::openArguments({"kate", "-b", "%F"}, files), (QStringList{"kate", "-b", "/tmp/a b.txt", "/tmp/c.png"}));
        QCOMPARE(dockmodel::openArguments({"firefox", "%u"}, files), (QStringList{"firefox", "file:///tmp/a b.txt"}));
        QCOMPARE(dockmodel::openArguments({"gimp", "--icon", "%i"}, files), (QStringList{"gimp", "--icon", "/tmp/a b.txt", "/tmp/c.png"}));
        QCOMPARE(dockmodel::openArguments({"app", "--file=%f", "%%x"}, files), (QStringList{"app", "--file=/tmp/a b.txt", "%x"}));
        const DockApp parsed = dockmodel::parseDesktopEntry("k.desktop", "[Desktop Entry]\nType=Application\nName=K\nExec=kate -b %U\n");
        QCOMPARE(parsed.exec, (QStringList{"kate", "-b", "%U"}));
        QCOMPARE(parsed.command, (QStringList{"kate", "-b"}));
    }

    void stretchReminderCountsRealWork()
    {
        using E = StretchTimer::Event;
        using S = StretchTimer::State;
        StretchTimer t;
        t.workLimitMs = 60000; // 1 min of work
        t.idleResetMs = 30000;
        t.breakMs = 20000;
        qint64 now = 0;
        auto run = [&](qint64 ms, qint64 idle, bool busy = false) {
            E last = E::None;
            for (qint64 done = 0; done < ms; done += 5000) {
                now += 5000;
                const E e = t.tick(now, 5000, idle, busy);
                if (e != E::None)
                    last = e;
            }
            return last;
        };

        // Working (input within the threshold) adds up to a reminder.
        QCOMPARE(run(55000, 1000), E::None);
        QCOMPARE(run(5000, 1000), E::Remind);
        QCOMPARE(t.state, S::Reminding);
        // "Later": nothing until the snooze is over, then asked again.
        t.snooze(now, 20000);
        QCOMPARE(run(15000, 1000), E::None);
        QCOMPARE(run(5000, 1000), E::Remind);
        // Break: over after its length, and work starts from zero.
        t.startBreak(now);
        QCOMPARE(run(15000, 1000), E::None);
        QCOMPARE(run(5000, 1000), E::BreakOver);
        QCOMPARE(t.state, S::Working);
        QCOMPARE(t.workedMs, 0);

        // Walking away for longer than the idle threshold counts as the break.
        run(40000, 1000);
        run(5000, 31000);
        QCOMPARE(t.workedMs, 0);
        // A film or a call holds the reminder back until it's over.
        QCOMPARE(run(60000, 1000, true), E::None);
        QCOMPARE(run(5000, 1000), E::Remind);
        // "Skip" starts a full period again.
        t.skip();
        QCOMPARE(t.workedMs, 0);
        QCOMPARE(t.state, S::Working);

        // A suspend (a huge gap between ticks) isn't counted as work.
        t.tick(now + 3600000, 3600000, 1000, false);
        QVERIFY(t.workedMs <= 15000);

        // 20-20-20: an eye reminder every interval of work.
        StretchTimer eyes;
        eyes.eyeIntervalMs = 20000;
        qint64 n = 0;
        int seen = 0;
        for (int i = 0; i < 8; ++i)
            seen += eyes.tick(n += 5000, 5000, 1000, false) == E::EyeBreak;
        QCOMPARE(seen, 2);
    }

    void dockMovesPinned()
    {
        QCOMPARE(dockmodel::move({"a", "b", "c"}, 0, 2), (QStringList{"b", "c", "a"}));
        QCOMPARE(dockmodel::move({"a", "b", "c"}, 2, 0), (QStringList{"c", "a", "b"}));
        QCOMPARE(dockmodel::move({"a", "b"}, 0, 5), (QStringList{"a", "b"}));
    }
};

// DocTools paints with QPdfWriter/QImage: needs a GUI app, offscreen so tests run anywhere.
int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestCore test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_core.moc"
