#pragma once

#include "platform/GlassModel.h"

#include <QColor>
#include <QObject>
#include <QVariantList>
#include <cmath>

// Notch's colors as semantic roles, for QML (`theme.text`, `theme.surface`, …).
// Light or dark follows `mode`: "dark" (default, the classic black island), "light", or "system".
// `style` picks the palette (see styles()): "classic" (Apple-like, accent follows KDE's accent / color
// scheme, live), "washi" (Japanese: sumi ink / washi paper, vermilion shu), and themed palettes with
// their own accent (frost, ocean, forest, sunset, lavender, mocha). Unknown styles fall back to classic.
class Theme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY changed)
    Q_PROPERTY(QVariantList styles READ styles CONSTANT) // {id, name, caption, accent, surface, island} for Settings
    Q_PROPERTY(bool washi READ washi NOTIFY changed)
    Q_PROPERTY(QColor success READ success NOTIFY changed)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed)
    Q_PROPERTY(QColor warn READ warn NOTIFY changed)
    Q_PROPERTY(QColor info READ info NOTIFY changed)
    Q_PROPERTY(QColor media READ media NOTIFY changed)
    Q_PROPERTY(int chipRadius READ chipRadius NOTIFY changed)
    // One shape language: corner radii and gaps come from these (Washi is the sharper one).
    Q_PROPERTY(int radiusSm READ radiusSm NOTIFY changed) // small controls, thumbnails
    Q_PROPERTY(int radiusMd READ radiusMd NOTIFY changed) // cards, tiles, menus
    Q_PROPERTY(int radiusLg READ radiusLg NOTIFY changed) // popups, panels
    Q_PROPERTY(QString displayFont READ displayFont NOTIFY changed) // headings; "" = system font
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(bool glass READ glass NOTIFY changed) // Settings -> Appearance -> Glass: the island and its surfaces are see-through
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed) // the accent as text on its own light tint: darker on a light theme
    Q_PROPERTY(QColor hoverFill READ hoverFill NOTIFY changed) // a quiet fill under the pointer
    Q_PROPERTY(QColor pressFill READ pressFill NOTIFY changed)
    Q_PROPERTY(QColor hairline READ hairline NOTIFY changed) // the 1 px edge of everything: white 8 % on dark, black 8 % on light
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor systemAccent READ systemAccent NOTIFY changed) // the desktop's own accent (Windows / KDE), whatever look is chosen
    Q_PROPERTY(QColor accentSoft READ accentSoft NOTIFY changed)   // tinted fills (active buttons)
    Q_PROPERTY(QColor accentFaint READ accentFaint NOTIFY changed) // very light tints (selection, drop)
    Q_PROPERTY(QColor island READ island NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor textSoft READ textSoft NOTIFY changed)
    Q_PROPERTY(QColor textDim READ textDim NOTIFY changed)
    Q_PROPERTY(QColor textFaint READ textFaint NOTIFY changed)
    Q_PROPERTY(QColor placeholder READ placeholder NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor surfaceHover READ surfaceHover NOTIFY changed)
    Q_PROPERTY(QColor surfacePressed READ surfacePressed NOTIFY changed)
    Q_PROPERTY(QColor well READ well NOTIFY changed)
    Q_PROPERTY(QColor card READ card NOTIFY changed)
    Q_PROPERTY(QColor outline READ outline NOTIFY changed)
    Q_PROPERTY(QColor dangerBg READ dangerBg NOTIFY changed)
    Q_PROPERTY(QColor warnBg READ warnBg NOTIFY changed)
    Q_PROPERTY(QColor infoBg READ infoBg NOTIFY changed)

public:
    explicit Theme(QObject *parent = nullptr);

    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    QString style() const { return m_style; }
    void setStyle(const QString &style);
    bool washi() const { return m_style == QLatin1String("washi"); }
    bool dark() const;
    QColor accent() const;
    QColor systemAccent() const { return m_accent; }
    QColor success() const { return role(&Palette::success); }
    QColor danger() const { return role(&Palette::danger); }
    QColor warn() const { return role(&Palette::warn); }
    QColor info() const { return role(&Palette::info); }
    QColor media() const { return role(&Palette::media); }
    int chipRadius() const { return washi() ? 6 : 13; }
    // Text on a filled background: white or near-black, whichever reads better (white on a light blue accent
    // is hard to read; WCAG contrast ratio decides).
    Q_INVOKABLE QColor onColor(const QColor &background) const
    {
        const auto channel = [](double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
        const double l = 0.2126 * channel(background.redF()) + 0.7152 * channel(background.greenF()) + 0.0722 * channel(background.blueF());
        const double white = 1.05 / (l + 0.05);
        const double black = (l + 0.05) / (0.006 + 0.05); // #111113
        return white >= black ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#111113"));
    }
    int radiusSm() const { return washi() ? 4 : 8; }
    int radiusMd() const { return washi() ? 6 : 12; }
    int radiusLg() const { return washi() ? 8 : 18; }
    QString displayFont() const { return washi() ? QStringLiteral("Noto Serif CJK JP") : QString(); }
    QColor accentSoft() const { return withAlpha(accent(), dark() ? 0.30 : 0.18); }
    QColor accentFaint() const { return withAlpha(accent(), 0.10); }
    bool glass() const { return m_glass; }
    // Glass on: opacity of the island and of the surfaces on it (GlassModel.h); the blur behind is the system's, when there is one.
    void setGlass(bool on, int strength, bool blur);
    QColor accentText() const { return dark() ? accent() : accent().darker(165); }
    QColor hoverFill() const { return withAlpha(dark() ? QColor(255, 255, 255) : QColor(0, 0, 0), dark() ? 0.07 : 0.06); }
    QColor pressFill() const { return withAlpha(dark() ? QColor(255, 255, 255) : QColor(0, 0, 0), dark() ? 0.12 : 0.10); }
    QColor hairline() const { return withAlpha(dark() ? QColor(255, 255, 255) : QColor(0, 0, 0), 0.08); }
    QColor island() const { return m_glass ? withAlpha(role(&Palette::island), GlassModel::islandAlpha(m_glassStrength, m_blur)) : role(&Palette::island); }
    QColor text() const { return role(&Palette::text); }
    QColor textSoft() const { return role(&Palette::textSoft); }
    QColor textDim() const { return role(&Palette::textDim); }
    QColor textFaint() const { return role(&Palette::textFaint); }
    QColor placeholder() const { return role(&Palette::placeholder); }
    QColor surface() const { return onIsland(role(&Palette::surface)); }
    QColor surfaceHover() const { return onIsland(role(&Palette::surfaceHover)); }
    QColor surfacePressed() const { return onIsland(role(&Palette::surfacePressed)); }
    QColor well() const { return onIsland(role(&Palette::well)); }
    QColor card() const { return onIsland(role(&Palette::card)); }
    QColor outline() const { return role(&Palette::outline); }
    QColor dangerBg() const { return role(&Palette::dangerBg); }
    QColor warnBg() const { return role(&Palette::warnBg); }
    QColor infoBg() const { return role(&Palette::infoBg); }
    QVariantList styles() const;

    // One look in one brightness. accent == nullptr: follow the desktop's accent (classic).
    struct Palette {
        const char *island, *text, *textSoft, *textDim, *textFaint, *placeholder;
        const char *surface, *surfaceHover, *surfacePressed, *well, *card, *outline;
        const char *dangerBg, *warnBg, *infoBg;
        const char *accent, *success, *danger, *warn, *info, *media;
    };

signals:
    void changed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    const Palette &palette() const;
    QColor role(const char *Palette::*member) const { return QColor(palette().*member); }
    static QColor withAlpha(QColor c, double alpha)
    {
        c.setAlphaF(alpha);
        return c;
    }
    void readAccent();
    QColor onIsland(QColor c) const { return m_glass ? withAlpha(c, GlassModel::surfaceAlpha(m_glassStrength, m_blur)) : c; }

    QString m_mode = QStringLiteral("dark");
    QString m_style = QStringLiteral("classic");
    QColor m_accent;
    bool m_glass = false, m_blur = false;
    int m_glassStrength = 60;
};
