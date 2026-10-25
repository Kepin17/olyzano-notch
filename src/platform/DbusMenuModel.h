#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

// An application's menu as `com.canonical.dbusmenu` hands it over (id, properties, children), made into what the
// bar draws. Pure, for the tests. A raw node is {id: int, props: map, children: list of raw nodes}.
namespace DbusMenuModel {

// "_File" -> "File" (the underscore marks the key to press); "A__B" -> "A_B".
inline QString cleanLabel(const QString &raw)
{
    QString out;
    for (qsizetype i = 0; i < raw.size(); ++i) {
        if (raw[i] == u'_') {
            if (i + 1 < raw.size() && raw[i + 1] == u'_')
                out += raw[i++];
            continue;
        }
        out += raw[i];
    }
    return out;
}

// [["Control","Shift","i"]] -> "Ctrl+Shift+I": the first way of pressing it.
inline QString shortcutText(const QVariant &v)
{
    QVariantList ways = v.toList();
    if (ways.isEmpty())
        return {};
    QStringList parts;
    for (const QVariant &p : ways.first().toStringList()) {
        const QString k = p.toString();
        static const QHash<QChar, QString> raw{{QChar(0x1b), QStringLiteral("Esc")}, {QChar(0x7f), QStringLiteral("Del")}, {QChar(0x08), QStringLiteral("Backspace")},
                                               {QChar(0x09), QStringLiteral("Tab")}, {QChar(0x0d), QStringLiteral("Enter")}, {QChar(0x0a), QStringLiteral("Enter")}};
        if (k.size() == 1 && raw.contains(k.at(0))) { // some apps send the key itself, not its name
            parts << raw.value(k.at(0));
            continue;
        }
        parts << (k == QLatin1String("Control") ? QStringLiteral("Ctrl") : k == QLatin1String("Super") ? QStringLiteral("Meta")
                  : k.size() == 1 ? k.toUpper() : k.left(1).toUpper() + k.mid(1));
    }
    return parts.join(u'+');
}

// Something to show: hidden items go, a separator never leads, trails or doubles.
inline QVariantList tidy(const QVariantList &items)
{
    QVariantList out;
    for (const QVariant &v : items) {
        const bool separator = v.toMap().value(QStringLiteral("separator")).toBool();
        if (separator && (out.isEmpty() || out.last().toMap().value(QStringLiteral("separator")).toBool()))
            continue;
        out << v;
    }
    while (!out.isEmpty() && out.last().toMap().value(QStringLiteral("separator")).toBool())
        out.removeLast();
    return out;
}

inline QVariantMap convert(const QVariantMap &raw)
{
    const QVariantMap p = raw.value(QStringLiteral("props")).toMap();
    QVariantMap n;
    n[QStringLiteral("id")] = raw.value(QStringLiteral("id")).toInt();
    n[QStringLiteral("separator")] = p.value(QStringLiteral("type")).toString() == QLatin1String("separator");
    n[QStringLiteral("label")] = cleanLabel(p.value(QStringLiteral("label")).toString());
    n[QStringLiteral("enabled")] = p.value(QStringLiteral("enabled"), true).toBool();
    n[QStringLiteral("shortcut")] = shortcutText(p.value(QStringLiteral("shortcut")));
    const QString toggle = p.value(QStringLiteral("toggle-type")).toString();
    n[QStringLiteral("check")] = toggle == QLatin1String("checkmark") && p.value(QStringLiteral("toggle-state")).toInt() == 1;
    n[QStringLiteral("radio")] = toggle == QLatin1String("radio") && p.value(QStringLiteral("toggle-state")).toInt() == 1;
    QVariantList kids;
    for (const QVariant &c : raw.value(QStringLiteral("children")).toList()) {
        const QVariantMap child = c.toMap();
        if (!child.value(QStringLiteral("props")).toMap().value(QStringLiteral("visible"), true).toBool())
            continue;
        kids << convert(child);
    }
    kids = tidy(kids);
    n[QStringLiteral("children")] = kids;
    n[QStringLiteral("submenu")] = !kids.isEmpty() || p.value(QStringLiteral("children-display")).toString() == QLatin1String("submenu");
    return n;
}

// The menu bar: the root's children, each with its own items.
inline QVariantList menuBar(const QVariantMap &root)
{
    return convert(root).value(QStringLiteral("children")).toList();
}

}
