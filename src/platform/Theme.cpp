#include "Theme.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

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
