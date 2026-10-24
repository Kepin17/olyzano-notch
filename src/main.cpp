#include "app/NotchController.h"
#include "app/ShareController.h"
#include "services/QrImage.h"
#include "platform/DesktopIntegration.h"
#include "platform/AudioLevels.h"
#include "platform/ColorPicker.h"
#include "platform/KdeConnect.h"
#include "platform/Theme.h"
#include "platform/MediaPlayers.h"
#include "platform/SystemEvents.h"

#include <QDir>
#include <QApplication>
#include <QIcon>
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
        return QIcon::fromTheme(id, QIcon::fromTheme(QStringLiteral("text-x-generic"))).pixmap(s);
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

    DesktopIntegration desktop;
    SystemEvents systemEvents;
    MediaPlayers media;
    AudioLevels audio;
    KdeConnect kdeconnect;
    Theme theme;
    ColorPicker colorPicker;
    QObject::connect(&colorPicker, &ColorPicker::picked, &controller, &NotchController::addColor);
    theme.setMode(controller.themeMode());
    theme.setStyle(controller.uiStyle());
    QObject::connect(&controller, &NotchController::uiStyleChanged, &theme, [&] { theme.setStyle(controller.uiStyle()); });
    QObject::connect(&controller, &NotchController::themeModeChanged, &theme, [&] { theme.setMode(controller.themeMode()); });
    QObject::connect(&kdeconnect, &KdeConnect::received, &controller, &NotchController::addReceived);
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("mime"), new MimeIconProvider);
    engine.addImageProvider(QStringLiteral("qr"), new QrImageProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("share"), &share);
    engine.rootContext()->setContextProperty(QStringLiteral("notch"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("desktop"), &desktop);
    engine.rootContext()->setContextProperty(QStringLiteral("systemEvents"), &systemEvents);
    engine.rootContext()->setContextProperty(QStringLiteral("media"), &media);
    engine.rootContext()->setContextProperty(QStringLiteral("audio"), &audio);
    engine.rootContext()->setContextProperty(QStringLiteral("kdeconnect"), &kdeconnect);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("colorPicker"), &colorPicker);

    // One Notch per monitor, following hotplug.
    QQmlComponent notchWindow(&engine, "Notch", "Main");
    QHash<QScreen *, QQuickWindow *> windows;
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
    };
    for (QScreen *screen : app.screens())
        addScreen(screen);
    if (windows.isEmpty())
        return 1;
    QObject::connect(&app, &QGuiApplication::screenAdded, addScreen);
    QObject::connect(&app, &QGuiApplication::screenRemoved, [&](QScreen *screen) { delete windows.take(screen); });

    engine.loadFromModule("Notch", "Tray");
    const int rc = app.exec();
    qDeleteAll(windows); // before the engine and controller they bind to
    return rc;
}
