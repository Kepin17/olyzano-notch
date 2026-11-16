#pragma once

#include <QByteArray>
#include <QList>
#include <QPoint>
#include <QRegularExpression>
#include <QSize>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>

// The parts of mirroring an Android phone through `adb` that need no phone: reading what adb prints, working out where a click on the picture lands
// on the phone's screen, the shell command for each kind of touch or key, and cutting the JPEG pictures out of the byte stream ffmpeg writes. Pure and
// tested; AndroidMirror.cpp runs the programs.
namespace Android {

struct Device {
    QString serial, state, model; // state: device | unauthorized | offline | other
    bool wifi = false;            // a network address, not a cable
    bool usable() const { return state == QLatin1String("device"); }
};

// `adb devices -l`. Lines such as "* daemon started" and the header are skipped; "no permissions" (a missing udev rule) is "other".
inline QList<Device> parseDevices(const QByteArray &out)
{
    QList<Device> list;
    const QStringList lines = QString::fromUtf8(out).split(u'\n');
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('*')) || line.startsWith(QLatin1String("List of devices")))
            continue;
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 2)
            continue;
        Device d;
        d.serial = parts.at(0);
        const QString state = parts.at(1);
        d.state = state == QLatin1String("device") || state == QLatin1String("unauthorized") || state == QLatin1String("offline") ? state : QStringLiteral("other");
        for (const QString &p : parts.mid(2))
            if (p.startsWith(QLatin1String("model:")))
                d.model = QString(p.mid(6)).replace(u'_', u' ');
        d.wifi = d.serial.contains(u':') || d.serial.contains(QLatin1String("._adb-tls-"));
        list.append(d);
    }
    return list;
}

// `adb shell wm size`: "Physical size: 1080x2400", and "Override size: 720x1600" when the person lowered it (that one is what is drawn).
inline QSize parseWmSize(const QString &out)
{
    QSize physical, override;
    static const QRegularExpression re(QStringLiteral("(Physical|Override) size:\\s*(\\d+)x(\\d+)"));
    auto it = re.globalMatch(out);
    while (it.hasNext()) {
        const auto m = it.next();
        (m.captured(1) == QLatin1String("Override") ? override : physical) = QSize(m.captured(2).toInt(), m.captured(3).toInt());
    }
    return override.isValid() && !override.isEmpty() ? override : physical;
}

// The size of the picture asked of the phone: the long side at most `longest`, the same shape, both sides even (the encoder needs that).
inline QSize streamSize(const QSize &screen, int longest = 1280)
{
    if (screen.width() < 2 || screen.height() < 2)
        return QSize();
    const double scale = std::min(1.0, double(longest) / double(std::max(screen.width(), screen.height())));
    const auto even = [](double v) { return std::max(2, int(std::lround(v / 2.0)) * 2); };
    return QSize(even(screen.width() * scale), even(screen.height() * scale));
}

// A point on the picture (0..1 across, 0..1 down) -> a pixel of the phone's screen. `frame` is the picture as it comes, `screen` the phone's own size in
// its natural position: when the phone is turned, the picture is wider than tall and the screen is the other way round, so the sides swap.
inline QPoint toDevice(double nx, double ny, const QSize &frame, const QSize &screen)
{
    if (screen.isEmpty())
        return QPoint();
    QSize s = screen;
    if (frame.isValid() && (frame.width() > frame.height()) != (s.width() > s.height()))
        s.transpose();
    const auto pixel = [](double n, int size) { return std::clamp(int(std::lround(std::clamp(n, 0.0, 1.0) * (size - 1))), 0, std::max(0, size - 1)); };
    return QPoint(pixel(nx, s.width()), pixel(ny, s.height()));
}

inline QString tapCommand(const QPoint &p) { return QStringLiteral("input tap %1 %2").arg(p.x()).arg(p.y()); }

// A drag; held still it is a long press. The time is kept between 1 ms and 5 s.
inline QString swipeCommand(const QPoint &from, const QPoint &to, int ms)
{
    return QStringLiteral("input swipe %1 %2 %3 %4 %5").arg(from.x()).arg(from.y()).arg(to.x()).arg(to.y()).arg(std::clamp(ms, 1, 5000));
}

// The keys the window offers; anything else is refused so that a name from outside can never become another command.
inline QString keyCommand(const QString &name)
{
    static const QList<QPair<QString, QString>> keys = {{QStringLiteral("back"), QStringLiteral("KEYCODE_BACK")},       {QStringLiteral("home"), QStringLiteral("KEYCODE_HOME")},
                                                        {QStringLiteral("recents"), QStringLiteral("KEYCODE_APP_SWITCH")}, {QStringLiteral("power"), QStringLiteral("KEYCODE_POWER")},
                                                        {QStringLiteral("volume-up"), QStringLiteral("KEYCODE_VOLUME_UP")}, {QStringLiteral("volume-down"), QStringLiteral("KEYCODE_VOLUME_DOWN")},
                                                        {QStringLiteral("enter"), QStringLiteral("KEYCODE_ENTER")},       {QStringLiteral("backspace"), QStringLiteral("KEYCODE_DEL")},
                                                        {QStringLiteral("wake"), QStringLiteral("KEYCODE_WAKEUP")},       {QStringLiteral("menu"), QStringLiteral("KEYCODE_MENU")}};
    for (const auto &k : keys)
        if (k.first == name)
            return QStringLiteral("input keyevent ") + k.second;
    return QString();
}

// Typing: `input text` takes plain ASCII only (a space is "%s"). The text goes inside single quotes so the phone's shell reads it as one word. Returns
// the command, or "" with the reason in `why` (not ASCII, a "%s" of its own, or too long).
inline QString textCommand(const QString &text, QString *why = nullptr)
{
    const auto refuse = [&](const QString &w) { if (why) *why = w; return QString(); };
    if (text.isEmpty())
        return refuse(QStringLiteral("Nothing to type."));
    if (text.size() > 500)
        return refuse(QStringLiteral("That is too long to type at once."));
    QString out;
    for (const QChar c : text) {
        if (c == u' ')
            out += QStringLiteral("%s");
        else if (c == u'\'')
            out += QStringLiteral("'\\''");
        else if (c.unicode() < 0x20 || c.unicode() > 0x7e)
            return refuse(QStringLiteral("The phone can only be typed to in plain letters, digits and symbols (no accents, emoji or new lines)."));
        else
            out += c;
    }
    if (text.contains(QLatin1String("%s")))
        return refuse(QStringLiteral("The phone reads \"%s\" as a space, so that cannot be typed."));
    return QStringLiteral("input text '") + out + QStringLiteral("'");
}

// `adb connect` and `adb pair` answer in words, with exit code 0 even on failure.
inline bool connectedOrPaired(const QString &out)
{
    const QString s = out.toLower();
    return (s.contains(QLatin1String("connected to")) && !s.contains(QLatin1String("failed"))) || s.contains(QLatin1String("already connected")) || s.contains(QLatin1String("successfully paired"));
}

// "host:port" with a plausible address and port, so that what the person types never becomes an option of adb.
inline bool validEndpoint(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9.\\-]{0,100}:\\d{2,5}$"));
    if (!re.match(text).hasMatch())
        return false;
    const int port = text.section(u':', -1).toInt();
    return port >= 1 && port <= 65535;
}

// Where adb may be, best first: the PATH is looked at by the caller; these are the places the Android SDK puts it.
inline QStringList adbCandidates(const QString &home, const QString &androidHome, const QString &localAppData)
{
    QStringList bases;
    if (!androidHome.isEmpty())
        bases << androidHome;
    if (!home.isEmpty())
        bases << home + QStringLiteral("/Android/Sdk") << home + QStringLiteral("/Library/Android/sdk");
    if (!localAppData.isEmpty())
        bases << localAppData + QStringLiteral("/Android/Sdk");
    QStringList out;
    for (const QString &base : std::as_const(bases))
        out << base + QStringLiteral("/platform-tools/adb") << base + QStringLiteral("/platform-tools/adb.exe");
    return out;
}

// The pictures of a Motion JPEG stream (what ffmpeg writes to a pipe), cut where each one starts (FF D8) and ends (FF D9). Inside a picture an FF byte
// of data is always followed by 00 or a restart marker, so FF D9 is only ever the end.
class JpegSplitter
{
public:
    QList<QByteArray> feed(const QByteArray &bytes)
    {
        m_buffer += bytes;
        QList<QByteArray> out;
        for (;;) {
            const qsizetype start = m_buffer.indexOf("\xFF\xD8");
            if (start < 0) {
                const bool halfStart = !m_buffer.isEmpty() && static_cast<unsigned char>(m_buffer.back()) == 0xFF; // the D8 may come with the next bytes
                m_buffer = halfStart ? QByteArray(1, '\xFF') : QByteArray();
                break;
            }
            const qsizetype end = m_buffer.indexOf("\xFF\xD9", start + 2);
            if (end < 0) {
                if (start > 0)
                    m_buffer.remove(0, start); // what came before the start is not a picture
                break;
            }
            out.append(m_buffer.mid(start, end + 2 - start));
            m_buffer.remove(0, end + 2);
        }
        if (m_buffer.size() > 8 * 1024 * 1024) // a picture that never ends: start again
            m_buffer.clear();
        return out;
    }
    void reset() { m_buffer.clear(); }

private:
    QByteArray m_buffer;
};

} // namespace Android
