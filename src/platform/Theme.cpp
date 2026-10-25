#include "Theme.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QVariantMap>

namespace {
// Field order: island, text, textSoft, textDim, textFaint, placeholder,
//              surface, surfaceHover, surfacePressed, well, card, outline, dangerBg, warnBg, infoBg,
//              accent, success, danger, warn, info, media
struct Style {
    const char *id, *name, *caption;
    Theme::Palette dark, light;
};

const Style Styles[] = {
    {"classic", "Classic", "Black glass, highlights in your KDE accent color.",
     {"#000000", "#f5f5f7", "#e5e5ea", "#8e8e93", "#5a5a5e", "#6e6e73", "#1c1c1e", "#2c2c2e", "#3a3a3c", "#0c0c0e", "#111113", "#1f1f23",
      "#2a1215", "#2a1f0a", "#12262e", nullptr, "#30d158", "#ff453a", "#ff9f0a", "#5ac8fa", "#7fb5cf"},
     {"#ffffff", "#1d1d1f", "#3a3a3c", "#6e6e73", "#aeaeb2", "#8e8e93", "#f0f0f2", "#e5e5ea", "#d1d1d6", "#f5f5f7", "#f5f5f7", "#d2d2d7",
      "#fde8e7", "#fff3e0", "#e5f4fb", nullptr, "#30d158", "#ff453a", "#ff9f0a", "#5ac8fa", "#7fb5cf"}},
    {"washi", "Washi 和紙", "Sumi ink and washi paper, with a vermilion (shu) accent.",
     {"#14120f", "#efe9dc", "#d9d1c0", "#9b917f", "#5f574b", "#7a705f", "#211e1a", "#2b2722", "#37322b", "#0e0c0a", "#1a1714", "#3b352d",
      "#2c1512", "#2a2110", "#152029", "#d4493b", "#7d9a5a", "#d4493b", "#d9a23a", "#6f97b5", "#8aa2b0"},
     {"#f3eee2", "#1c1a17", "#3b362f", "#6b6355", "#a89e8b", "#8f8673", "#e8e1d1", "#dfd6c3", "#d3c8b1", "#f8f4eb", "#eee7d8", "#cdc2ab",
      "#f6dcd4", "#f4e6c8", "#dde7ee", "#c8372d", "#5f7d3f", "#c8372d", "#b7811c", "#3f6a8a", "#5d7888"}},
    {"frost", "Frost", "Arctic blue-greys with an icy accent.",
     {"#1b1f27", "#eceff4", "#d8dee9", "#8f9bb3", "#4c566a", "#7b88a1", "#242933", "#2e3440", "#3b4252", "#161a21", "#20242d", "#343b48",
      "#3a2328", "#3a3224", "#213040", "#88c0d0", "#a3be8c", "#bf616a", "#ebcb8b", "#81a1c1", "#8fbcbb"},
     {"#eceff4", "#2e3440", "#3b4252", "#5e6a80", "#a3acbd", "#7b88a1", "#e5e9f0", "#d8dee9", "#cbd2de", "#f4f6f9", "#edf0f5", "#c9d0dc",
      "#f5dfe1", "#f7eed9", "#dde8f2", "#5e81ac", "#6f8f55", "#bf616a", "#c2933a", "#5e81ac", "#4f8a8b"}},
    {"ocean", "Ocean", "Deep sea blues with a teal accent.",
     {"#06121c", "#e6f1f8", "#c8dbe8", "#7f98ab", "#3f5466", "#6a8396", "#0d1f2d", "#15293a", "#1d3448", "#040d15", "#0a1825", "#1b3142",
      "#2e1418", "#2c2412", "#0f2c3a", "#2ec4d6", "#3ddc97", "#ff5d73", "#ffb347", "#4fc3f7", "#6fb6d6"},
     {"#f2f8fc", "#0b2233", "#23394a", "#5a7385", "#a7b9c6", "#7c93a3", "#e3eef5", "#d4e4ee", "#c3d8e6", "#f8fbfd", "#ebf3f8", "#c2d4e0",
      "#fde4e8", "#fdf0dc", "#dcf0f8", "#0b8fa8", "#1e9e6a", "#d93a52", "#c27a12", "#1b84b8", "#3a7f9e"}},
    {"forest", "Forest", "Moss and pine greens, calm and earthy.",
     {"#0f140f", "#e8efe4", "#ccd8c6", "#8a9a84", "#4a5746", "#77876f", "#182018", "#212b20", "#2b372a", "#0a0e0a", "#141b14", "#263125",
      "#2c1715", "#2b2513", "#16262a", "#7fbf6a", "#8fd176", "#e06c5a", "#e0b04f", "#6fb3b8", "#94b38a"},
     {"#f3f6ef", "#1c2a1a", "#33432f", "#66775f", "#aab6a3", "#7f8f78", "#e6ede0", "#dae4d2", "#cbd8c1", "#f8faf5", "#edf2e8", "#c8d4bf",
      "#f7e0db", "#f6ecd2", "#dcecee", "#3f8a3a", "#3f8a3a", "#c2493a", "#b07f1c", "#3a7f86", "#5b7f52"}},
    {"sunset", "Sunset", "Warm dusk tones with a coral accent.",
     {"#1a1014", "#fbeee8", "#ecd5cc", "#a8877f", "#5c4440", "#92736c", "#24161b", "#301e24", "#3d262e", "#120a0d", "#1e1317", "#3a2429",
      "#3a1518", "#3a2612", "#2a1f30", "#ff7a59", "#7ed39b", "#ff4f6d", "#ffb454", "#c792ea", "#f29e8e"},
     {"#fff6f1", "#2b1a17", "#46302b", "#85665e", "#c9b0a8", "#a08880", "#fbe9e1", "#f6dcd1", "#efcbbd", "#fffaf7", "#fdf0ea", "#eccbbd",
      "#fde0e3", "#fdeed7", "#f0e4f7", "#e8572f", "#2f9e5b", "#d9364f", "#c97a12", "#8e5bbf", "#c76a58"}},
    {"lavender", "Lavender", "Soft violets with a lilac accent.",
     {"#14111c", "#efeaf8", "#d6cee8", "#958ba9", "#4f4762", "#7f7594", "#1d1828", "#272134", "#322a42", "#0e0b14", "#18141f", "#2e2640",
      "#2e1520", "#2c2414", "#1c2236", "#b392f0", "#7fd1a8", "#f27a93", "#f0c27a", "#8fb4f0", "#c3a6e8"},
     {"#f7f4fc", "#231c33", "#3a3150", "#6f6585", "#b7aecb", "#8a809f", "#ece6f6", "#e1d9f0", "#d3c8e8", "#fbf9fe", "#f2eef9", "#d5cbe6",
      "#fbe2e9", "#f8eedb", "#e2e9f8", "#7c4dd6", "#2f9e6e", "#d0436a", "#b5821f", "#4a72c9", "#8a64c4"}},
    {"mocha", "Mocha", "Coffee browns with a caramel accent.",
     {"#1b1714", "#f1e9e1", "#dacdc0", "#a08f80", "#574b41", "#8a7a6c", "#25201b", "#2f2822", "#3a322a", "#14110e", "#201b17", "#362e27",
      "#331a17", "#332715", "#1f2628", "#d39b6a", "#9cc58a", "#e07a6a", "#e6b450", "#8fb3c0", "#c9a385"},
     {"#faf6f1", "#2a211b", "#44382f", "#7d6c5f", "#c3b5a8", "#998878", "#f0e7dd", "#e7dbcf", "#dccdbe", "#fdfaf6", "#f5eee6", "#dccdbd",
      "#f8e1dc", "#f6ebd3", "#e1ebee", "#a0632f", "#5d8f47", "#c0503f", "#b0801e", "#4f7f90", "#96704f"}},
};

const Style &findStyle(const QString &id)
{
    for (const Style &s : Styles)
        if (id == QLatin1String(s.id))
            return s;
    return Styles[0];
}
} // namespace

Theme::Theme(QObject *parent)
    : QObject(parent)
{
    readAccent();
    qApp->installEventFilter(this); // ApplicationPaletteChange: KDE accent / scheme changed
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &Theme::changed);
}

void Theme::setMode(const QString &mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    emit changed();
}

void Theme::setStyle(const QString &style)
{
    if (style == m_style)
        return;
    m_style = style;
    emit changed();
}

bool Theme::dark() const
{
    if (m_mode == QLatin1String("light"))
        return false;
    if (m_mode == QLatin1String("system"))
        return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
    return true;
}

void Theme::readAccent()
{
    m_accent = QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight);
    if (!m_accent.isValid())
        m_accent = QColor("#0a84ff");
}

bool Theme::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange) {
        readAccent();
        emit changed();
    }
    return QObject::eventFilter(watched, event);
}

const Theme::Palette &Theme::palette() const
{
    const Style &s = findStyle(m_style);
    return dark() ? s.dark : s.light;
}

QColor Theme::accent() const
{
    const char *fixed = palette().accent;
    return fixed ? QColor(fixed) : m_accent;
}

QVariantList Theme::styles() const
{
    QVariantList out;
    for (const Style &s : Styles)
        out << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(s.id)},
                           {QStringLiteral("name"), QString::fromUtf8(s.name)},
                           {QStringLiteral("caption"), QString::fromUtf8(s.caption)},
                           {QStringLiteral("accent"), QColor(s.dark.accent ? s.dark.accent : "#0a84ff")},
                           {QStringLiteral("surface"), QColor(s.dark.surface)},
                           {QStringLiteral("island"), QColor(s.dark.island)}};
    return out;
}
