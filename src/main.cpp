#include "app/NotchController.h"
#include "app/NearbyController.h"
#include "app/ShareController.h"
#include "services/QrImage.h"
#include "platform/DesktopIntegration.h"
#include "platform/Activities.h"
#include "platform/AppDock.h"
#include "platform/Wellbeing.h"
#include "platform/AudioLevels.h"
#include "platform/ColorPicker.h"
#include "platform/DesktopNotifications.h"
#include "platform/Sounds.h"
#include "platform/KdeConnect.h"
#include "platform/Theme.h"
#include "platform/Lyrics.h"
#include "platform/MediaPlayers.h"
#include "platform/SystemEvents.h"

#include <QDir>
#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QImageReader>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QScreen>
#include <QSocketNotifier>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

// image://mime/<icon-name> -> themed icon for a MIME type.
class MimeIconProvider : public QQuickImageProvider
{
public:
    MimeIconProvider() : QQuickImageProvider(Pixmap) {}
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requested) override
    {
        const QSize s = requested.isValid() ? requested : QSize(32, 32);
        if (size)
            *size = s;
        // Windows has no icon theme: device icons named after a bundled one (bluetooth, headphones) use that.
        const QString bundled = QStringLiteral(":/qt/qml/Notch/src/qml/icons/%1.svg").arg(id);
        if (!QIcon::hasThemeIcon(id) && QFile::exists(bundled))
            return QIcon(bundled).pixmap(s);
        return QIcon::fromTheme(id, QIcon::fromTheme(QStringLiteral("text-x-generic"))).pixmap(s);
    }
};

// image://thumb/<percent-encoded file url> -> the picture scaled down to fit 96 px, for the drag pixmap
// (the drag used the full-size file, so a photo dragged out of the shelf came out huge).
class ThumbImageProvider : public QQuickImageProvider
{
public:
    ThumbImageProvider() : QQuickImageProvider(Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override
    {
        QString path = QUrl(id).toLocalFile();
        if (path.isEmpty() || !QFile::exists(path))
            path = QUrl(QUrl::fromPercentEncoding(id.toUtf8())).toLocalFile();
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QSize s = reader.size();
        if (s.isValid())
            reader.setScaledSize(s.scaled(96, 96, Qt::KeepAspectRatio));
        const QImage image = reader.read();
        if (size)
            *size = image.size();
        return image;
    }
};

#ifdef Q_OS_UNIX
// SIGTERM/SIGINT/SIGHUP (logout, `kill`, Ctrl+C) quit through the event loop, so aboutToQuit
// runs and locked categories are unmounted. Self-pipe: the handler itself only write()s.
static int quitPipe[2];

static void quitOnSignals(QCoreApplication &app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, quitPipe) != 0)
        return;
    auto *notifier = new QSocketNotifier(quitPipe[1], QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
    for (int sig : {SIGTERM, SIGINT, SIGHUP})
        std::signal(sig, [](int) { if (::write(quitPipe[0], "q", 1) < 0) {} });
}
#else
static void quitOnSignals(QCoreApplication &) {} // Windows: a closing session already ends the event loop
#endif

// image://qr/<base64url text> -> QR code (white background, so it scans in dark mode too).
class QrImageProvider : public QQuickImageProvider
{
public:
    QrImageProvider() : QQuickImageProvider(Pixmap) {}
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requested) override
    {
        const QString text = QString::fromUtf8(QByteArray::fromBase64(id.toLatin1(), QByteArray::Base64UrlEncoding));
        QImage image;
        try {
            image = renderQr(text, requested.width() > 0 ? requested.width() : 220);
        } catch (...) { // text too long for a QR code
        }
        if (size)
            *size = image.size();
        return QPixmap::fromImage(image);
    }
};

int main(int argc, char *argv[])
{
    QQuickWindow::setDefaultAlphaBuffer(true);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("notch"));
    app.setDesktopFileName(QStringLiteral("notch"));
    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/Notch/src/qml/icons/notch.png")));
    app.setApplicationVersion(QStringLiteral(NOTCH_VERSION));
    quitOnSignals(app);

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                            + QStringLiteral("/notch");
    QDir().mkpath(dataDir);
    QLockFile lock(dataDir + QStringLiteral("/notch.lock"));
    if (!lock.tryLock(100)) {
        qWarning("Notch is already running");
        return 0;
    }

    NotchController controller(dataDir);
    controller.start(); // failures surface in the UI via errorMessage
    ShareController share(&controller);
    NearbyController nearby(&controller);

    DesktopIntegration desktop;
    SystemEvents systemEvents;
    MediaPlayers media;
    AudioLevels audio;
    KdeConnect kdeconnect;
    Theme theme;
    ColorPicker colorPicker;
    AppDock dock;
    Activities activities;
    Wellbeing wellbeing;
    Sounds sounds;
    Lyrics lyrics;
    QObject::connect(&media, &MediaPlayers::changed, &lyrics, [&] {
        lyrics.track(media.active() ? media.artist() : QString(), media.active() ? media.title() : QString(), media.album(), media.lengthMs());
    });
    DesktopNotifications desktopNotifications;
    QObject::connect(&colorPicker, &ColorPicker::picked, &controller, &NotchController::addColor);

    // Sound effects: wired here, once, rather than in each screen's notch.
    sounds.setBusyChecks([&] { return !activities.micApps().isEmpty(); }, [&] { return dock.fullScreenActive(); });
    auto play = [&](const char *name) { return [&sounds, name] { sounds.play(QString::fromLatin1(name)); }; };
    QObject::connect(&controller, &NotchController::sound, &sounds, &Sounds::play);
    QObject::connect(&colorPicker, &ColorPicker::picked, &sounds, play("pick"));
    QObject::connect(&dock, &AppDock::trashEmptied, &sounds, play("trash"));
    QObject::connect(&activities, &Activities::timerFinished, &sounds, play("timer"));
    QObject::connect(&wellbeing, &Wellbeing::reminder, &sounds, play("stretch"));
    QObject::connect(&wellbeing, &Wellbeing::eyeBreak, &sounds, play("stretch"));
    QObject::connect(&wellbeing, &Wellbeing::breakOver, &sounds, play("break-over"));
    QObject::connect(&nearby, &NearbyController::incomingRequest, &sounds, play("notify"));
    QObject::connect(&nearby, &NearbyController::transferDone, &sounds, [&](const QString &direction) {
        if (direction == QLatin1String("out"))
            sounds.play(QStringLiteral("send"));
    });
    QObject::connect(&kdeconnect, &KdeConnect::sent, &sounds, [&](int, const QString &, const QString &error) {
        if (error.isEmpty())
            sounds.play(QStringLiteral("send"));
    });
    QObject::connect(&kdeconnect, &KdeConnect::phoneCall, &sounds, [&](const QString &event) {
        if (event == QLatin1String("ringing") && activities.phoneAlerts())
            sounds.playRing();
        else
            sounds.stopRing();
        if (event == QLatin1String("missedCall") && activities.phoneAlerts())
            sounds.play(QStringLiteral("notify"));
    });
    QObject::connect(&kdeconnect, &KdeConnect::phoneNotification, &sounds, [&] {
        if (activities.phoneAlerts())
            sounds.play(QStringLiteral("notify"));
    });
    QObject::connect(&desktopNotifications, &DesktopNotifications::notification, &sounds, play("notify"));
    QObject::connect(&systemEvents, &SystemEvents::alert, &sounds, [&](const QString &kind, const QString &title) {
        if (!controller.alerts())
            return;
        // Plugged in: "charging" titled "Charging"; unplugged: "battery" on Linux, "charging" titled "On battery" on Windows.
        if (kind == QLatin1String("charging") || kind == QLatin1String("battery"))
            sounds.play(kind == QLatin1String("charging") && title == SystemEvents::tr("Charging") ? QStringLiteral("plug-in")
                                                                                                : QStringLiteral("plug-out"));
        else if (kind == QLatin1String("bluetooth"))
            sounds.play(QStringLiteral("device"));
        else if (kind == QLatin1String("lowbattery"))
            sounds.play(QStringLiteral("warning"));
    });
    theme.setMode(controller.themeMode());
    theme.setStyle(controller.uiStyle());
    QObject::connect(&controller, &NotchController::uiStyleChanged, &theme, [&] { theme.setStyle(controller.uiStyle()); });
    QObject::connect(&controller, &NotchController::themeModeChanged, &theme, [&] { theme.setMode(controller.themeMode()); });
    QObject::connect(&kdeconnect, &KdeConnect::received, &controller, &NotchController::addReceived);
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("mime"), new MimeIconProvider);
    engine.addImageProvider(QStringLiteral("qr"), new QrImageProvider);
    engine.addImageProvider(QStringLiteral("thumb"), new ThumbImageProvider);
    engine.addImageProvider(QStringLiteral("appicon"), new DockIconProvider(&dock));
    engine.rootContext()->setContextProperty(QStringLiteral("share"), &share);
    engine.rootContext()->setContextProperty(QStringLiteral("nearby"), &nearby);
    engine.rootContext()->setContextProperty(QStringLiteral("notch"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("desktop"), &desktop);
    engine.rootContext()->setContextProperty(QStringLiteral("systemEvents"), &systemEvents);
    engine.rootContext()->setContextProperty(QStringLiteral("media"), &media);
    engine.rootContext()->setContextProperty(QStringLiteral("audio"), &audio);
    engine.rootContext()->setContextProperty(QStringLiteral("kdeconnect"), &kdeconnect);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("colorPicker"), &colorPicker);
    engine.rootContext()->setContextProperty(QStringLiteral("dock"), &dock);
    engine.rootContext()->setContextProperty(QStringLiteral("activities"), &activities);
    engine.rootContext()->setContextProperty(QStringLiteral("wellbeing"), &wellbeing);
    engine.rootContext()->setContextProperty(QStringLiteral("sounds"), &sounds);
    engine.rootContext()->setContextProperty(QStringLiteral("lyrics"), &lyrics);
    engine.rootContext()->setContextProperty(QStringLiteral("desktopNotifications"), &desktopNotifications);

    // One Notch per monitor, following hotplug.
    QQmlComponent notchWindow(&engine, "Notch", "Main");
    QHash<QScreen *, QQuickWindow *> windows;
    // And one app dock per monitor on the edge chosen in Settings, shown while the dock is switched on.
    QQmlComponent dockWindow(&engine, "Notch", "Dock");
    QHash<QScreen *, QQuickWindow *> docks;
    auto addScreen = [&](QScreen *screen) {
        auto *window = qobject_cast<QQuickWindow *>(notchWindow.create());
        if (!window) {
            qCritical().noquote() << notchWindow.errorString();
            return;
        }
        window->setScreen(screen);
        desktop.attach(window);
        window->show();
        windows.insert(screen, window);

        auto *dockView = qobject_cast<QQuickWindow *>(dockWindow.create());
        if (!dockView) {
            qCritical().noquote() << dockWindow.errorString();
            return;
        }
        dockView->setScreen(screen);
        desktop.attachDock(dockView, dock.position());
        dockView->setVisible(dock.enabled());
        QObject::connect(&dock, &AppDock::enabledChanged, dockView, [dockView, &dock] { dockView->setVisible(dock.enabled()); });
        // Another edge: hide, re-anchor, show again (queued so the window has taken its new size from QML).
        QObject::connect(&dock, &AppDock::positionChanged, dockView, [dockView, &dock, &desktop] {
            dockView->hide();
            desktop.moveDock(dockView, dock.position());
            dockView->setVisible(dock.enabled());
        }, Qt::QueuedConnection);
        docks.insert(screen, dockView);
    };
    for (QScreen *screen : app.screens())
        addScreen(screen);
    if (windows.isEmpty())
        return 1;
    QObject::connect(&app, &QGuiApplication::screenAdded, addScreen);
    QObject::connect(&app, &QGuiApplication::screenRemoved, [&](QScreen *screen) {
        delete windows.take(screen);
        delete docks.take(screen);
    });

    engine.loadFromModule("Notch", "Tray");
    const int rc = app.exec();
    qDeleteAll(windows); // before the engine and controller they bind to
    qDeleteAll(docks);
    return rc;
}
