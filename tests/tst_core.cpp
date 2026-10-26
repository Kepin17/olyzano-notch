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
#include "platform/DbusMenuModel.h"
#include "platform/GamingModel.h"
#include "platform/KaraokeModel.h"
#include "platform/CalendarModel.h"
#include "platform/DoomscrollModel.h"
#include "platform/TasksModel.h"
#include "platform/Cities.h"
#include "platform/PrayerModel.h"
#include "platform/ReserveModel.h"
#include "platform/UpdateModel.h"
#include "platform/VoiceModel.h"
#include "platform/WidgetModel.h"
#include "platform/LauncherModel.h"
#include "platform/ColorFormat.h"
#include "platform/PanelModel.h"
#include "platform/LrcParser.h"
#include "platform/Lyrics.h"
#include "platform/LyricsQuery.h"
#include "platform/NotificationFilter.h"
#include "platform/SoundsModel.h"
#include "platform/AudioBands.h"
#include "platform/DiscordCall.h"
#include "platform/DiscordIpc.h"
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

// A web server on localhost for the lyrics lookups: what each path answers, and how often it was asked.
struct FakeLyricsServer {
    QTcpServer server;
    QHash<QString, QPair<int, QByteArray>> routes; // path -> status, body
    int hits = 0;
    FakeLyricsServer()
    {
        server.listen(QHostAddress::LocalHost);
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (QTcpSocket *s = server.nextPendingConnection())
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    const QByteArray head = s->readAll();
                    if (!head.contains("\r\n\r\n"))
                        return;
                    ++hits;
                    const QUrl asked(QString::fromLatin1(head.split(' ').value(1)));
                    const QString path = asked.path();
                    // "/api/search?q" answers the search by words, the plain path every other one
                    const auto r = asked.query().contains(QStringLiteral("q=")) && routes.contains(path + QStringLiteral("?q"))
                                       ? routes.value(path + QStringLiteral("?q")) : routes.value(path, {404, "{}"});
                    s->write("HTTP/1.1 " + QByteArray::number(r.first) + " X\r\nContent-Type: application/json\r\nContent-Length: "
                             + QByteArray::number(r.second.size()) + "\r\nConnection: close\r\n\r\n" + r.second);
                    s->disconnectFromHost();
                });
        });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())); }
};

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
        QCOMPARE(q.value(0).toInt(), 6);
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

    void emptyCategoriesExpireAfterADay()
    {
        const QDateTime t0 = QDateTime::currentDateTimeUtc();
        const auto at = [&](int hours) { return t0.addSecs(hours * 3600); };
        QVERIFY(m_repo->insertCategory({QStringLiteral("e1"), QStringLiteral("Empty")}));
        QVERIFY(m_repo->insertCategory({QStringLiteral("e2"), QStringLiteral("Full")}));
        const FileItem item = addItem(writeSource(QStringLiteral("f.txt"), "f"), t0);
        QVERIFY(m_repo->setCategory(item.id, QStringLiteral("e2")));
        QVERIFY(m_repo->staleCategories(at(0), at(-24)).isEmpty());   // e1 is stamped now
        QVERIFY(m_repo->staleCategories(at(23), at(-1)).isEmpty());   // not a day yet
        QCOMPARE(m_repo->staleCategories(at(25), at(1)), QStringList{QStringLiteral("e1")});
        QVERIFY(m_repo->setCategory(item.id, QStringLiteral("e1")));  // e1 filled, e2 emptied: clocks swap
        QVERIFY(m_repo->staleCategories(at(26), at(2)).isEmpty());
        QCOMPARE(m_repo->staleCategories(at(51), at(27)), QStringList{QStringLiteral("e2")});
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
        QCOMPARE(MediaNames::displayName(QStringLiteral("Discord.exe")), QStringLiteral("Discord"));
        QCOMPARE(MediaNames::processName(QStringLiteral("com.squirrel.Discord.Discord")), QStringLiteral("discord"));
    }

    void tasksParseAndSort()
    {
        using namespace TasksModel;
        const QDateTime now(QDate(2026, 11, 2), QTime(10, 0));
        QCOMPARE(parseDue("17:30", now), QDateTime(QDate(2026, 11, 2), QTime(17, 30)));
        QCOMPARE(parseDue("9:00", now), QDateTime(QDate(2026, 11, 3), QTime(9, 0))); // already past today
        QCOMPARE(parseDue("tomorrow", now), QDateTime(QDate(2026, 11, 3), QTime(9, 0)));
        QCOMPARE(parseDue("today 18:00", now), QDateTime(QDate(2026, 11, 2), QTime(18, 0)));
        QCOMPARE(parseDue("in 30m", now), now.addSecs(1800));
        QCOMPARE(parseDue("in 2 h", now), now.addSecs(7200));
        QCOMPARE(parseDue("2026-12-01 08:15", now), QDateTime(QDate(2026, 12, 1), QTime(8, 15)));
        QCOMPARE(parseDue("fri 17:00", now), QDateTime(QDate(2026, 11, 6), QTime(17, 0)));
        QCOMPARE(parseDue("monday", now), QDateTime(QDate(2026, 11, 9), QTime(9, 0))); // today is a Monday: next week
        QCOMPARE(parseDue("12 nov 17:00", now), QDateTime(QDate(2026, 11, 12), QTime(17, 0)));
        QCOMPARE(parseDue("12/11", now), QDateTime(QDate(2026, 11, 12), QTime(9, 0)));
        QCOMPARE(parseDue("1/1", now), QDateTime(QDate(2027, 1, 1), QTime(9, 0)));
        QCOMPARE(parseDue("5/3/2027 8:30", now), QDateTime(QDate(2027, 3, 5), QTime(8, 30)));
        QVERIFY(!parseDue("31/2", now).isValid());
        QVERIFY(!parseDue("25:00", now).isValid());
        QVERIFY(!parseDue("soon", now).isValid());
        const Quick q = parseQuick("Buy milk tomorrow 17:00", now);
        QCOMPARE(q.title, QStringLiteral("Buy milk"));
        QCOMPARE(q.due, QDateTime(QDate(2026, 11, 3), QTime(17, 0)));
        QCOMPARE(parseQuick("Call Ana @ in 45m", now).title, QStringLiteral("Call Ana"));
        QVERIFY(!parseQuick("Water the plants", now).due.isValid());
        QCOMPARE(parseQuick("Read 17:30", now).title, QStringLiteral("Read"));
        Task a{"a", "late", now.addSecs(7200)}, b{"b", "none", {}}, c{"c", "soon", now.addSecs(600)}, d{"d", "done", now.addSecs(60)};
        d.done = true;
        const QList<Task> s = sorted({a, b, c, d});
        QCOMPARE(s[0].id, QStringLiteral("c"));
        QCOMPARE(s[1].id, QStringLiteral("a"));
        QCOMPARE(s[2].id, QStringLiteral("b"));
        QCOMPARE(s[3].id, QStringLiteral("d"));
        QCOMPARE(nextOpen({a, b, c, d}), 2);
        QCOMPARE(int(dueState(c, now)), int(DueState::Soon));
        QCOMPARE(int(dueState(a, now)), int(DueState::Later));
        QCOMPARE(int(dueState(d, now)), int(DueState::None));
        QCOMPARE(remaining(now.addSecs(1500), now), QStringLiteral("in 25 min"));
        QCOMPARE(remaining(now.addSecs(-7200), now), QStringLiteral("overdue 2 h"));
        QCOMPARE(remaining(QDateTime(QDate(2026, 11, 3), QTime(9, 0)), now), QStringLiteral("tomorrow 09:00"));
        QCOMPARE(fromJson(toJson({a, b})).size(), 2);
        QCOMPARE(fromJson(toJson({a})).first().due, a.due);
    }

    void tasksRemindOnce()
    {
        using namespace TasksModel;
        const QDateTime now(QDate(2026, 11, 2), QTime(10, 0));
        QList<Task> list{Task{"a", "report", now.addSecs(20 * 60)}, Task{"old", "ancient", now.addDays(-3)}};
        QVERIFY(fire(list, now).size() == 0 + 0); // the old one is settled quietly, the other is 20 minutes away
        QVERIFY(list[1].notified);
        QCOMPARE(nextWake(list, now), now.addSecs(5 * 60)); // 15 minutes before
        const QList<Event> soon = fire(list, now.addSecs(6 * 60));
        QCOMPARE(soon.size(), 1);
        QVERIFY(!soon[0].now);
        QCOMPARE(fire(list, now.addSecs(7 * 60)).size(), 0); // once
        const QList<Event> due = fire(list, now.addSecs(21 * 60));
        QCOMPARE(due.size(), 1);
        QVERIFY(due[0].now);
        QCOMPARE(fire(list, now.addSecs(22 * 60)).size(), 0);
        QVERIFY(!nextWake(list, now.addSecs(22 * 60)).isValid());
    }

    void widgetGeometry()
    {
        using namespace WidgetModel;
        const QSizeF screen(1920, 1080);
        // floating: the window centres on the card, and stays on the screen at the edges
        QCOMPARE(windowRect(Floating, 960, 14, screen), QRectF(600, 14, FloatW, FloatH));
        QCOMPARE(windowRect(Floating, 40, 14, screen).x(), 0.0);
        QCOMPARE(windowRect(Floating, 1900, 14, screen).right(), 1920.0);
        QCOMPARE(windowRect(Floating, 960, 1075, screen).bottom(), 1080.0);
        // open: the big window; the card is drawn at the same screen spot inside it
        const QRectF open = windowRect(Open, 100, 900, screen);
        QCOMPARE(open, QRectF(0, 460, OpenW, OpenH));
        QCOMPARE(islandX(open, 100, 400), 0.0);              // would stick out on the left: pushed in
        QCOMPARE(islandX(windowRect(Open, 960, 14, screen), 960, 400), 220.0);
        QCOMPARE(islandY(open, 900, 38), 440.0);             // 900 - 460
        QCOMPARE(islandY(open, 1075, 38), OpenH - 38);       // the panel never leaves the window
        // snap: close to an edge or the middle sticks, far away does not
        QCOMPARE(snap(965, 300, 400, 38, screen), QPointF(960, 300));
        QCOMPARE(snap(215, 300, 400, 38, screen), QPointF(200 + EdgeGap, 300));
        QCOMPARE(snap(1000, 14, 400, 38, screen).y(), EdgeGap);
        QCOMPARE(snap(1000, 1030, 400, 38, screen).y(), 1080 - 38 - EdgeGap);
        QCOMPARE(snap(700, 300, 400, 38, screen), QPointF(700, 300));
        QCOMPARE(snap(-50, 5000, 400, 38, screen), QPointF(200 + EdgeGap, 1080 - 38 - EdgeGap)); // off screen: back on
        QCOMPARE(windowRect(Dragging, 100, 100, screen), QRectF(0, 0, 1920, 1080)); // a drag: the whole screen
        QCOMPARE(islandX(windowRect(Dragging, 100, 100, screen), 500, 400), 300.0);
        QCOMPARE(defaultAnchor(screen), QPointF(960, 14));
    }

    void prayerMatchesReference()
    {
        using namespace PrayerModel;
        // Fajr, Sunrise, Dhuhr, Asr, Maghrib, Isha from the Aladhan calculator for the same method, place and day.
        // The plain method is checked within a minute; Kemenag adds its 2 minutes of caution (and takes 2 from sunrise).
        struct Ref { QDate d; double lat, lon, tz; const char *method; const char *t[6]; };
        const Ref refs[] = {
            {QDate(2026, 11, 2), -6.2088, 106.8456, 7, "kemenag", {"04:06", "05:26", "11:36", "14:54", "17:46", "18:58"}},
            {QDate(2026, 1, 15), -5.1477, 119.4327, 8, "kemenag", {"04:36", "06:00", "12:12", "15:36", "18:23", "19:38"}},
            {QDate(2026, 3, 20), 21.4225, 39.8262, 3, "ummalqura", {"05:09", "06:25", "12:28", "15:52", "18:32", "20:02"}},
            {QDate(2026, 6, 21), 51.5074, -0.1278, 1, "mwl", {"02:31", "04:43", "13:02", "17:25", "21:22", "23:27"}},
        };
        for (const Ref &r : refs) {
            const Method &m = method(QString::fromLatin1(r.method));
            const Times t = times(r.d, r.lat, r.lon, r.tz, m);
            QVERIFY(t.valid());
            for (int i = 0; i < PrayerCount; ++i) {
                const QTime want = QTime::fromString(QString::fromLatin1(r.t[i]), "HH:mm").addSecs((i == Sunrise ? -m.ihtiyat : m.ihtiyat) * 60);
                const int off = qAbs(want.secsTo(t.at[i].time())) / 60;
                if (off > 1)
                    qWarning() << r.method << r.d << shortName(i) << "got" << t.at[i].toString("HH:mm") << "want" << want.toString("HH:mm");
                QVERIFY(off <= 1);
            }
            QVERIFY(t.at[Fajr] < t.at[Sunrise] && t.at[Sunrise] < t.at[Dhuhr] && t.at[Dhuhr] < t.at[Asr]
                    && t.at[Asr] < t.at[Maghrib] && t.at[Maghrib] < t.at[Isha]);
        }
        // Hanafi Asr is later; Umm al-Qura Isha is 90 minutes after Maghrib; the next prayer; names
        const Times a = times(QDate(2026, 11, 2), -6.2088, 106.8456, 7, method("mwl"), 1);
        QVERIFY(times(QDate(2026, 11, 2), -6.2088, 106.8456, 7, method("mwl"), 2).at[Asr] > a.at[Asr]);
        const Times q = times(QDate(2026, 3, 20), 21.4225, 39.8262, 3, method("ummalqura"));
        QCOMPARE(q.at[Maghrib].secsTo(q.at[Isha]), 90 * 60);
        QCOMPARE(next(a, a.at[Dhuhr].addSecs(-60)), int(Dhuhr));
        QCOMPARE(next(a, a.at[Fajr].addSecs(-3600)), int(Fajr));
        QCOMPARE(next(a, a.at[Isha].addSecs(60)), -1);
        QCOMPARE(name(Dhuhr, QDate(2026, 11, 6)), QStringLiteral("Dhuhr · Jumat")); // a Friday
        QCOMPARE(name(Dhuhr, QDate(2026, 11, 2)), QStringLiteral("Dhuhr · Zuhur"));
        // Far north in summer the twilight never ends: no nonsense, Fajr still before sunrise and Isha after Maghrib
        const Times n = times(QDate(2026, 6, 21), 64.1, -21.9, 0, method("mwl")); // Reykjavik
        QVERIFY(!n.valid() || (n.at[Fajr] < n.at[Sunrise] && n.at[Isha] > n.at[Maghrib]));
    }

    void prayerCities()
    {
        QCOMPARE(Cities::guess("Asia/Jakarta"), QStringLiteral("Jakarta"));
        QCOMPARE(Cities::guess("Asia/Makassar"), QStringLiteral("Makassar"));
        QVERIFY(Cities::guess("Antarctica/Troll").isEmpty());
        for (const char *z : {"Asia/Jakarta", "Asia/Makassar", "Asia/Jayapura", "Asia/Pontianak", "Asia/Riyadh", "Europe/London", "Australia/Perth"})
            QVERIFY(Cities::indexOf(Cities::guess(z)) >= 0); // every guess is a city that is in the list
        int n = 0;
        const Cities::City *c = Cities::all(&n);
        QVERIFY(n > 80);
        for (int i = 0; i < n; ++i)
            QVERIFY(c[i].lat >= -90 && c[i].lat <= 90 && c[i].lon >= -180 && c[i].lon <= 180);
    }

    void voiceFollowsTheRoom()
    {
        using VoiceModel::Detector;
        qint64 t = 0;
        // a hot microphone: the room hums at 1800 RMS, which a fixed line at ~1300 would call a voice all day
        for (int level : {0, 1, 2}) {
            Detector d;
            d.level = level;
            bool ever = false;
            for (int i = 0; i < 400; ++i, t += 60)
                ever = d.update(1800 + (i % 3) * 40, t) || ever;
            QVERIFY2(!ever || level == 2, "a steady hum must not read as a voice");
        }
        Detector d;
        for (int i = 0; i < 100; ++i, t += 60)
            QVERIFY(!d.update(1800, t));
        QVERIFY(d.update(9000, t) == false);          // one loud chunk alone: a click
        t += 60;
        QVERIFY(d.update(9000, t));                   // two in a row: a voice
        t += 60;
        QVERIFY(d.update(1800, t));                   // between words it holds
        t += 400;
        QVERIFY(!d.update(1800, t));                  // then lets go
        QVERIFY(d.floor() > 1700 && d.floor() < 1900); // and the voice did not raise the background
        // a quiet room: ordinary speech stands out, the room does not
        Detector q;
        for (int i = 0; i < 50; ++i, t += 60)
            QVERIFY(!q.update(150, t));
        q.update(4000, t += 60);
        QVERIFY(q.update(4000, t += 60));
        // the room getting louder is learned slowly, so it stops counting as a voice after a while
        Detector n;
        for (int i = 0; i < 50; ++i, t += 60)
            n.update(300, t);
        bool stillVoice = true;
        for (int i = 0; i < 1500 && stillVoice; ++i, t += 60)
            stillVoice = n.update(2000, t);
        QVERIFY(!stillVoice);
        QVERIFY(VoiceModel::sensitivity(0).least > VoiceModel::sensitivity(1).least && VoiceModel::sensitivity(1).least > VoiceModel::sensitivity(2).least);
        QByteArray two(8, 0);
        reinterpret_cast<qint16 *>(two.data())[0] = 1000;
        reinterpret_cast<qint16 *>(two.data())[1] = -1000;
        QVERIFY(qAbs(VoiceModel::rms(two) - std::sqrt(2e6 / 4)) < 1);
    }

    void reservePlan()
    {
        using namespace ReserveModel;
        QCOMPARE(heightFor(1.0), 44);
        QCOMPARE(heightFor(1.3), 57);
        QCOMPARE(heightFor(0.8), 35);
        QCOMPARE(heightFor(9), 57); // the scale is clamped like the setting
        // off, or any mode but the capsule: nothing is kept
        QCOMPARE(plan("capsule", false, 1, 0, true).notchZone, 0);
        for (const char *mode : {"hidden", "widget", "open"}) {
            QCOMPARE(plan(mode, true, 1, 0, true).notchZone, 0);
            QCOMPARE(plan(mode, true, 1, 0, false).appBar, 0);
        }
        // Wayland: the island adds only what the status bar does not already keep
        QCOMPARE(plan("capsule", true, 1, 0, true).notchZone, 44);
        QCOMPARE(plan("capsule", true, 1, 30, true).notchZone, 14);
        QCOMPARE(plan("capsule", true, 1, 60, true).notchZone, 0);
        // Windows: one strip, the taller of the two
        QCOMPARE(plan("capsule", true, 1, 0, false).appBar, 44);
        QCOMPARE(plan("capsule", true, 1, 30, false).appBar, 44);
        QCOMPARE(plan("capsule", true, 1, 60, false).appBar, 60);
        QCOMPARE(plan("capsule", false, 1, 30, false).appBar, 30); // the bar alone still keeps its own room
        QCOMPARE(plan("hidden", false, 1, 30, true).notchZone, 0);
    }

    void contributorsSkipBots()
    {
        const QByteArray json = R"([{"login":"Kepin17","type":"User"},{"login":"dependabot[bot]","type":"Bot"},{"login":"helngopidingin","type":"User"}])";
        QCOMPARE(UpdateModel::parseContributors(json), (QStringList{"Kepin17", "helngopidingin"}));
        QVERIFY(UpdateModel::parseContributors("{}").isEmpty());
    }

    void discordPipeFrames()
    {
        using namespace DiscordIpc;
        // A frame is opcode + length (both little endian int32) + JSON, and one read can hold several.
        QByteArray wire = encode(Handshake, {{"v", 1}, {"client_id", "123"}}) + encode(Frame, {{"cmd", "GET_VOICE_SETTINGS"}});
        QCOMPARE(wire.left(8), QByteArray("\x00\x00\x00\x00\x19\x00\x00\x00", 8)); // op 0, 25 bytes of JSON
        qint32 op = -1;
        QJsonObject message;
        QVERIFY(take(wire, &op, &message));
        QCOMPARE(op, qint32(Handshake));
        QCOMPARE(message.value("client_id").toString(), QStringLiteral("123"));
        QVERIFY(take(wire, &op, &message));
        QCOMPARE(op, qint32(Frame));
        QCOMPARE(message.value("cmd").toString(), QStringLiteral("GET_VOICE_SETTINGS"));
        QVERIFY(wire.isEmpty());
        // Half a frame waits for the rest; nonsense in the header is dropped, not buffered for ever.
        QByteArray half = encode(Frame, {{"cmd", "x"}});
        QByteArray rest = half.mid(half.size() - 3);
        half.chop(3);
        QVERIFY(!take(half, &op, &message));
        half += rest;
        QVERIFY(take(half, &op, &message));
        QByteArray junk("\x01\x00\x00\x00\xff\xff\xff\x7f", 8);
        QVERIFY(!take(junk, &op, &message));
        QVERIFY(junk.isEmpty());
    }

    void discordMuteState()
    {
        using DiscordIpc::Mute;
        QCOMPARE(DiscordIpc::muteFrom({}), Mute::Unknown);
        QCOMPARE(DiscordIpc::muteFrom({{"mute", false}, {"deaf", false}}), Mute::Live);
        QCOMPARE(DiscordIpc::muteFrom({{"mute", true}, {"deaf", false}}), Mute::Muted);
        QCOMPARE(DiscordIpc::muteFrom({{"mute", true}, {"deaf", true}}), Mute::Deafened);
        QCOMPARE(DiscordIpc::muteFrom({{"deaf", true}}), Mute::Deafened); // deafened wins whatever `mute` says
    }

    void discordCallState()
    {
        CallTracker call;
        QCOMPARE(call.update(false, 0.0), CallState::Off);
        QCOMPARE(call.update(true, 0.0), CallState::InCall);   // in a call, nobody talking
        QCOMPARE(call.update(true, 0.3), CallState::Talking);  // a voice
        for (int i = 0; i < 4; ++i)                            // short gaps between words keep it talking
            QCOMPARE(call.update(true, 0.0), CallState::Talking);
        QCOMPARE(call.update(true, 0.0), CallState::InCall);   // a longer silence ends it
        QCOMPARE(call.update(true, 0.3), CallState::Talking);
        QCOMPARE(call.update(false, 0.3), CallState::Off);     // leaving the call wins over the meter
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

    void lrcParsing()
    {
        const auto lines = Lrc::parse(QStringLiteral(
            "[ar:Someone]\n[ti:Song]\n[00:12.50] second\r\n[00:01.00]first\n[01:02.345][02:00.00] again\nplain text\n[00:30.00]\n[03:00.00]\n[03:10.00]\n"));
        QCOMPARE(lines.size(), 5); // the trailing gap markers are dropped, the one between lines stays
        QCOMPARE(lines[0].ms, qint64(1000));
        QCOMPARE(lines[0].text, QStringLiteral("first"));
        QCOMPARE(lines[1].ms, qint64(12500)); // hundredths
        QCOMPARE(lines[2].ms, qint64(30000));
        QVERIFY(lines[2].text.isEmpty());
        QCOMPARE(lines[3].ms, qint64(62345)); // milliseconds, and the same words twice
        QCOMPARE(lines[4].ms, qint64(120000));
        QCOMPARE(lines[4].text, QStringLiteral("again"));

        const auto shifted = Lrc::parse(QStringLiteral("[offset:500]\n[00:10.00] x\n[00:00.20] y"));
        QCOMPARE(shifted[0].ms, qint64(0));    // never before the start
        QCOMPARE(shifted[1].ms, qint64(9500)); // a positive offset brings the words sooner
        QVERIFY(Lrc::parse(QStringLiteral("no stamps here\n[bad:tag]")).isEmpty());

        QCOMPARE(Lrc::indexAt(lines, 0), -1);
        QCOMPARE(Lrc::indexAt(lines, 1000), 0);
        QCOMPARE(Lrc::indexAt(lines, 12499), 0);
        QCOMPARE(Lrc::indexAt(lines, 12500), 1);
        QCOMPARE(Lrc::indexAt(lines, 999999), 4);
        QCOMPARE(Lrc::indexAt({}, 5), -1);
    }

    void lyricsAreLookedUpCachedAndOffByDefault()
    {
        FakeLyricsServer web;
        const QByteArray song = R"({"syncedLyrics":"[00:01.00] hello\n[00:05.00] there","plainLyrics":"hello\nthere","instrumental":false})";
        web.routes.insert(QStringLiteral("/api/get"), {200, song});
        const QString conf = m_tmp.filePath(QStringLiteral("lyrics-a.conf")), cache = m_tmp.filePath(QStringLiteral("lyrics-cache"));
        {
            Lyrics off(conf, cache);
            off.setBaseUrl(web.url());
            QCOMPARE(off.state(), QStringLiteral("off"));
            QVERIFY(!off.consented()); // never switched on yet
            off.track(QStringLiteral("Artist"), QStringLiteral("Song"), QStringLiteral("Album"), 200000);
            QTest::qWait(150);
            QCOMPARE(web.hits, 0); // nothing is sent until it is switched on
            QCOMPARE(off.requests(), 0);

            off.setEnabled(true); // asks for the song already playing
            QTRY_COMPARE(off.state(), QStringLiteral("synced"));
            QCOMPARE(web.hits, 1);
            QCOMPARE(off.lines().size(), 2);
            off.setPosition(500);
            QCOMPARE(off.currentIndex(), -1);
            off.setPosition(1500);
            QCOMPARE(off.currentText(), QStringLiteral("hello"));
            off.setOffsetMs(1000); // the words come a second sooner
            QCOMPARE(off.currentText(), QStringLiteral("hello"));
            off.setPosition(4200);
            QCOMPARE(off.currentText(), QStringLiteral("there"));
            off.setEnabled(false);
            QCOMPARE(off.state(), QStringLiteral("off"));
            QVERIFY(off.lines().isEmpty());
            QVERIFY(off.consented()); // switching it off from the Music page must not ask again
        }
        { // a new run finds it in the cache: no second request
            Lyrics again(conf, cache);
            again.setBaseUrl(web.url());
            again.setEnabled(true);
            again.track(QStringLiteral("artist"), QStringLiteral("SONG"), QString(), 200500);
            QCOMPARE(again.state(), QStringLiteral("synced"));
            QCOMPARE(web.hits, 1);
        }
    }

    void lyricsFallBackToSearchAndRememberMisses()
    {
        FakeLyricsServer web;
        web.routes.insert(QStringLiteral("/api/get"), {404, "{}"});
        web.routes.insert(QStringLiteral("/api/search"), {200, R"([{"duration":300,"plainLyrics":"only words"},)"
                                                              R"({"duration":199,"syncedLyrics":"[00:02.00] close one","plainLyrics":"close one"}])"});
        const QString cache = m_tmp.filePath(QStringLiteral("lyrics-cache-b"));
        Lyrics l(m_tmp.filePath(QStringLiteral("lyrics-b.conf")), cache);
        l.setBaseUrl(web.url());
        l.setEnabled(true);
        l.track(QStringLiteral("A"), QStringLiteral("B"), QString(), 200000);
        QTRY_COMPARE(l.state(), QStringLiteral("synced"));
        QCOMPARE(web.hits, 2); // the exact lookup, then the search
        l.setPosition(2500);
        QCOMPARE(l.currentText(), QStringLiteral("close one")); // the synced one closest in length

        web.routes.insert(QStringLiteral("/api/search"), {200, "[]"});
        l.track(QStringLiteral("Nobody"), QStringLiteral("Unknown"), QString(), 100000);
        QTRY_COMPARE(l.state(), QStringLiteral("none"));
        const int asked = web.hits;
        l.track(QString(), QString(), QString(), 0); // stops
        QCOMPARE(l.state(), QStringLiteral("none"));
        l.track(QStringLiteral("Nobody"), QStringLiteral("Unknown"), QString(), 100000); // a miss is remembered
        QCOMPARE(l.state(), QStringLiteral("none"));
        QCOMPARE(web.hits, asked);

        web.routes.insert(QStringLiteral("/api/get"), {200, R"({"instrumental":true})"});
        l.track(QStringLiteral("X"), QStringLiteral("Y"), QString(), 90000);
        QTRY_COMPARE(l.state(), QStringLiteral("none"));
        web.routes.insert(QStringLiteral("/api/get"), {500, "oops"});
        l.track(QStringLiteral("P"), QStringLiteral("Q"), QString(), 90000);
        QTRY_COMPARE(l.state(), QStringLiteral("error"));
    }

    void gamingModeOnlyHidesNotchOverAFullScreenApp()
    {
        GamingModel g;
        g.fullScreen = true;
        QVERIFY(!g.active()); // switched off: a full-screen window changes nothing
        g.enabled = true;
        QVERIFY(g.active());  // on, with a game full screen: Notch keeps away
        g.fullScreen = false;
        QVERIFY(!g.active()); // the game is minimised or in a window: Notch is back
        g.fullScreen = true;
        QVERIFY(g.active());
        g.enabled = false;
        QVERIFY(!g.active());
    }

    void quickSettingsModel()
    {
        const QByteArray json = R"([{"name":"alsa_output.a","description":"Speakers","mute":false,
            "volume":{"front-left":{"value_percent":"62%"},"front-right":{"value_percent":"58%"}}},
            {"name":"bluez_output.b","description":"Headphones","mute":true,"volume":{"mono":{"value_percent":"100%"}}},{"description":"no name"}])";
        const auto sinks = PanelModel::parseSinks(json, QStringLiteral("bluez_output.b"));
        QCOMPARE(sinks.size(), 2);
        QCOMPARE(sinks[0].description, QStringLiteral("Speakers"));
        QCOMPARE(sinks[0].volume, 62); // the loudest channel
        QVERIFY(!sinks[0].isDefault && sinks[1].isDefault && sinks[1].mute);
        QVERIFY(PanelModel::parseSinks("not json", {}).isEmpty());
        const QByteArray in = R"([{"name":"alsa_input.usb","description":"USB mic","mute":false,"volume":{"mono":{"value_percent":"80%"}}},
            {"name":"alsa_output.a.monitor","description":"Monitor of Speakers","mute":false,"volume":{"mono":{"value_percent":"100%"}}}])";
        const auto mics = PanelModel::parseSources(in, QStringLiteral("alsa_input.usb"));
        QCOMPARE(mics.size(), 1); // what plays is not a microphone
        QVERIFY(mics[0].isDefault && mics[0].volume == 80);

        {
            using PanelModel::AccessPoint;
            const QList<AccessPoint> aps{{QStringLiteral("Cafe"), QStringLiteral("/a"), 80, false, false}, {QStringLiteral("Home"), QStringLiteral("/b"), 30, true, false},
                                         {QStringLiteral("Home"), QStringLiteral("/c"), 60, true, false}, {QString(), QStringLiteral("/d"), 99, false, false},
                                         {QStringLiteral("Office"), QStringLiteral("/e"), 50, true, true}, {QStringLiteral("Work"), QStringLiteral("/f"), 40, true, false}};
            const auto nets = PanelModel::mergeWifi(aps, {{QStringLiteral("/s1"), QStringLiteral("Home")}, {QStringLiteral("/s2"), QStringLiteral("Work")}}, QStringLiteral("Work"));
            QCOMPARE(nets.size(), 4); // the hidden one is gone, "Home" is merged
            QCOMPARE(nets[0].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Work")); // in use
            QCOMPARE(nets[1].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Home")); // saved
            QCOMPARE(nets[1].toMap().value(QStringLiteral("ap")).toString(), QStringLiteral("/c"));     // its strongest access point
            QCOMPARE(nets[2].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Cafe")); // then by signal
            QVERIFY(nets[3].toMap().value(QStringLiteral("enterprise")).toBool());
            QVariantList bt{QVariantMap{{"name", "far"}, {"rssi", -90}}, QVariantMap{{"name", "near"}, {"rssi", -40}}, QVariantMap{{"name", "mine"}, {"paired", true}},
                            QVariantMap{{"name", "on"}, {"paired", true}, {"connected", true}}};
            PanelModel::orderBluetooth(bt);
            QCOMPARE(bt[0].toMap().value("name").toString(), QStringLiteral("on"));
            QCOMPARE(bt[1].toMap().value("name").toString(), QStringLiteral("mine"));
            QCOMPARE(bt[2].toMap().value("name").toString(), QStringLiteral("near"));
        }
        {
            const QVariantList g = CalendarModel::grid(2028, 2, 1, QDate(2028, 2, 29)); // February 2028 (leap), weeks from Monday
            QCOMPARE(g.size(), 42);
            QCOMPARE(g[0].toMap().value("day").toInt(), 31); // Mon 31 Jan
            QVERIFY(!g[0].toMap().value("inMonth").toBool());
            QCOMPARE(g[1].toMap().value("day").toInt(), 1);
            QVERIFY(g[1].toMap().value("inMonth").toBool());
            QVERIFY(g[29].toMap().value("today").toBool()); // 29 Feb
            QCOMPARE(g[0].toMap().value("week").toInt(), 5);
            QCOMPARE(CalendarModel::grid(2026, 11, 7, QDate()).at(0).toMap().value("day").toInt(), 1); // 1 Nov 2026 is a Sunday
            QVERIFY(CalendarModel::grid(2026, 13, 1, QDate()).isEmpty());
        }
        QCOMPARE(ColorFormat::format(QStringLiteral("#3daee9"), QStringLiteral("hex")), QStringLiteral("#3DAEE9"));
        QCOMPARE(ColorFormat::format(QStringLiteral("#3DAEE9"), QStringLiteral("rgb")), QStringLiteral("rgb(61, 174, 233)"));
        QCOMPARE(ColorFormat::format(QStringLiteral("#ff0000"), QStringLiteral("hsl")), QStringLiteral("hsl(0, 100%, 50%)"));
        QCOMPARE(ColorFormat::format(QStringLiteral("nonsense"), QStringLiteral("rgb")), QStringLiteral("nonsense"));
        QCOMPARE(ColorFormat::readableOn(QStringLiteral("#ffffff")), QStringLiteral("#000000"));
        QCOMPARE(ColorFormat::readableOn(QStringLiteral("#101820")), QStringLiteral("#ffffff"));
        {
            using namespace DoomscrollModel;
            const Options on;
            QCOMPARE(classify({{}, QStringLiteral("google-chrome"), QStringLiteral("(86) Instagram - Google Chrome")}, on), Site::Instagram);
            QCOMPARE(classify({{}, {}, QStringLiteral("TikTok - Make Your Day")}, on), Site::TikTok);
            QCOMPARE(classify({{}, {}, QStringLiteral("How to cook rice - YouTube"), 30000, true}, on), Site::Shorts); // a 30 s item
            QCOMPARE(classify({{}, {}, QStringLiteral("Lofi mix - YouTube"), 3600000, true}, on), Site::None);         // a long video is not
            QCOMPARE(classify({{}, {}, QStringLiteral("cats #shorts - YouTube")}, on), Site::Shorts);
            QCOMPARE(classify({{}, {}, QStringLiteral("Facebook - Reels")}, on), Site::None); // off by default
            Options fb; fb.facebook = true; fb.custom = {QStringLiteral("snack")};
            QCOMPARE(classify({{}, {}, QStringLiteral("Facebook - Reels")}, fb), Site::Facebook);
            QCOMPARE(classify({{}, {}, QStringLiteral("Snackvideo")}, fb), Site::Custom);
            Options none; none.instagram = false;
            QCOMPARE(classify({{}, {}, QStringLiteral("Instagram")}, none), Site::None);

            Tracker t;
            qint64 now = 1000000; int warns = 0; qint64 first = 0, second = 0;
            for (int i = 0; i < 2000; ++i, now += 2000) { // 66 minutes of Instagram
                if (t.tick(now, Site::Instagram, true, {}) == Tracker::Event::Warn) {
                    ++warns;
                    if (warns == 1) first = now - 1000000;
                    if (warns == 2) second = now - 1000000;
                }
            }
            QCOMPARE(warns, 6); // at 15 min, then every 10 min
            QVERIFY(first >= 15 * 60000 && first < 15 * 60000 + 4000);
            QCOMPARE(second - first, qint64(10 * 60000));
            // watching without touching anything does not count; five minutes away starts over
            Tracker u; now = 1000000;
            for (int i = 0; i < 600; ++i, now += 2000) u.tick(now, Site::TikTok, false, {});
            QCOMPARE(u.totalMs(), qint64(0));
            for (int i = 0; i < 100; ++i, now += 2000) u.tick(now, Site::TikTok, true, {});
            QVERIFY(u.totalMs() > 190000);
            for (int i = 0; i < 160; ++i, now += 2000) u.tick(now, Site::None, true, {}); // 5+ minutes elsewhere
            QCOMPARE(u.totalMs(), qint64(0));
            // swiping to new videos warns before the minutes do
            Tracker v; now = 1000000; int w = 0;
            for (int i = 0; i < 40 && w == 0; ++i, now += 8000)
                w += v.tick(now, Site::Shorts, true, QStringLiteral("clip%1").arg(i)) == Tracker::Event::Warn;
            QCOMPARE(w, 1);
            QCOMPARE(v.swipes(), 30);
            QCOMPARE(v.mainSite(), Site::Shorts);
            // muted for the day, and snoozed
            Tracker m; m.limitMs = 4000; now = 1000000;
            m.muteFor(now, 3600000);
            for (int i = 0; i < 20; ++i, now += 2000) QVERIFY(m.tick(now, Site::TikTok, true, {}) == Tracker::Event::None);
        }
        {
            using namespace UpdateModel;
            QCOMPARE(parseVersion(QStringLiteral("v0.11.0")), (QList<int>{0, 11, 0}));
            QCOMPARE(parseVersion(QStringLiteral("0.12.0-beta.1")), (QList<int>{0, 12, 0}));
            QCOMPARE(parseVersion(QStringLiteral(" 1.2 ")), (QList<int>{1, 2}));
            QVERIFY(parseVersion(QStringLiteral("latest")).isEmpty());
            QVERIFY(parseVersion({}).isEmpty());
            QVERIFY(isNewer(QStringLiteral("v0.11.0"), QStringLiteral("0.10.0")));
            QVERIFY(isNewer(QStringLiteral("0.10.0"), QStringLiteral("0.9.3")));        // numbers, not text
            QVERIFY(!isNewer(QStringLiteral("0.9.3"), QStringLiteral("0.10.0")));
            QVERIFY(!isNewer(QStringLiteral("0.11"), QStringLiteral("0.11.0")));        // a missing part is 0
            QVERIFY(isNewer(QStringLiteral("0.11.1"), QStringLiteral("0.11")));
            QVERIFY(!isNewer(QStringLiteral("0.11.0"), QStringLiteral("0.11.0")));
            QVERIFY(!isNewer(QStringLiteral("nightly"), QStringLiteral("0.11.0")));      // garbage is never newer
            QVERIFY(!isNewer(QStringLiteral("0.12.0"), QStringLiteral("dev")));
            const auto asset = [](const QString &n) { return QJsonObject{{"name", n}, {"browser_download_url", QStringLiteral("https://github.com/o/r/releases/download/v0.12.0/") + n}}; };
            const QJsonObject body{{"tag_name", "v0.12.0"}, {"body", " What is new \n- one "}, {"html_url", "https://github.com/o/r/releases/tag/v0.12.0"}, {"draft", false}, {"prerelease", false},
                                   {"assets", QJsonArray{asset("notch_0.12.0_amd64.deb"), asset("Notch-0.12.0-windows-x64-setup.exe"), asset("Notch-0.12.0-windows-x64-portable.zip")}}};
            const QByteArray json = QJsonDocument(body).toJson();
            const Release r = parseRelease(json);
            QVERIFY(r.valid());
            QCOMPARE(r.version(), QStringLiteral("0.12.0"));
            QCOMPARE(r.notes.left(9), QStringLiteral("What is n"));
            QVERIFY(pickAsset(r.assets, QStringLiteral("linux"), false).endsWith(QLatin1String("amd64.deb")));
            QVERIFY(pickAsset(r.assets, QStringLiteral("windows"), false).endsWith(QLatin1String("setup.exe")));
            QVERIFY(pickAsset(r.assets, QStringLiteral("windows"), true).endsWith(QLatin1String("portable.zip")));
            QVERIFY(pickAsset({}, QStringLiteral("linux"), false).isEmpty());
            QVERIFY(!parseRelease(QJsonDocument(QJsonObject{{"tag_name", "v9.0.0"}, {"prerelease", true}}).toJson()).valid());
            QVERIFY(!parseRelease(QJsonDocument(QJsonObject{{"tag_name", "v9.0.0"}, {"draft", true}}).toJson()).valid());
            QVERIFY(!parseRelease("not json").valid());
            QVERIFY(!parseRelease(QJsonDocument(QJsonObject{{"message", "API rate limit exceeded"}}).toJson()).valid());
        }
        QCOMPARE(LauncherModel::groupOf({QStringLiteral("Network"), QStringLiteral("WebBrowser")}), QStringLiteral("Internet"));
        QCOMPARE(LauncherModel::groupOf({QStringLiteral("Utility"), QStringLiteral("Game")}), QStringLiteral("Games"));
        QCOMPARE(LauncherModel::groupOf({QStringLiteral("Qt")}), QStringLiteral("Other"));
        QVERIFY(LauncherModel::score(QStringLiteral("fire"), QStringLiteral("Firefox"), {}) > LauncherModel::score(QStringLiteral("fox"), QStringLiteral("Firefox"), {}));
        QVERIFY(LauncherModel::score(QStringLiteral("web"), QStringLiteral("Firefox"), QStringLiteral("browser web")) > 0);
        QVERIFY(LauncherModel::score(QStringLiteral("ffx"), QStringLiteral("Firefox"), {}) > 0);
        QCOMPARE(LauncherModel::score(QStringLiteral("zzz"), QStringLiteral("Firefox"), {}), 0);
        {
            const QVariantList apps{QVariantMap{{"id", "a"}, {"name", "Konsole"}, {"category", "System"}}, QVariantMap{{"id", "b"}, {"name", "Kate"}, {"category", "Development"}},
                                    QVariantMap{{"id", "c"}, {"name", "Okular Kate Viewer"}, {"category", "Office"}}};
            QCOMPARE(LauncherModel::search(apps, {}, {}, {}).first().toMap().value("name").toString(), QStringLiteral("Kate")); // A-Z
            QCOMPARE(LauncherModel::search(apps, QStringLiteral("kate"), {}, {}).first().toMap().value("id").toString(), QStringLiteral("b")); // name first
            QCOMPARE(LauncherModel::search(apps, QStringLiteral("kate"), {}, {}).size(), 2);
            QCOMPARE(LauncherModel::search(apps, {}, QStringLiteral("System"), {}).size(), 1);
            QCOMPARE(LauncherModel::search(apps, QStringLiteral("ko"), {}, {{"c", 9}}).first().toMap().value("id").toString(), QStringLiteral("a"));
        }
        QCOMPARE(PanelModel::channelOf(2437), 6);
        QCOMPARE(PanelModel::channelOf(5745), 149);
        QCOMPARE(PanelModel::channelOf(5955), 1);
        QCOMPARE(PanelModel::channelOf(1000), 0);
        QCOMPARE(PanelModel::bandText(5745), QStringLiteral("5 GHz"));
        QCOMPARE(PanelModel::bandText(2412), QStringLiteral("2.4 GHz"));
        QCOMPARE(PanelModel::securityText(1, 0, 0x108), QStringLiteral("WPA2-PSK"));
        QCOMPARE(PanelModel::securityText(1, 0, 0x500), QStringLiteral("WPA2/WPA3"));
        QCOMPARE(PanelModel::securityText(1, 0, 0x200), QStringLiteral("WPA2-Enterprise"));
        QCOMPARE(PanelModel::securityText(1, 0, 0), QStringLiteral("WEP"));
        QCOMPARE(PanelModel::securityText(0, 0, 0), QStringLiteral("Open"));
        QCOMPARE(PanelModel::wifiQrPayload(QStringLiteral("olyzano room"), QStringLiteral("olzm0404"), true), QStringLiteral("WIFI:T:WPA;S:olyzano room;P:olzm0404;;"));
        QCOMPARE(PanelModel::wifiQrPayload(QStringLiteral("a;b"), QStringLiteral("p:\\\"x"), true), QStringLiteral("WIFI:T:WPA;S:a\\;b;P:p\\:\\\\\\\"x;;"));
        QCOMPARE(PanelModel::wifiQrPayload(QStringLiteral("cafe"), {}, false), QStringLiteral("WIFI:T:nopass;S:cafe;;"));
        QCOMPARE(PanelModel::wifiBars(0), 0);
        QCOMPARE(PanelModel::wifiBars(20), 1);
        QCOMPARE(PanelModel::wifiBars(49), 2);
        QCOMPARE(PanelModel::wifiBars(74), 3);
        QCOMPARE(PanelModel::wifiBars(75), 4);
        QCOMPARE(PanelModel::volumeLevel(80, true), 0);
        QCOMPARE(PanelModel::volumeLevel(10, false), 1);
        QCOMPARE(PanelModel::volumeLevel(50, false), 2);
        QCOMPARE(PanelModel::volumeLevel(100, false), 3);
        QCOMPARE(PanelModel::timeLeft(0), QString());
        QCOMPARE(PanelModel::timeLeft(35 * 60), QStringLiteral("35 min"));
        QCOMPARE(PanelModel::timeLeft(7500), QStringLiteral("2 h 05 min"));
    }

    void appMenuIsReadFromDbusmenu()
    {
        const auto node = [](int id, const QVariantMap &props, const QVariantList &kids = {}) {
            return QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("props"), props}, {QStringLiteral("children"), kids}};
        };
        const QVariantMap sep{{QStringLiteral("type"), QStringLiteral("separator")}};
        const QVariantMap root = node(0, {}, {
            node(13, {{QStringLiteral("label"), QStringLiteral("_File")}, {QStringLiteral("children-display"), QStringLiteral("submenu")}}, {
                node(1, {{QStringLiteral("type"), QStringLiteral("separator")}}),   // leading separator: dropped
                node(2, {{QStringLiteral("label"), QStringLiteral("New _tab")},
                         {QStringLiteral("shortcut"), QVariantList{QStringList{QStringLiteral("Control"), QStringLiteral("t")}}}}),
                node(3, sep), node(4, sep),                                          // doubled: one stays
                node(5, {{QStringLiteral("label"), QStringLiteral("_Stop")}, {QStringLiteral("enabled"), false}}),
                node(6, {{QStringLiteral("label"), QStringLiteral("Secret")}, {QStringLiteral("visible"), false}}),
                node(7, {{QStringLiteral("label"), QStringLiteral("Show _bar")}, {QStringLiteral("toggle-type"), QStringLiteral("checkmark")},
                         {QStringLiteral("toggle-state"), 1}, {QStringLiteral("shortcut"), QVariantList{QStringList{QStringLiteral("Control"), QStringLiteral("Shift"), QStringLiteral("b")}}}}),
                node(8, sep)}),                                                      // trailing separator: dropped
            node(21, {{QStringLiteral("label"), QStringLiteral("_Tools")}, {QStringLiteral("children-display"), QStringLiteral("submenu")}}, {
                node(22, {{QStringLiteral("label"), QStringLiteral("More")}, {QStringLiteral("children-display"), QStringLiteral("submenu")}},
                     {node(23, {{QStringLiteral("label"), QStringLiteral("A__B")}})})})});
        const QVariantList bar = DbusMenuModel::menuBar(root);
        QCOMPARE(bar.size(), 2);
        QCOMPARE(bar[0].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("File")); // the mnemonic marker is gone
        const QVariantList file = bar[0].toMap().value(QStringLiteral("children")).toList();
        QCOMPARE(file.size(), 4); // tab, separator, Stop, Show bar: no hidden item, no stray separators
        QCOMPARE(file[0].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("New tab"));
        QCOMPARE(file[0].toMap().value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+T"));
        QVERIFY(file[1].toMap().value(QStringLiteral("separator")).toBool());
        QVERIFY(!file[2].toMap().value(QStringLiteral("enabled")).toBool());
        QVERIFY(file[3].toMap().value(QStringLiteral("check")).toBool());
        QCOMPARE(file[3].toMap().value(QStringLiteral("shortcut")).toString(), QStringLiteral("Ctrl+Shift+B"));
        const QVariantMap more = bar[1].toMap().value(QStringLiteral("children")).toList()[0].toMap();
        QVERIFY(more.value(QStringLiteral("submenu")).toBool());
        QCOMPARE(more.value(QStringLiteral("children")).toList()[0].toMap().value(QStringLiteral("label")).toString(), QStringLiteral("A_B"));
        QCOMPARE(DbusMenuModel::shortcutText(QVariant()), QString());
        QCOMPARE(DbusMenuModel::shortcutText(QVariantList{QStringList{QStringLiteral("Shift"), QString(QChar(0x1b))}}), QStringLiteral("Shift+Esc"));
    }

    void karaokeLoopModules()
    {
        const QStringList args = KaraokeModel::loadArgs(25);
        QCOMPARE(args.value(1), QStringLiteral("module-loopback"));
        QVERIFY(args.contains(QStringLiteral("latency_msec=25")));
        QVERIFY(args.last().contains(QStringLiteral("OlyzanoKaraoke")));
        QCOMPARE(KaraokeModel::moduleId(QStringLiteral("42\n")), 42);
        QCOMPARE(KaraokeModel::moduleId(QStringLiteral("Failure: Module initialization failed")), -1);
        const QString list = QStringLiteral("1\tmodule-device-restore\t\n"
                                            "27\tmodule-loopback\tsource=a sink=b source_output_properties=application.name=OlyzanoKaraoke\n"
                                            "28\tmodule-loopback\tsource=x sink=y\n"
                                            "31\tmodule-loopback\tsource_output_properties=application.name=OlyzanoKaraoke latency_msec=30\n");
        QCOMPARE(KaraokeModel::staleModules(list), QList<int>({27, 31})); // another loopback of the user's stays
    }

    void lyricsQueriesAreCleaned()
    {
        QCOMPARE(LyricsQuery::cleanArtist(QStringLiteral("MAZZEL - Topic")), QStringLiteral("MAZZEL"));
        QCOMPARE(LyricsQuery::cleanArtist(QStringLiteral("YOASOBIVEVO")), QStringLiteral("YOASOBI"));
        QCOMPARE(LyricsQuery::cleanArtist(QStringLiteral("Main, Guest")), QStringLiteral("Main"));
        QCOMPARE(LyricsQuery::cleanTitle(QStringLiteral("Touch (Official Video) [MV]"), QStringLiteral("MAZZEL")), QStringLiteral("Touch"));
        QCOMPARE(LyricsQuery::cleanTitle(QStringLiteral("MAZZEL - Touch"), QStringLiteral("MAZZEL")), QStringLiteral("Touch"));
        QCOMPARE(LyricsQuery::cleanTitle(QStringLiteral("Song (feat. Someone)"), QStringLiteral("A")), QStringLiteral("Song (feat. Someone)")); // kept
        QCOMPARE(LyricsQuery::cleanTitle(QStringLiteral("Tune [Live at the Hall]"), {}), QStringLiteral("Tune"));
        QVERIFY(LyricsQuery::sameArtist(QStringLiteral("ADO"), QStringLiteral("Ado")));
        QVERIFY(LyricsQuery::sameArtist(QStringLiteral("Mr.Children"), QStringLiteral("Mr. Children")));
        QVERIFY(!LyricsQuery::sameArtist(QStringLiteral("Ado"), QStringLiteral("Someone")));
        QVERIFY(!LyricsQuery::sameArtist({}, QStringLiteral("Ado")));
    }

    void lyricsSearchByWordsKeepsTheRightArtist()
    {
        FakeLyricsServer web;
        web.routes.insert(QStringLiteral("/api/get"), {404, "{}"});
        web.routes.insert(QStringLiteral("/api/search"), {200, "[]"}); // by artist and title: nothing
        web.routes.insert(QStringLiteral("/api/search?q"), {200, R"([{"artistName":"Other Band","duration":200,"syncedLyrics":"[00:01.00] wrong"},)"
                                                                 R"({"artistName":"The Band & Friends","duration":205,"syncedLyrics":"[00:01.00] right"}])"});
        Lyrics l(m_tmp.filePath(QStringLiteral("lyrics-c.conf")), m_tmp.filePath(QStringLiteral("lyrics-cache-c")));
        l.setBaseUrl(web.url());
        l.setEnabled(true);
        l.track(QStringLiteral("The Band - Topic"), QStringLiteral("Tune (Official Video)"), QString(), 200000);
        QTRY_COMPARE(l.state(), QStringLiteral("synced"));
        QCOMPARE(web.hits, 3); // exact, search, search by words
        l.setPosition(1200);
        QCOMPARE(l.currentText(), QStringLiteral("right"));
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

    void smoothedLevels()
    {
        // Rises faster than it falls, and never overshoots the target.
        const double up = smoothLevel(0.0, 1.0, 33);
        const double down = 1.0 - smoothLevel(1.0, 0.0, 33);
        QVERIFY(up > down);
        QVERIFY(up < 1.0 && up > 0.0);
        // Falling is monotonic towards the target.
        double v = 1.0;
        for (int i = 0; i < 20; ++i) {
            const double next = smoothLevel(v, 0.0, 33);
            QVERIFY(next < v);
            v = next;
        }
        // One long step equals the same time in short steps: uneven audio updates don't change the motion.
        double stepped = 0.0;
        for (int i = 0; i < 10; ++i)
            stepped = smoothLevel(stepped, 0.8, 10);
        QVERIFY(std::abs(stepped - smoothLevel(0.0, 0.8, 100)) < 1e-9);
        // A +-5 % flicker in the input barely moves the bar once it has settled.
        double bar = 0.5;
        for (int i = 0; i < 60; ++i)
            bar = smoothLevel(bar, i % 2 ? 0.55 : 0.45, 33);
        double lo = 1, hi = 0;
        for (int i = 0; i < 20; ++i) {
            bar = smoothLevel(bar, i % 2 ? 0.55 : 0.45, 33);
            lo = std::min(lo, bar);
            hi = std::max(hi, bar);
        }
        QVERIFY(hi - lo < 0.04);
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
