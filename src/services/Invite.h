#pragma once

#include <QString>
#include <QUrl>

// Invitation links a host's relay prints: https://share.example.com/join#<token>. A host that has a cloud shelf adds the shelf's
// key after a "~" (https://share.example.com/join#<token>~<key>): the key stays in the fragment, so the relay never sees it.
namespace Invite {

struct Parsed {
    QUrl server;   // https://share.example.com (any path before /join kept, no trailing slash)
    QString token;
    QByteArray shelfKey; // the cloud shelf's key when the link carries one, else empty
    QString error; // empty when the link is usable
};

Parsed parse(const QString &text);

// The link as the host shares it: the relay's invitation plus the cloud shelf's key.
QString withShelfKey(const QString &link, const QByteArray &key);

// The address of a relay as the user typed it: https only (http for localhost, for testing), no trailing
// slash, no query or fragment. Invalid (empty) QUrl when it cannot be used.
QUrl normalizeServer(const QString &text);

}
