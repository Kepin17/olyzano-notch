#pragma once

#include <QColor>
#include <QObject>

// Notch's colors as semantic roles, for QML (`theme.text`, `theme.surface`, …).
// Accent follows KDE's accent / color scheme (palette highlight, live). Light or dark follows
// `mode`: "dark" (default, the classic black island), "light", or "system". `style` picks the look:
// "classic" (Apple-like, KDE accent) or "washi" (Japanese: sumi ink / washi paper, vermilion shu).
class Theme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY changed)
    Q_PROPERTY(bool washi READ washi NOTIFY changed)
    Q_PROPERTY(QColor success READ success NOTIFY changed)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed)
    Q_PROPERTY(QColor warn READ warn NOTIFY changed)
    Q_PROPERTY(QColor info READ info NOTIFY changed)
    Q_PROPERTY(QColor media READ media NOTIFY changed)
    Q_PROPERTY(int chipRadius READ chipRadius NOTIFY changed)
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
    QColor accent() const { return washi() ? QColor(dark() ? "#d4493b" : "#c8372d") : m_accent; }
    QColor success() const { return pick("#30d158", "#30d158", "#7d9a5a", "#5f7d3f"); }
    QColor danger() const { return pick("#ff453a", "#ff453a", "#d4493b", "#c8372d"); }
    QColor warn() const { return pick("#ff9f0a", "#ff9f0a", "#d9a23a", "#b7811c"); }
    QColor info() const { return pick("#5ac8fa", "#5ac8fa", "#6f97b5", "#3f6a8a"); }
    QColor media() const { return pick("#7fb5cf", "#7fb5cf", "#8aa2b0", "#5d7888"); }
    int chipRadius() const { return washi() ? 6 : 13; }
    QString displayFont() const { return washi() ? QStringLiteral("Noto Serif CJK JP") : QString(); }
    QColor accentSoft() const { return withAlpha(accent(), dark() ? 0.30 : 0.18); }
    QColor accentFaint() const { return withAlpha(accent(), 0.10); }
    // pick(classic dark, classic light, washi dark, washi light)
    QColor island() const { return pick("#000000", "#ffffff", "#14120f", "#f3eee2"); }
    QColor text() const { return pick("#f5f5f7", "#1d1d1f", "#efe9dc", "#1c1a17"); }
    QColor textSoft() const { return pick("#e5e5ea", "#3a3a3c", "#d9d1c0", "#3b362f"); }
    QColor textDim() const { return pick("#8e8e93", "#6e6e73", "#9b917f", "#6b6355"); }
    QColor textFaint() const { return pick("#5a5a5e", "#aeaeb2", "#5f574b", "#a89e8b"); }
    QColor placeholder() const { return pick("#6e6e73", "#8e8e93", "#7a705f", "#8f8673"); }
    QColor surface() const { return pick("#1c1c1e", "#f0f0f2", "#211e1a", "#e8e1d1"); }
    QColor surfaceHover() const { return pick("#2c2c2e", "#e5e5ea", "#2b2722", "#dfd6c3"); }
    QColor surfacePressed() const { return pick("#3a3a3c", "#d1d1d6", "#37322b", "#d3c8b1"); }
    QColor well() const { return pick("#0c0c0e", "#f5f5f7", "#0e0c0a", "#f8f4eb"); }
    QColor card() const { return pick("#111113", "#f5f5f7", "#1a1714", "#eee7d8"); }
    QColor outline() const { return pick("#1f1f23", "#d2d2d7", "#3b352d", "#cdc2ab"); }
    QColor dangerBg() const { return pick("#2a1215", "#fde8e7", "#2c1512", "#f6dcd4"); }
    QColor warnBg() const { return pick("#2a1f0a", "#fff3e0", "#2a2110", "#f4e6c8"); }
    QColor infoBg() const { return pick("#12262e", "#e5f4fb", "#152029", "#dde7ee"); }

signals:
    void changed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QColor pick(const char *dark1, const char *light1, const char *dark2, const char *light2) const
    {
        return QColor(washi() ? (dark() ? dark2 : light2) : (dark() ? dark1 : light1));
    }
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
