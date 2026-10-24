#pragma once

#include <QString>
#include <QUrl>

// Invitation links a host's relay prints: https://share.example.com/join#<token>.
namespace Invite {

struct Parsed {
    QUrl server;   // https://share.example.com (any path before /join kept, no trailing slash)
    QString token;
    QString error; // empty when the link is usable
};

Parsed parse(const QString &text);

// The address of a relay as the user typed it: https only (http for localhost, for testing), no trailing
// slash, no query or fragment. Invalid (empty) QUrl when it cannot be used.
QUrl normalizeServer(const QString &text);

}
