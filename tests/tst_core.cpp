#include "repositories/FileRepository.h"
#include "services/CleanupService.h"
#include "services/FileService.h"
#include "services/FolderWatcher.h"
#include "platform/BatteryThreshold.h"
#include "platform/PlayerSelection.h"
#include "platform/AudioBands.h"

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <unistd.h>

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
        QCOMPARE(q.value(0).toInt(), 3);
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
        if (::geteuid() != 0)
            QVERIFY(!FileService::validateSource(locked).isEmpty());
        QVERIFY(FileService::validateSource(writeSource(QStringLiteral("ok.txt"), "x")).isEmpty());
    }

    void failedCopyLeavesNothing()
    {
        if (::geteuid() == 0)
            QSKIP("root ignores permissions");
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

    void removeManagedGuards()
    {
        QVERIFY(!FileService::removeManaged(m_files, QStringLiteral("../escape")));
        QVERIFY(!FileService::removeManaged(m_files, QStringLiteral("not-a-uuid/x")));
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(!FileService::removeManaged(m_files, id + QStringLiteral("/..")));
        QVERIFY(!FileService::removeManaged(m_files, id + QStringLiteral("/a/b")));

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
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
