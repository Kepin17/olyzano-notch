#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

// Which desktop notifications the notch repeats. Pure, for the tests.
class NotificationFilter
{
public:
    // Notch's own, and KDE Connect's (the phone's notifications already come through KdeConnect, so they
    // would show twice), never; nor anything without a name; nor the same app + title again within 2 s
    // (apps that update one notification, or post it twice). Every other app comes through.
    bool accept(const QString &app, const QString &title, qint64 nowMs)
    {
        const QString a = app.trimmed().toLower();
        if (a.isEmpty() && title.trimmed().isEmpty())
            return false;
        if (a == QLatin1String("notch") || a == QLatin1String("olyzano notch") || a.startsWith(QLatin1String("kdeconnect")) || a.startsWith(QLatin1String("kde connect")))
            return false;
        for (auto it = m_recent.begin(); it != m_recent.end();)
            it = nowMs - it.value() > 2000 ? m_recent.erase(it) : std::next(it);
        const QString key = a + u'\x1f' + title;
        if (m_recent.contains(key))
            return false;
        m_recent.insert(key, nowMs);
        return true;
    }

private:
    QHash<QString, qint64> m_recent;
};
