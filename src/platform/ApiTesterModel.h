#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QUrl>
#include <QVector>
#include <algorithm>

// Pure data + logic for the API tester (like TasksModel): no files, no sockets, no QObject — so tst_core can
// drive round-trips, {{var}} substitution, the history ring and Postman v2.1 mapping without the controller.
namespace ApiTesterModel {

struct KeyValue {
    QString key, value;
    bool enabled = true;
    bool secret = false; // environment variables only: the value is hidden in the editor
};

struct AuthSpec {
    QString type = QStringLiteral("none"); // none | bearer | basic
    QString token;
    QString user, password;
};

struct RequestSpec {
    QString id, name;
    QString method = QStringLiteral("GET");
    QString url;
    QVector<KeyValue> headers, query;
    QString bodyMode = QStringLiteral("none"); // none | json | raw | form
    QString body;
    AuthSpec auth;
    QString preScript, testScript, description;
};

struct RequestNode {
    QString type; // folder | request
    QString id, name;
    RequestSpec request;               // when type == "request"
    QList<RequestNode> items;          // when type == "folder"
};

struct Collection {
    QString id, name;
    QList<RequestNode> items;
};

struct Environment {
    QString id, name;
    QVector<KeyValue> variables;
};

struct TestResult {
    QString name, message;
    bool passed = false;
};

struct HistoryEntry {
    QString id, atIso, method, url, reqHeaders, resHeaders, body, error;
    int status = 0;
    qint64 durationMs = 0;
    bool bodyTruncated = false;
    QList<TestResult> tests;
};

inline QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// ---- {{var}} substitution over enabled key/values; unknown keys stay literal ----

inline QString substitute(const QString &text, const QVector<KeyValue> &vars)
{
    QString out = text;
    int guard = 0;
    static const QRegularExpression re(QStringLiteral(R"(\{\{\s*([A-Za-z0-9_.-]+)\s*\}\})"));
    // QRegularExpression lives in QtCore — include here for header-only use.
    for (int pass = 0; pass < 8; ++pass) {
        bool any = false;
        QRegularExpressionMatchIterator it = re.globalMatch(out);
        QString result;
        int last = 0;
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            result += out.mid(last, m.capturedStart() - last);
            const QString key = m.captured(1);
            const KeyValue *hit = nullptr;
            for (const KeyValue &kv : vars)
                if (kv.enabled && kv.key == key) {
                    hit = &kv;
                    break;
                }
            if (hit) {
                result += hit->value;
                any = true;
            } else {
                result += m.captured(0);
            }
            last = m.capturedEnd();
        }
        result += out.mid(last);
        out = result;
        if (!any || ++guard > 8)
            break;
    }
    return out;
}

inline QVector<KeyValue> substituteAll(const QVector<KeyValue> &list, const QVector<KeyValue> &vars)
{
    QVector<KeyValue> out;
    out.reserve(list.size());
    for (const KeyValue &kv : list) {
        KeyValue n = kv;
        n.value = substitute(n.value, vars);
        n.key = substitute(n.key, vars);
        out.push_back(n);
    }
    return out;
}

inline bool hasUnresolved(const QString &text, const QVector<KeyValue> &vars)
{
    static const QRegularExpression re(QStringLiteral(R"(\{\{\s*([A-Za-z0-9_.-]+)\s*\}\})"));
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QString key = it.next().captured(1);
        bool found = false;
        for (const KeyValue &kv : vars)
            if (kv.enabled && kv.key == key) {
                found = true;
                break;
            }
        if (!found)
            return true;
    }
    return false;
}

// ---- URL assembly: query string of enabled pairs, skip empties ----

inline QString buildUrl(QString base, const QVector<KeyValue> &query)
{
    QStringList enc;
    for (const KeyValue &kv : query) {
        if (!kv.enabled || kv.key.isEmpty())
            continue;
        enc << QUrl::toPercentEncoding(kv.key) + QLatin1Char('=') + QUrl::toPercentEncoding(kv.value);
    }
    if (enc.isEmpty())
        return base;
    const QString join = base.contains(QLatin1Char('?'))
                             ? (base.endsWith(QLatin1Char('?')) || base.endsWith(QLatin1Char('&')) ? QStringLiteral("") : QStringLiteral("&"))
                             : QStringLiteral("?");
    return base + join + enc.join(QLatin1Char('&'));
}

// ---- history ring: newest first, drop past limit ----

inline void historyPush(QList<HistoryEntry> &list, HistoryEntry entry, int limit)
{
    if (limit < 1)
        limit = 1;
    entry.id = entry.id.isEmpty() ? newId() : entry.id;
    list.prepend(std::move(entry));
    while (list.size() > limit)
        list.removeLast();
}

// ---- pretty JSON when parseable; otherwise the original bytes as text ----

inline QString prettyJson(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject() || doc.isArray())
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    return QString::fromUtf8(body);
}

// ---- the tree: collections hold folders and requests; every operation works on ids ----

inline QString nodeId(const RequestNode &n) { return n.type == QLatin1String("folder") ? n.id : n.request.id; }

inline void reassignIds(RequestNode &n) // a copy gets new ids all the way down
{
    n.id = newId();
    if (n.type == QLatin1String("request"))
        n.request.id = n.id;
    for (RequestNode &c : n.items)
        reassignIds(c);
}

inline bool subtreeHas(const RequestNode &n, const QString &id)
{
    if (nodeId(n) == id)
        return true;
    for (const RequestNode &c : n.items)
        if (subtreeHas(c, id))
            return true;
    return false;
}

struct Spot {
    QList<RequestNode> *list = nullptr; // the list the node sits in (a collection's or a folder's items)
    int index = -1;
    explicit operator bool() const { return list != nullptr; }
};

inline Spot locate(QList<RequestNode> &items, const QString &id)
{
    for (int i = 0; i < items.size(); ++i) {
        if (nodeId(items[i]) == id)
            return {&items, i};
        if (items[i].type == QLatin1String("folder"))
            if (Spot s = locate(items[i].items, id))
                return s;
    }
    return {};
}

inline Spot locate(QList<Collection> &cols, const QString &id)
{
    for (Collection &c : cols)
        if (Spot s = locate(c.items, id))
            return s;
    return {};
}

// The list a new node goes into: a collection's own items, or a folder's. `parentId` is a collection id or a folder id.
inline QList<RequestNode> *containerFor(QList<Collection> &cols, const QString &parentId)
{
    for (Collection &c : cols)
        if (c.id == parentId)
            return &c.items;
    const Spot s = locate(cols, parentId);
    if (s && (*s.list)[s.index].type == QLatin1String("folder"))
        return &(*s.list)[s.index].items;
    return nullptr;
}

inline QString addFolder(QList<Collection> &cols, const QString &parentId, const QString &name)
{
    QList<RequestNode> *into = containerFor(cols, parentId);
    if (!into)
        return {};
    RequestNode f;
    f.type = QStringLiteral("folder");
    f.id = newId();
    f.name = name.trimmed().isEmpty() ? QStringLiteral("New folder") : name.trimmed();
    into->push_back(f);
    return f.id;
}

inline QString addRequestNode(QList<Collection> &cols, const QString &parentId, const QString &name = QString())
{
    QList<RequestNode> *into = containerFor(cols, parentId);
    if (!into)
        return {};
    RequestNode n;
    n.type = QStringLiteral("request");
    n.id = newId();
    n.name = name.trimmed().isEmpty() ? QStringLiteral("New request") : name.trimmed();
    n.request.id = n.id;
    n.request.name = n.name;
    into->push_back(n);
    return n.id;
}

inline bool renameNode(QList<Collection> &cols, const QString &id, const QString &name)
{
    const QString nm = name.trimmed();
    if (nm.isEmpty())
        return false;
    for (Collection &c : cols)
        if (c.id == id) {
            c.name = nm;
            return true;
        }
    const Spot s = locate(cols, id);
    if (!s)
        return false;
    RequestNode &n = (*s.list)[s.index];
    n.name = nm;
    if (n.type == QLatin1String("request"))
        n.request.name = nm;
    return true;
}

inline bool removeNode(QList<Collection> &cols, const QString &id)
{
    for (int i = 0; i < cols.size(); ++i)
        if (cols[i].id == id) {
            cols.removeAt(i);
            return true;
        }
    const Spot s = locate(cols, id);
    if (!s)
        return false;
    s.list->removeAt(s.index);
    return true;
}

// A copy right after the original, with new ids and " copy" on its name. -> the copy's id, "" when there is no such node.
inline QString duplicateNode(QList<Collection> &cols, const QString &id)
{
    for (int i = 0; i < cols.size(); ++i)
        if (cols[i].id == id) {
            Collection c = cols[i];
            c.id = newId();
            c.name += QStringLiteral(" copy");
            for (RequestNode &n : c.items)
                reassignIds(n);
            cols.insert(i + 1, c);
            return c.id;
        }
    const Spot s = locate(cols, id);
    if (!s)
        return {};
    RequestNode copy = (*s.list)[s.index];
    reassignIds(copy);
    copy.name += QStringLiteral(" copy");
    if (copy.type == QLatin1String("request"))
        copy.request.name = copy.name;
    s.list->insert(s.index + 1, copy);
    return nodeId(copy);
}

// Move a folder or request into another collection or folder, at `index` (-1 = the end). A folder cannot go into itself or
// into something inside it. -> false (and nothing changes) when it cannot.
inline bool moveNode(QList<Collection> &cols, const QString &id, const QString &parentId, int index = -1)
{
    Spot from = locate(cols, id);
    if (!from)
        return false;
    const RequestNode node = (*from.list)[from.index];
    if (subtreeHas(node, parentId))
        return false;
    if (!containerFor(cols, parentId))
        return false;
    from.list->removeAt(from.index);
    QList<RequestNode> *into = containerFor(cols, parentId); // looked up again: removing moved the lists around; it exists, it was checked
    into->insert(index < 0 || index > into->size() ? into->size() : index, node);
    return true;
}

// ---- JSON (de)serialization ----

inline QJsonValue kvToJson(const QVector<KeyValue> &list)
{
    QJsonArray a;
    for (const KeyValue &kv : list)
        a << QJsonObject{{QStringLiteral("key"), kv.key}, {QStringLiteral("value"), kv.value}, {QStringLiteral("enabled"), kv.enabled},
                         {QStringLiteral("secret"), kv.secret}};
    return a;
}

inline QVector<KeyValue> kvFromJson(const QJsonValue &v)
{
    QVector<KeyValue> out;
    for (const QJsonValue &x : v.toArray()) {
        const QJsonObject o = x.toObject();
        KeyValue kv;
        kv.key = o.value(QStringLiteral("key")).toString();
        kv.value = o.value(QStringLiteral("value")).toString();
        kv.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        kv.secret = o.value(QStringLiteral("secret")).toBool(false);
        out.push_back(kv);
    }
    return out;
}

inline QJsonObject requestToJson(const RequestSpec &r)
{
    QJsonObject o{
        {QStringLiteral("type"), QStringLiteral("request")},
        {QStringLiteral("id"), r.id},
        {QStringLiteral("name"), r.name},
        {QStringLiteral("method"), r.method},
        {QStringLiteral("url"), r.url},
        {QStringLiteral("headers"), kvToJson(r.headers)},
        {QStringLiteral("query"), kvToJson(r.query)},
        {QStringLiteral("bodyMode"), r.bodyMode},
        {QStringLiteral("body"), r.body},
        {QStringLiteral("auth"),
         QJsonObject{{QStringLiteral("type"), r.auth.type},
                     {QStringLiteral("token"), r.auth.token},
                     {QStringLiteral("user"), r.auth.user},
                     {QStringLiteral("password"), r.auth.password}}},
        {QStringLiteral("preScript"), r.preScript},
        {QStringLiteral("testScript"), r.testScript},
        {QStringLiteral("description"), r.description},
    };
    return o;
}

inline RequestSpec requestFromJson(const QJsonObject &o)
{
    RequestSpec r;
    r.id = o.value(QStringLiteral("id")).toString();
    r.name = o.value(QStringLiteral("name")).toString();
    r.method = o.value(QStringLiteral("method")).toString(QStringLiteral("GET"));
    r.url = o.value(QStringLiteral("url")).toString();
    r.headers = kvFromJson(o.value(QStringLiteral("headers")));
    r.query = kvFromJson(o.value(QStringLiteral("query")));
    r.bodyMode = o.value(QStringLiteral("bodyMode")).toString(QStringLiteral("none"));
    r.body = o.value(QStringLiteral("body")).toString();
    const QJsonObject auth = o.value(QStringLiteral("auth")).toObject();
    r.auth.type = auth.value(QStringLiteral("type")).toString(QStringLiteral("none"));
    r.auth.token = auth.value(QStringLiteral("token")).toString();
    r.auth.user = auth.value(QStringLiteral("user")).toString();
    r.auth.password = auth.value(QStringLiteral("password")).toString();
    r.preScript = o.value(QStringLiteral("preScript")).toString();
    r.testScript = o.value(QStringLiteral("testScript")).toString();
    r.description = o.value(QStringLiteral("description")).toString();
    return r;
}

inline QJsonObject nodeToJson(const RequestNode &n)
{
    if (n.type == QLatin1String("folder")) {
        QJsonArray kids;
        for (const RequestNode &c : n.items)
            kids << nodeToJson(c);
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("folder")},
                           {QStringLiteral("id"), n.id},
                           {QStringLiteral("name"), n.name},
                           {QStringLiteral("items"), kids}};
    }
    return requestToJson(n.request);
}

inline RequestNode nodeFromJson(const QJsonObject &o)
{
    RequestNode n;
    n.type = o.value(QStringLiteral("type")).toString(QStringLiteral("request"));
    n.id = o.value(QStringLiteral("id")).toString();
    n.name = o.value(QStringLiteral("name")).toString();
    if (n.type == QLatin1String("folder")) {
        for (const QJsonValue &c : o.value(QStringLiteral("items")).toArray())
            n.items.push_back(nodeFromJson(c.toObject()));
    } else {
        n.request = requestFromJson(o);
        if (n.request.id.isEmpty())
            n.request.id = n.id;
        if (n.request.name.isEmpty())
            n.request.name = n.name;
    }
    return n;
}

inline QJsonObject collectionToJson(const Collection &c)
{
    QJsonArray kids;
    for (const RequestNode &n : c.items)
        kids << nodeToJson(n);
    return QJsonObject{{QStringLiteral("id"), c.id},
                       {QStringLiteral("name"), c.name},
                       {QStringLiteral("items"), kids}};
}

inline Collection collectionFromJson(const QJsonObject &o)
{
    Collection c;
    c.id = o.value(QStringLiteral("id")).toString();
    c.name = o.value(QStringLiteral("name")).toString();
    for (const QJsonValue &n : o.value(QStringLiteral("items")).toArray())
        c.items.push_back(nodeFromJson(n.toObject()));
    return c;
}

inline QByteArray collectionsToJson(const QList<Collection> &list)
{
    QJsonArray a;
    for (const Collection &c : list)
        a << collectionToJson(c);
    return QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("collections"), a}}).toJson(QJsonDocument::Indented);
}

inline QList<Collection> collectionsFromJson(const QByteArray &bytes)
{
    QList<Collection> out;
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    for (const QJsonValue &c : root.value(QStringLiteral("collections")).toArray()) {
        if (c.toObject().value(QStringLiteral("readonly")).toBool())
            continue; // the built-in templates older versions saved: gone, every collection is the person's own now
        out.push_back(collectionFromJson(c.toObject()));
    }
    return out;
}

inline QJsonObject environmentToJson(const Environment &e)
{
    return QJsonObject{{QStringLiteral("id"), e.id}, {QStringLiteral("name"), e.name}, {QStringLiteral("variables"), kvToJson(e.variables)}};
}

inline Environment environmentFromJson(const QJsonObject &o)
{
    Environment e;
    e.id = o.value(QStringLiteral("id")).toString();
    e.name = o.value(QStringLiteral("name")).toString();
    e.variables = kvFromJson(o.value(QStringLiteral("variables")));
    return e;
}

inline QByteArray environmentsToJson(const QList<Environment> &list, const QString &activeId)
{
    QJsonArray a;
    for (const Environment &e : list)
        a << environmentToJson(e);
    return QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                     {QStringLiteral("activeId"), activeId},
                                     {QStringLiteral("environments"), a}})
        .toJson(QJsonDocument::Indented);
}

inline QList<Environment> environmentsFromJson(const QByteArray &bytes, QString *activeId)
{
    QList<Environment> out;
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    if (activeId)
        *activeId = root.value(QStringLiteral("activeId")).toString();
    for (const QJsonValue &e : root.value(QStringLiteral("environments")).toArray())
        out.push_back(environmentFromJson(e.toObject()));
    return out;
}

inline QByteArray historyToJson(const QList<HistoryEntry> &list)
{
    QJsonArray a;
    for (const HistoryEntry &h : list) {
        QJsonArray tests;
        for (const TestResult &t : h.tests)
            tests << QJsonObject{{QStringLiteral("name"), t.name}, {QStringLiteral("passed"), t.passed}, {QStringLiteral("message"), t.message}};
        a << QJsonObject{{QStringLiteral("id"), h.id},
                         {QStringLiteral("at"), h.atIso},
                         {QStringLiteral("method"), h.method},
                         {QStringLiteral("url"), h.url},
                         {QStringLiteral("status"), h.status},
                         {QStringLiteral("durationMs"), h.durationMs},
                         {QStringLiteral("reqHeaders"), h.reqHeaders},
                         {QStringLiteral("resHeaders"), h.resHeaders},
                         {QStringLiteral("body"), h.body},
                         {QStringLiteral("bodyTruncated"), h.bodyTruncated},
                         {QStringLiteral("error"), h.error},
                         {QStringLiteral("tests"), tests}};
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("entries"), a}}).toJson(QJsonDocument::Indented);
}

inline QList<HistoryEntry> historyFromJson(const QByteArray &bytes)
{
    QList<HistoryEntry> out;
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    for (const QJsonValue &x : root.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject o = x.toObject();
        HistoryEntry h;
        h.id = o.value(QStringLiteral("id")).toString();
        h.atIso = o.value(QStringLiteral("at")).toString();
        h.method = o.value(QStringLiteral("method")).toString();
        h.url = o.value(QStringLiteral("url")).toString();
        h.status = o.value(QStringLiteral("status")).toInt();
        h.durationMs = static_cast<qint64>(o.value(QStringLiteral("durationMs")).toDouble());
        h.reqHeaders = o.value(QStringLiteral("reqHeaders")).toString();
        h.resHeaders = o.value(QStringLiteral("resHeaders")).toString();
        h.body = o.value(QStringLiteral("body")).toString();
        h.bodyTruncated = o.value(QStringLiteral("bodyTruncated")).toBool();
        h.error = o.value(QStringLiteral("error")).toString();
        for (const QJsonValue &t : o.value(QStringLiteral("tests")).toArray()) {
            const QJsonObject to = t.toObject();
            h.tests << TestResult{to.value(QStringLiteral("name")).toString(), to.value(QStringLiteral("message")).toString(),
                                  to.value(QStringLiteral("passed")).toBool()};
        }
        out.push_back(h);
    }
    return out;
}

// ---- Postman Collection v2.1 ----

inline QJsonValue postmanUrl(const QString &raw)
{
    // Prefer raw string; object form is produced on export only when simple.
    return raw;
}

inline QString postmanUrlString(const QJsonValue &u)
{
    if (u.isString())
        return u.toString();
    if (u.isObject()) {
        const QJsonObject o = u.toObject();
        if (o.contains(QStringLiteral("raw")))
            return o.value(QStringLiteral("raw")).toString();
        const QString proto = o.value(QStringLiteral("protocol")).toString();
        QStringList host;
        for (const QJsonValue &h : o.value(QStringLiteral("host")).toArray())
            host << h.toString();
        QStringList path;
        for (const QJsonValue &p : o.value(QStringLiteral("path")).toArray())
            path << p.toString();
        QString s = proto.isEmpty() ? QString() : proto + QStringLiteral("://");
        s += host.join(QLatin1Char('.'));
        if (!path.isEmpty())
            s += QLatin1Char('/') + path.join(QLatin1Char('/'));
        return s;
    }
    return {};
}

inline RequestNode requestNodeFromPostmanItem(const QJsonObject &item)
{
    RequestNode n;
    n.id = newId();
    n.name = item.value(QStringLiteral("name")).toString();
    if (item.contains(QStringLiteral("item"))) {
        n.type = QStringLiteral("folder");
        for (const QJsonValue &c : item.value(QStringLiteral("item")).toArray())
            n.items.push_back(requestNodeFromPostmanItem(c.toObject()));
        return n;
    }
    n.type = QStringLiteral("request");
    const QJsonValue rv = item.value(QStringLiteral("request"));
    QJsonObject ro = rv.isString() ? QJsonObject() : rv.toObject();
    n.request.id = n.id;
    n.request.name = n.name;
    n.request.method = ro.value(QStringLiteral("method")).toString(QStringLiteral("GET"));
    n.request.url = postmanUrlString(ro.value(QStringLiteral("url")));
    for (const QJsonValue &h : ro.value(QStringLiteral("header")).toArray()) {
        const QJsonObject ho = h.toObject();
        if (ho.value(QStringLiteral("disabled")).toBool())
            continue;
        n.request.headers << KeyValue{ho.value(QStringLiteral("key")).toString(), ho.value(QStringLiteral("value")).toString(), true};
    }
    const QJsonObject body = ro.value(QStringLiteral("body")).toObject();
    const QString mode = body.value(QStringLiteral("mode")).toString();
    if (mode == QLatin1String("raw")) {
        n.request.bodyMode = QStringLiteral("raw");
        n.request.body = body.value(QStringLiteral("raw")).toString();
        if (body.value(QStringLiteral("options")).toObject().value(QStringLiteral("raw")).toObject().value(QStringLiteral("language")).toString()
            == QLatin1String("json"))
            n.request.bodyMode = QStringLiteral("json");
    } else if (mode == QLatin1String("urlencoded")) {
        n.request.bodyMode = QStringLiteral("form");
        for (const QJsonValue &f : body.value(QStringLiteral("urlencoded")).toArray())
            n.request.body += (n.request.body.isEmpty() ? QString() : QStringLiteral("&")) + f.toObject().value(QStringLiteral("key")).toString()
                              + QLatin1Char('=') + f.toObject().value(QStringLiteral("value")).toString();
    }
    const QJsonObject auth = ro.value(QStringLiteral("auth")).toObject();
    const QString at = auth.value(QStringLiteral("type")).toString();
    if (at == QLatin1String("bearer"))
        n.request.auth = {QStringLiteral("bearer"), auth.value(QStringLiteral("bearer")).toObject().value(QStringLiteral("token")).toString(), {}, {}};
    else if (at == QLatin1String("basic")) {
        const QJsonObject b = auth.value(QStringLiteral("basic")).toObject();
        n.request.auth = {QStringLiteral("basic"), {}, b.value(QStringLiteral("username")).toString(), b.value(QStringLiteral("password")).toString()};
    }
    for (const QJsonValue &ev : item.value(QStringLiteral("event")).toArray()) {
        const QJsonObject eo = ev.toObject();
        const QString listen = eo.value(QStringLiteral("listen")).toString();
        const QString script = eo.value(QStringLiteral("script")).toObject().value(QStringLiteral("exec")).toVariant().toStringList().join(QLatin1Char('\n'));
        if (listen == QLatin1String("prerequest"))
            n.request.preScript = script;
        else if (listen == QLatin1String("test"))
            n.request.testScript = script;
    }
    return n;
}

inline Collection collectionFromPostman(const QJsonObject &root)
{
    Collection c;
    c.id = newId();
    c.name = root.value(QStringLiteral("info")).toObject().value(QStringLiteral("name")).toString(QStringLiteral("Imported"));
    for (const QJsonValue &item : root.value(QStringLiteral("item")).toArray())
        c.items.push_back(requestNodeFromPostmanItem(item.toObject()));
    return c;
}

inline Collection collectionFromPostman(const QByteArray &bytes, bool *ok)
{
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    const bool valid = root.contains(QStringLiteral("info")) || root.contains(QStringLiteral("item"));
    if (ok)
        *ok = valid;
    if (!valid)
        return {};
    return collectionFromPostman(root);
}

inline QJsonObject itemFromNode(const RequestNode &n)
{
    if (n.type == QLatin1String("folder")) {
        QJsonArray kids;
        for (const RequestNode &c : n.items)
            kids << itemFromNode(c);
        return QJsonObject{{QStringLiteral("name"), n.name}, {QStringLiteral("item"), kids}};
    }
    const RequestSpec &r = n.request;
    QJsonArray headers;
    for (const KeyValue &h : r.headers)
        headers << QJsonObject{{QStringLiteral("key"), h.key}, {QStringLiteral("value"), h.value}, {QStringLiteral("disabled"), !h.enabled}};
    QJsonObject body;
    if (r.bodyMode == QLatin1String("json") || r.bodyMode == QLatin1String("raw")) {
        body = QJsonObject{{QStringLiteral("mode"), QStringLiteral("raw")},
                           {QStringLiteral("raw"), r.body},
                           {QStringLiteral("options"),
                            QJsonObject{{QStringLiteral("raw"),
                                         QJsonObject{{QStringLiteral("language"), r.bodyMode == QLatin1String("json") ? QStringLiteral("json") : QStringLiteral("text")}}}}}};
    } else if (r.bodyMode == QLatin1String("form")) {
        QJsonArray enc;
        for (const QString &pair : r.body.split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
            const int eq = pair.indexOf(QLatin1Char('='));
            enc << QJsonObject{{QStringLiteral("key"), eq < 0 ? pair : pair.left(eq)},
                               {QStringLiteral("value"), eq < 0 ? QString() : pair.mid(eq + 1)}};
        }
        body = QJsonObject{{QStringLiteral("mode"), QStringLiteral("urlencoded")}, {QStringLiteral("urlencoded"), enc}};
    }
    QJsonObject request{{QStringLiteral("method"), r.method}, {QStringLiteral("url"), postmanUrl(r.url)}, {QStringLiteral("header"), headers}};
    if (!body.isEmpty())
        request.insert(QStringLiteral("body"), body);
    if (r.auth.type != QLatin1String("none")) {
        if (r.auth.type == QLatin1String("bearer"))
            request.insert(QStringLiteral("auth"),
                           QJsonObject{{QStringLiteral("type"), QStringLiteral("bearer")},
                                       {QStringLiteral("bearer"), QJsonObject{{QStringLiteral("token"), r.auth.token}}}});
        else
            request.insert(QStringLiteral("auth"),
                           QJsonObject{{QStringLiteral("type"), QStringLiteral("basic")},
                                       {QStringLiteral("basic"),
                                        QJsonObject{{QStringLiteral("username"), r.auth.user}, {QStringLiteral("password"), r.auth.password}}}});
    }
    QJsonArray events;
    if (!r.preScript.isEmpty())
        events << QJsonObject{{QStringLiteral("listen"), QStringLiteral("prerequest")},
                              {QStringLiteral("script"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text/javascript")},
                                                                      {QStringLiteral("exec"), QJsonArray{r.preScript}}}}};
    if (!r.testScript.isEmpty())
        events << QJsonObject{{QStringLiteral("listen"), QStringLiteral("test")},
                              {QStringLiteral("script"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text/javascript")},
                                                                      {QStringLiteral("exec"), QJsonArray{r.testScript}}}}};
    QJsonObject item{{QStringLiteral("name"), r.name.isEmpty() ? n.name : r.name}, {QStringLiteral("request"), request}};
    if (!events.isEmpty())
        item.insert(QStringLiteral("event"), events);
    return item;
}

inline QByteArray collectionToPostman(const Collection &c)
{
    QJsonArray items;
    for (const RequestNode &n : c.items)
        items << itemFromNode(n);
    QJsonObject info{{QStringLiteral("name"), c.name},
                     {QStringLiteral("schema"),
                      QStringLiteral("https://schema.getpostman.com/json/collection/v2.1.0/collection.json")}};
    return QJsonDocument(QJsonObject{{QStringLiteral("info"), info}, {QStringLiteral("item"), items}}).toJson(QJsonDocument::Indented);
}

inline Environment environmentFromPostman(const QByteArray &bytes, bool *ok)
{
    const QJsonObject o = QJsonDocument::fromJson(bytes).object();
    const bool valid = o.value(QStringLiteral("_postman_variable_scope")).toString() == QLatin1String("environment")
                       || o.contains(QStringLiteral("values"));
    if (ok)
        *ok = valid;
    Environment e;
    e.id = newId();
    e.name = o.value(QStringLiteral("name")).toString(QStringLiteral("Imported"));
    for (const QJsonValue &v : o.value(QStringLiteral("values")).toArray()) {
        const QJsonObject vo = v.toObject();
        e.variables << KeyValue{vo.value(QStringLiteral("key")).toString(), vo.value(QStringLiteral("value")).toString(),
                                vo.value(QStringLiteral("enabled")).toBool(true)};
    }
    return e;
}

inline QByteArray environmentToPostman(const Environment &e)
{
    QJsonArray values;
    for (const KeyValue &kv : e.variables)
        values << QJsonObject{{QStringLiteral("key"), kv.key}, {QStringLiteral("value"), kv.value}, {QStringLiteral("enabled"), kv.enabled}};
    return QJsonDocument(QJsonObject{{QStringLiteral("id"), e.id},
                                     {QStringLiteral("name"), e.name},
                                     {QStringLiteral("values"), values},
                                     {QStringLiteral("_postman_variable_scope"), QStringLiteral("environment")},
                                     {QStringLiteral("_postman_exported_at"),
                                      QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}})
        .toJson(QJsonDocument::Indented);
}

} // namespace ApiTesterModel
