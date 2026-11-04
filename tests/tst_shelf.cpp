#include "app/NotchController.h"
#include "services/GuiImageProvider.h"

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QtTest>

// The shelf with a lot on it: removing, restoring and searching must not freeze the panel.
class TestShelf : public QObject
{
    Q_OBJECT

private slots:
    // A shelf with thousands of files must stay responsive: removing everything, and the first search, in small steps.
    void bigShelfStaysResponsive()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        NotchController shelf(dir.path());
        QVERIFY(shelf.start());
        constexpr int N = 1500;
        const QString filler = QString("lorem ipsum dolor sit amet ").repeated(1500); // about 40 KB a note
        for (int i = 0; i < N; ++i)
            shelf.addTextNote(QStringLiteral("note %1 ").arg(i) + filler + QStringLiteral(" needle%1").arg(i % 50));
        QTRY_COMPARE_WITH_TIMEOUT(shelf.count(), N, 120000);

        auto rssMb = [] { QFile f("/proc/self/status"); f.open(QIODevice::ReadOnly); const QByteArray all = f.readAll(); const int at = all.indexOf("VmRSS:"); return at < 0 ? 0 : all.mid(at + 6, 12).simplified().split(' ').first().toInt() / 1024; }; // Linux only
        const int rssBefore = rssMb();
        QElapsedTimer clock;
        clock.start();
        shelf.setFilter(QStringLiteral("needle7"));
        const qint64 firstSearchMs = clock.elapsed();
        // the index fills in the background: once it has everything, exactly the notes with that word show (1500 / 50 = 30)
        QTRY_COMPARE_WITH_TIMEOUT(shelf.items()->rowCount(), N / 50, 60000);
        QCoreApplication::processEvents();
        qInfo() << "BENCH search rss MB before" << rssBefore << "after" << rssMb();
        shelf.setFilter(QString());

        // the longest stretch the event loop was not given back during "Clear all"
        qint64 longest = 0, last = 0;
        QElapsedTimer tick;
        tick.start();
        QTimer probe;
        probe.setInterval(5);
        connect(&probe, &QTimer::timeout, this, [&] { longest = qMax(longest, tick.elapsed() - last); last = tick.elapsed(); });
        probe.start();
        clock.restart();
        shelf.removeItems(shelf.visibleIds());
        QTRY_COMPARE_WITH_TIMEOUT(shelf.removedCount(), N, 120000);
        const qint64 clearMs = clock.elapsed();
        longest = qMax(longest, tick.elapsed() - last);
        qInfo() << "BENCH shelf files" << N << "first search ms" << firstSearchMs << "clear all ms" << clearMs << "longest UI stall ms" << longest;
        clock.restart();
        shelf.emptyRecentlyRemoved();
        QTRY_COMPARE_WITH_TIMEOUT(shelf.removedCount(), 0, 120000);
        qInfo() << "BENCH delete forever ms" << clock.elapsed();
    }

    void batchesKeepOneUndoAndRefuseOverlap()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        NotchController shelf(dir.path());
        QVERIFY(shelf.start());
        for (int i = 0; i < 25; ++i)
            shelf.addTextNote(QStringLiteral("note %1").arg(i));
        QTRY_COMPARE_WITH_TIMEOUT(shelf.count(), 25, 30000);
        const QStringList ids = shelf.visibleIds();
        QCOMPARE(ids.size(), 25);

        // a short list is done at once, without the steps
        shelf.removeItems(ids.mid(0, 3));
        QCOMPARE(shelf.removedCount(), 3);
        QCOMPARE(shelf.batchLabel(), QString());
        QCOMPARE(shelf.undoCount(), 3);
        shelf.undoRemove();
        QCOMPARE(shelf.removedCount(), 0);

        // a long one runs in steps: a label while it runs, a second job refused, one Undo for all of it at the end
        shelf.removeItems(ids);
        QVERIFY(!shelf.batchLabel().isEmpty());
        shelf.removeItems(ids.mid(0, 2)); // refused: one job at a time
        QVERIFY(!shelf.errorMessage().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(shelf.removedCount(), 25, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(shelf.batchLabel().isEmpty(), 10000);
        QCOMPARE(shelf.count(), 0);
        QCOMPARE(shelf.undoCount(), 25);
        shelf.undoRemove();
        QTRY_COMPARE_WITH_TIMEOUT(shelf.count(), 25, 10000);
        QCOMPARE(shelf.undoCount(), 0);

        // for good: every row and file goes
        shelf.removeItems(ids);
        QTRY_COMPARE_WITH_TIMEOUT(shelf.removedCount(), 25, 10000);
        shelf.emptyRecentlyRemoved();
        QTRY_COMPARE_WITH_TIMEOUT(shelf.removedCount(), 0, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(shelf.batchLabel().isEmpty(), 10000);
        QCOMPARE(shelf.count(), 0);
    }

    // Keep: a big selection is kept in steps, kept files go last in "Expiring first", and releasing them puts them back.
    void keepWorksInBatchesAndSortsLast()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        NotchController shelf(dir.path());
        QVERIFY(shelf.start());
        for (int i = 0; i < 25; ++i)
            shelf.addTextNote(QStringLiteral("keep %1").arg(i));
        QTRY_COMPARE_WITH_TIMEOUT(shelf.count(), 25, 30000);
        const QStringList ids = shelf.visibleIds();
        const auto kept = [&] {
            int n = 0;
            for (int r = 0; r < shelf.items()->rowCount(); ++r)
                n += shelf.items()->index(r, 0).data(FileItemModel::PinnedRole).toBool();
            return n;
        };
        shelf.setPinned(ids, true);
        QVERIFY(!shelf.batchLabel().isEmpty()); // 25 > one step
        QTRY_COMPARE_WITH_TIMEOUT(kept(), 25, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(shelf.batchLabel().isEmpty(), 10000);
        shelf.setPinned(ids.mid(0, 20), false);
        QTRY_COMPARE_WITH_TIMEOUT(kept(), 5, 10000);

        shelf.setSortMode(QStringLiteral("expiring"));
        for (int r = 0; r < 20; ++r)
            QVERIFY(!shelf.items()->index(r, 0).data(FileItemModel::PinnedRole).toBool());
        for (int r = 20; r < 25; ++r)
            QVERIFY(shelf.items()->index(r, 0).data(FileItemModel::PinnedRole).toBool());
        QCOMPARE(shelf.items()->index(24, 0).data(FileItemModel::ExpiresTextRole).toString(), QStringLiteral("kept, never expires"));
    }

    // Icons from the theme are made on the GUI thread, whichever thread asks (a race with it crashed Notch in KIconLoader).
    void iconProviderMakesImagesOnTheGuiThread()
    {
        QThread *madeIn = nullptr;
        GuiImageProvider provider([&madeIn](const QString &, const QSize &size) {
            madeIn = QThread::currentThread();
            QImage image(size, QImage::Format_ARGB32);
            image.fill(Qt::red);
            return image;
        }, QSize(32, 32));

        std::unique_ptr<QQuickImageResponse> a(provider.requestImageResponse(QStringLiteral("a"), QSize(20, 10)));
        QSignalSpy spyA(a.get(), &QQuickImageResponse::finished);
        QTRY_COMPARE(spyA.count(), 1);
        std::unique_ptr<QQuickTextureFactory> factoryA(a->textureFactory());
        QCOMPARE(factoryA->textureSize(), QSize(20, 10));
        QCOMPARE(madeIn, QThread::currentThread());

        // asked from another thread (the image reader's): still made here, and the size falls back
        madeIn = nullptr;
        QQuickImageResponse *b = nullptr;
        QThread *worker = QThread::create([&] { b = provider.requestImageResponse(QStringLiteral("b"), QSize()); });
        worker->start();
        QVERIFY(worker->wait(5000));
        QVERIFY(b);
        QSignalSpy spyB(b, &QQuickImageResponse::finished);
        QTRY_COMPARE_WITH_TIMEOUT(spyB.count(), 1, 5000);
        std::unique_ptr<QQuickTextureFactory> factoryB(b->textureFactory());
        QCOMPARE(factoryB->textureSize(), QSize(32, 32));
        QCOMPARE(madeIn, QCoreApplication::instance()->thread());
        delete b;
        delete worker;

        // dropped before it finished: nothing is touched afterwards
        { std::unique_ptr<QQuickImageResponse> c(provider.requestImageResponse(QStringLiteral("c"), QSize(8, 8))); }
        QTest::qWait(50);
    }

    // The real thing: a QML Image asking for image://t/... is served through the provider and ends up Ready, at the size asked.
    void qmlImageIsServedByTheProvider()
    {
        QQmlEngine engine;
        QThread *madeIn = nullptr;
        engine.addImageProvider(QStringLiteral("t"), new GuiImageProvider([&madeIn](const QString &, const QSize &size) {
            madeIn = QThread::currentThread();
            QImage image(size, QImage::Format_ARGB32);
            image.fill(Qt::blue);
            return image;
        }, QSize(32, 32)));
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nImage { source: \"image://t/anything\"; sourceSize: Qt.size(20, 10); asynchronous: false }", QUrl());
        std::unique_ptr<QObject> image(component.create());
        QVERIFY2(image, qPrintable(component.errorString()));
        QTRY_COMPARE_WITH_TIMEOUT(image->property("status").toInt(), 1, 5000); // Image.Ready
        QCOMPARE(image->property("implicitWidth").toInt(), 20);
        QCOMPARE(madeIn, QCoreApplication::instance()->thread());
    }
};

QTEST_MAIN(TestShelf)
#include "tst_shelf.moc"
