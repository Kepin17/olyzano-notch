#include "Invite.h"

#include <QRegularExpression>

namespace Invite {

QUrl normalizeServer(const QString &text)
{
    QString typed = text.trimmed();
    if (typed.isEmpty())
        return {};
    if (!typed.contains(QLatin1String("://")))
        typed.prepend(QLatin1String("https://")); // "share.example.com" means the secure address
    QUrl url(typed);
    if (!url.isValid())
        return {};
    url.setQuery(QString());
    url.setFragment(QString());
    while (url.path().endsWith(u'/'))
        url.setPath(url.path().chopped(1));
    const QString host = url.host();
    const bool local = host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1");
    // The relay's key must not travel in the clear: plain http is only for a server on this very machine.
    if (host.isEmpty() || !(url.scheme() == QLatin1String("https") || (url.scheme() == QLatin1String("http") && local)))
        return {};
    return url;
}

Parsed parse(const QString &text)
{
    Parsed out;
    const QString link = text.trimmed();
    const int hash = link.indexOf(u'#');
    if (hash < 0 || hash == link.size() - 1) {
        out.error = QObject::tr("This invitation has no code after the # sign");
        return out;
    }
    out.token = link.mid(hash + 1);
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]{16,64}$"));
    if (!valid.match(out.token).hasMatch()) {
        out.error = QObject::tr("This invitation code is not valid");
        return out;
    }
    QString base = link.left(hash);
    if (!base.endsWith(QLatin1String("/join"))) {
        out.error = QObject::tr("This is not an invitation link (it should end in /join#...)");
        return out;
    }
    base.chop(5);
    out.server = normalizeServer(base);
    if (!out.server.isValid())
        out.error = QObject::tr("The server address must start with https://");
    return out;
}

}
