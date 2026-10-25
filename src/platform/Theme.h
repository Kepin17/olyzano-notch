#pragma once

#include <QColor>
#include <QObject>
#include <QVariantList>

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
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
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
    QColor success() const { return role(&Palette::success); }
    QColor danger() const { return role(&Palette::danger); }
    QColor warn() const { return role(&Palette::warn); }
    QColor info() const { return role(&Palette::info); }
    QColor media() const { return role(&Palette::media); }
    int chipRadius() const { return washi() ? 6 : 13; }
    int radiusSm() const { return washi() ? 4 : 8; }
    int radiusMd() const { return washi() ? 6 : 12; }
    int radiusLg() const { return washi() ? 8 : 18; }
    QString displayFont() const { return washi() ? QStringLiteral("Noto Serif CJK JP") : QString(); }
    QColor accentSoft() const { return withAlpha(accent(), dark() ? 0.30 : 0.18); }
    QColor accentFaint() const { return withAlpha(accent(), 0.10); }
    QColor island() const { return role(&Palette::island); }
    QColor text() const { return role(&Palette::text); }
    QColor textSoft() const { return role(&Palette::textSoft); }
    QColor textDim() const { return role(&Palette::textDim); }
    QColor textFaint() const { return role(&Palette::textFaint); }
    QColor placeholder() const { return role(&Palette::placeholder); }
    QColor surface() const { return role(&Palette::surface); }
    QColor surfaceHover() const { return role(&Palette::surfaceHover); }
    QColor surfacePressed() const { return role(&Palette::surfacePressed); }
    QColor well() const { return role(&Palette::well); }
    QColor card() const { return role(&Palette::card); }
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

    QString m_mode = QStringLiteral("dark");
    QString m_style = QStringLiteral("classic");
    QColor m_accent;
};
