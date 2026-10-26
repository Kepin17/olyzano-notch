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
#include "platform/GamingMode.h"
#include "platform/AppMenu.h"
#include "platform/Doomscroll.h"
#include "platform/Tasks.h"
#include "platform/Updates.h"
#include "platform/Launcher.h"
#include "platform/TrayHost.h"
#include "platform/Karaoke.h"
#include "platform/PanelBar.h"
#include "platform/QuickSettings.h"
#include "platform/Lyrics.h"
#include "platform/MediaPlayers.h"
#include "platform/SystemEvents.h"

#ifdef Q_OS_WIN
#include "platform/WinAudioSessions.h"
#endif

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>
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

// image://tray/<item key>/<revision> -> the icon of a tray item.
class TrayIconProvider : public QQuickImageProvider
{
public:
    explicit TrayIconProvider(TrayHost *host) : QQuickImageProvider(Pixmap), m_host(host) {}
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requested) override
    {
        const QSize s = requested.isValid() ? requested : QSize(32, 32);
        if (size)
            *size = s;
        return m_host->pixmap(id.section(u'/', 0, 0), s);
    }
private:
    TrayHost *m_host;
};

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
    if (app.arguments().contains(QStringLiteral("--check-updates"))) { // works beside a running Notch, too // asks GitHub now and prints what it found (NOTCH_FAKE_VERSION=0.10.0 pretends to be older)
        Updates u;
        QTimer::singleShot(20000, &app, [&] { fputs("timed out\n", stdout); app.exit(1); });
        QObject::connect(&u, &Updates::checked, &app, [&] { fputs(u.report().constData(), stdout); app.quit(); });
        u.check();
        return app.exec();
    }
    quitOnSignals(app);

#ifdef Q_OS_WIN
    // `notch --dump-discord <file>`: for 90 s, write what Discord's audio sessions do (capture / render, active or
    // not, how loud) about every 200 ms, to see how mute, deafen and leaving a call show up. Runs beside a running Notch.
    if (const int i = app.arguments().indexOf(QStringLiteral("--dump-discord")); i >= 0 && i + 1 < app.arguments().size()) {
        auto *sessions = new WinAudioSessions;
        auto *out = new QFile(app.arguments().at(i + 1));
        if (!out->open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 1;
        auto *clock = new QElapsedTimer;
        clock->start();
        QTimer::singleShot(150000, &app, &QCoreApplication::quit);
        auto *tick = new QTimer(&app);
        QObject::connect(tick, &QTimer::timeout, &app, [=] {
            out->write(QByteArray::number(clock->elapsed() / 1000.0, 'f', 1) + "s " + sessions->dump() + '\n');
            out->flush();
        });
        tick->start(200);
        return app.exec();
    }
#endif

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
    Karaoke karaoke;
    GamingMode gaming;
    PanelBar panelBar;
    QuickSettings quick;
    AppMenu appMenu;
    TrayHost trayHost;
    Launcher launcher(&dock);
    Doomscroll doomscroll(&dock, &media, &wellbeing);
    Tasks tasks;
    Updates updates;
    if (const int i = app.arguments().indexOf(QStringLiteral("--dump-menu")); i >= 0 && i + 1 < app.arguments().size()) { // the menu of a process, for checking
        fputs(appMenu.dump(app.arguments().at(i + 1).toLongLong()).constData(), stdout);
        return 0;
    }
    QObject::connect(&dock, &AppDock::activeAppChanged, &appMenu, [&] { appMenu.setPid(dock.activePid()); });
    if (const int i = app.arguments().indexOf(QStringLiteral("--doom-debug")); i >= 0 && i + 1 < app.arguments().size()) { // `--doom-debug instagram 1200`: pretend to scroll it for 20 minutes, print what it says
        doomscroll.debugFeed(app.arguments().at(i + 1), i + 2 < app.arguments().size() ? app.arguments().at(i + 2).toInt() : 1200);
        printf("site=%s minutes=%d swipes=%d reminding=%d\n%s\n", qPrintable(doomscroll.site()), doomscroll.minutes(), doomscroll.swipes(), doomscroll.reminding(), qPrintable(doomscroll.message()));
        return 0;
    }
    if (app.arguments().contains(QStringLiteral("--dump-tray"))) { // the tray items it sees
        QTimer::singleShot(1500, &app, [&] { fputs(trayHost.dump().constData(), stdout); app.quit(); });
        return app.exec();
    }
    if (app.arguments().contains(QStringLiteral("--dump-quick"))) { // what Quick Settings sees, for checking it on a computer
        QTimer::singleShot(600, &app, [&] { fputs(quick.dump().constData(), stdout); app.quit(); });
        return app.exec();
    }
    // "Full screen" for gaming mode is any app that fills the screen: a maximised one looks the same.
    const auto fillsScreen = [&] { gaming.setFullScreen(dock.coveringActive()); };
    QObject::connect(&dock, &AppDock::coveringChanged, &gaming, fillsScreen);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &karaoke, &Karaoke::stop);
    QObject::connect(&media, &MediaPlayers::changed, &lyrics, [&] {
        lyrics.track(media.active() ? media.artist() : QString(), media.active() ? media.title() : QString(), media.album(), media.lengthMs());
    });
    DesktopNotifications desktopNotifications;
    QObject::connect(&colorPicker, &ColorPicker::picked, &controller, &NotchController::addColor);

    // Sound effects: wired here, once, rather than in each screen's notch.
    sounds.setSilencer([&] { return gaming.active(); });
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
    engine.addImageProvider(QStringLiteral("tray"), new TrayIconProvider(&trayHost));
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
    engine.rootContext()->setContextProperty(QStringLiteral("karaoke"), &karaoke);
    engine.rootContext()->setContextProperty(QStringLiteral("gaming"), &gaming);
    engine.rootContext()->setContextProperty(QStringLiteral("topbar"), &panelBar);
    engine.rootContext()->setContextProperty(QStringLiteral("quick"), &quick);
    engine.rootContext()->setContextProperty(QStringLiteral("appMenu"), &appMenu);
    engine.rootContext()->setContextProperty(QStringLiteral("trayHost"), &trayHost);
    engine.rootContext()->setContextProperty(QStringLiteral("launcher"), &launcher);
    engine.rootContext()->setContextProperty(QStringLiteral("doomscroll"), &doomscroll);
    engine.rootContext()->setContextProperty(QStringLiteral("updates"), &updates);
    engine.rootContext()->setContextProperty(QStringLiteral("tasks"), &tasks);
    engine.rootContext()->setContextProperty(QStringLiteral("desktopNotifications"), &desktopNotifications);

    // One Notch per monitor, following hotplug.
    QQmlComponent notchWindow(&engine, "Notch", "Main");
    QHash<QScreen *, QQuickWindow *> windows;
    // And one app dock per monitor on the edge chosen in Settings, shown while the dock is switched on.
    QQmlComponent dockWindow(&engine, "Notch", "Dock");
    QHash<QScreen *, QQuickWindow *> docks;
    // And a status bar along the top of each monitor (Settings → Panel). Created before the notch, so the notch draws above it.
    QQmlComponent barWindow(&engine, "Notch", "Bar");
    QQmlComponent edgeWindow(&engine, "Notch", "EdgeStrip");
    QHash<QScreen *, QQuickWindow *> bars;
    if (const int i = app.arguments().indexOf(QStringLiteral("--super-key")); i >= 0) { // who gets the Windows key: status | on | off
        const QString what = i + 1 < app.arguments().size() ? app.arguments().at(i + 1) : QStringLiteral("status");
        if (what == QLatin1String("on"))
            launcher.setSuperKey(true);
        else if (what == QLatin1String("off"))
            launcher.setSuperKey(false);
        fputs(launcher.superKeyReport().constData(), stdout);
        return 0;
    }
    QObject::connect(&panelBar, &PanelBar::changed, &launcher, [&] { // no bar or no launcher button: the key goes back to Plasma
        if ((!panelBar.enabled() || !panelBar.launcher()) && launcher.superKey())
            launcher.setSuperKey(false);
    });
    QObject::connect(&launcher, &Launcher::toggleRequested, &app, [&bars] { // the Super key: on the screen the pointer is on
        QQuickWindow *target = nullptr;
        const QScreen *at = QGuiApplication::screenAt(QCursor::pos());
        for (auto it = bars.cbegin(); it != bars.cend(); ++it)
            if (it.key() == at || !target)
                target = it.value();
        if (target)
            QMetaObject::invokeMethod(target, "toggleLauncher");
    });
    auto addScreen = [&](QScreen *screen) {
        if (auto *bar = qobject_cast<QQuickWindow *>(barWindow.create())) {
            bar->setScreen(screen);
            desktop.attachBar(bar);
            const auto apply = [bar, &panelBar, &desktop] {
                desktop.setBarReserve(bar, panelBar.behavior() == QLatin1String("always") && panelBar.reserve() ? panelBar.height() : 0);
                bar->setVisible(panelBar.enabled());
            };
            apply();
            QObject::connect(&panelBar, &PanelBar::changed, bar, apply);
            bars.insert(screen, bar);
        } else {
            qCritical().noquote() << barWindow.errorString();
        }
        if (auto *strip = qobject_cast<QQuickWindow *>(edgeWindow.create())) { // widget mode: the thin line that opens the panel
            strip->setScreen(screen);
            desktop.attachEdgeStrip(strip);
            const auto show = [strip, &activities] { strip->setVisible(activities.capsuleMode() == QLatin1String("widget")); };
            show();
            QObject::connect(&activities, &Activities::settingsChanged, strip, show);
        } else {
            qCritical().noquote() << edgeWindow.errorString();
        }
        auto *window = qobject_cast<QQuickWindow *>(notchWindow.create());
        if (!window) {
            qCritical().noquote() << notchWindow.errorString();
            return;
        }
        window->setScreen(screen);
        desktop.attach(window);
        desktop.setAboveBar(window, panelBar.enabled()); // the bar must not cover the notch panel
        window->show();
        windows.insert(screen, window);
        QObject::connect(&panelBar, &PanelBar::changed, window, [window, &desktop, &panelBar] { desktop.setAboveBar(window, panelBar.enabled()); });
        if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
            // The bar can slip over the notch again (a full-screen app closing, Windows re-sorting topmost windows):
            // look now and then, and leave it alone while one of the notch's own menus is open.
            auto *keepAbove = new QTimer(window);
            keepAbove->setInterval(2000);
            QObject::connect(keepAbove, &QTimer::timeout, window, [window, &desktop, &panelBar] {
                if (panelBar.enabled() && window->isVisible() && !window->property("anyMenu").toBool())
                    desktop.setAboveBar(window, true);
            });
            keepAbove->start();
        }

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
        delete bars.take(screen);
    });

    // `notch --shot file.png` (offscreen, software rendering): the status bar with Quick Settings open, saved as a picture,
    // to see the layout where nobody is looking at a screen.
    if (const int i = app.arguments().indexOf(QStringLiteral("--shot")); i >= 0 && i + 1 < app.arguments().size()) {
        const QString path = app.arguments().at(i + 1);
        QTimer::singleShot(1200, &app, [&bars, &app, path] {
            QQuickWindow *bar = bars.isEmpty() ? nullptr : bars.constBegin().value();
            if (!bar)
                return app.quit();
            bar->setProperty(app.arguments().contains(QStringLiteral("--launcher")) ? "launcherOpen" : "quickOpen", true);
            QTimer::singleShot(900, &app, [bar, &app, path] { bar->grabWindow().save(path); app.quit(); });
        });
    }
    // `notch --shot-menu file.png <pid> [menu index]`: the status bar with that process's menu pulled down.
    if (const int i = app.arguments().indexOf(QStringLiteral("--shot-menu")); i >= 0 && i + 2 < app.arguments().size()) {
        const QString path = app.arguments().at(i + 1);
        const qint64 pid = app.arguments().at(i + 2).toLongLong();
        const int index = i + 3 < app.arguments().size() ? app.arguments().at(i + 3).toInt() : 0;
        appMenu.setPid(pid);
        QTimer::singleShot(2500, &app, [&bars, &app, path, index] {
            QQuickWindow *bar = bars.isEmpty() ? nullptr : bars.constBegin().value();
            if (!bar)
                return app.quit();
            bar->setProperty("menuX", 60);
            bar->setProperty("menuOpen", index);
            QTimer::singleShot(900, &app, [bar, &app, path] { bar->grabWindow().save(path); app.quit(); });
        });
    }
    // `notch --shot-page file.png <timer|color|shelf|tasks|capsule>`: the open panel on that page, the same way.
    if (const int i = app.arguments().indexOf(QStringLiteral("--shot-page")); i >= 0 && i + 2 < app.arguments().size()) {
        const QString path = app.arguments().at(i + 1), page = app.arguments().at(i + 2);
        QTimer::singleShot(1200, &app, [&windows, &app, &activities, &doomscroll, path, page] {
            QQuickWindow *w = windows.isEmpty() ? nullptr : windows.constBegin().value();
            if (!w)
                return app.quit();
            if (page == QLatin1String("doom")) { // twenty minutes of Instagram, and the warning it brings
                doomscroll.debugFeed(QStringLiteral("instagram"), 1200);
                return QTimer::singleShot(1500, &app, [w, &app, path] { w->grabWindow().save(path); app.quit(); });
            }
            if (page == QLatin1String("timer-run")) // a 25 minute timer running, to see the ring
                activities.startTimer(25 * 60, QStringLiteral("Tea"));
            if (page == QLatin1String("capsule")) // the closed notch as it rests (set capsuleMode=capsule in activities.conf)
                return QTimer::singleShot(1800, &app, [w, &app, path] { w->grabWindow().save(path); app.quit(); });
            w->setProperty("pinned", true);
            if (page == QLatin1String("share")) {
                QMetaObject::invokeMethod(w, "debugShare");
                return QTimer::singleShot(1200, &app, [w, &app, path] { w->grabWindow().save(path); app.quit(); });
            }
            w->setProperty("page", page.startsWith(QLatin1String("timer")) ? QStringLiteral("timer") : page);
            QTimer::singleShot(1500, &app, [w, &app, path] { w->grabWindow().save(path); app.quit(); });
        });
    }
    // `notch --shot-settings file.png [section]`: the open Settings page, the same way.
    if (const int i = app.arguments().indexOf(QStringLiteral("--shot-settings")); i >= 0 && i + 1 < app.arguments().size()) {
        const QString path = app.arguments().at(i + 1);
        const QString section = i + 2 < app.arguments().size() && !app.arguments().at(i + 2).startsWith(QLatin1String("--")) ? app.arguments().at(i + 2) : QString();
        QTimer::singleShot(1200, &app, [&windows, &app, path, section] {
            QQuickWindow *w = windows.isEmpty() ? nullptr : windows.constBegin().value();
            if (!w)
                return app.quit();
            w->setProperty("pinned", true);
            w->setProperty("page", QStringLiteral("settings"));
            if (!section.isEmpty())
                QMetaObject::invokeMethod(w, "debugSettings", Q_ARG(QVariant, section));
            QTimer::singleShot(1500, &app, [w, &app, path] { w->grabWindow().save(path); app.quit(); });
        });
    }
    engine.loadFromModule("Notch", "Tray");
    const int rc = app.exec();
    qDeleteAll(windows); // before the engine and controller they bind to
    qDeleteAll(docks);
    qDeleteAll(bars);
    return rc;
}
