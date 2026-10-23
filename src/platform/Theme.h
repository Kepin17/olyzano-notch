#pragma once

#include <QColor>
#include <QObject>

// Notch's colors as semantic roles, for QML (`theme.text`, `theme.surface`, …).
// Accent follows KDE's accent / color scheme (palette highlight, live). Light or dark follows
// `mode`: "dark" (default, the classic black island), "light", or "system".
class Theme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
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
    bool dark() const;
    QColor accent() const { return m_accent; }
    QColor accentSoft() const { return withAlpha(m_accent, dark() ? 0.30 : 0.18); }
    QColor accentFaint() const { return withAlpha(m_accent, 0.10); }
    QColor island() const { return pick("#000000", "#ffffff"); }
    QColor text() const { return pick("#f5f5f7", "#1d1d1f"); }
    QColor textSoft() const { return pick("#e5e5ea", "#3a3a3c"); }
    QColor textDim() const { return pick("#8e8e93", "#6e6e73"); }
    QColor textFaint() const { return pick("#5a5a5e", "#aeaeb2"); }
    QColor placeholder() const { return pick("#6e6e73", "#8e8e93"); }
    QColor surface() const { return pick("#1c1c1e", "#f0f0f2"); }
    QColor surfaceHover() const { return pick("#2c2c2e", "#e5e5ea"); }
    QColor surfacePressed() const { return pick("#3a3a3c", "#d1d1d6"); }
    QColor well() const { return pick("#0c0c0e", "#f5f5f7"); }
    QColor card() const { return pick("#111113", "#f5f5f7"); }
    QColor outline() const { return pick("#1f1f23", "#d2d2d7"); }
    QColor dangerBg() const { return pick("#2a1215", "#fde8e7"); }
    QColor warnBg() const { return pick("#2a1f0a", "#fff3e0"); }
    QColor infoBg() const { return pick("#12262e", "#e5f4fb"); }

signals:
    void changed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QColor pick(const char *darkColor, const char *lightColor) const { return QColor(dark() ? darkColor : lightColor); }
    static QColor withAlpha(QColor c, double alpha)
    {
        c.setAlphaF(alpha);
        return c;
    }
    void readAccent();

    QString m_mode = QStringLiteral("dark");
    QColor m_accent;
};
