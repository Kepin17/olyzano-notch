#pragma once

#include "platform/ApiTesterModel.h"

#include <QJsonDocument>
#include <QString>
#include <QStringList>
#include <QUrl>

// A request as a `curl` command, and a `curl` command as a request. Pure (no files, no sockets), like ApiTesterModel, so tst_core drives it.
namespace CurlModel {

using namespace ApiTesterModel;

// 'it'\''s': the way a POSIX shell takes any text as one word
inline QString shellQuote(const QString &s)
{
    QString q = s;
    q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

inline bool hasHeader(const QVector<KeyValue> &headers, const QString &name)
{
    for (const KeyValue &kv : headers)
        if (kv.enabled && kv.key.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

// `resolve`: fill {{variables}} from `vars` first (a command you can run anywhere), or keep them as written (a command for later).
inline QString toCurl(RequestSpec r, const QVector<KeyValue> &vars, bool resolve)
{
    if (resolve) {
        r.url = substitute(r.url, vars);
        r.headers = substituteAll(r.headers, vars);
        r.query = substituteAll(r.query, vars);
        r.body = substitute(r.body, vars);
        r.auth.token = substitute(r.auth.token, vars);
        r.auth.user = substitute(r.auth.user, vars);
        r.auth.password = substitute(r.auth.password, vars);
    }
    QStringList parts{QStringLiteral("curl")};
    const QString method = r.method.isEmpty() ? QStringLiteral("GET") : r.method.toUpper();
    if (method == QLatin1String("HEAD"))
        parts << QStringLiteral("-I");
    else if (method != QLatin1String("GET"))
        parts << QStringLiteral("-X ") + method;
    parts << shellQuote(buildUrl(r.url, r.query));
    for (const KeyValue &kv : std::as_const(r.headers))
        if (kv.enabled && !kv.key.trimmed().isEmpty())
            parts << QStringLiteral("-H ") + shellQuote(kv.key.trimmed() + QStringLiteral(": ") + kv.value);
    if (r.auth.type == QLatin1String("bearer") && !r.auth.token.isEmpty() && !hasHeader(r.headers, QStringLiteral("Authorization")))
        parts << QStringLiteral("-H ") + shellQuote(QStringLiteral("Authorization: Bearer ") + r.auth.token);
    else if (r.auth.type == QLatin1String("basic"))
        parts << QStringLiteral("-u ") + shellQuote(r.auth.user + QLatin1Char(':') + r.auth.password);
    if (r.bodyMode != QLatin1String("none") && !r.body.isEmpty()) {
        if (!hasHeader(r.headers, QStringLiteral("Content-Type"))) {
            const QString ct = r.bodyMode == QLatin1String("json") ? QStringLiteral("application/json")
                             : r.bodyMode == QLatin1String("form") ? QStringLiteral("application/x-www-form-urlencoded")
                                                                    : QStringLiteral("text/plain; charset=utf-8");
            parts << QStringLiteral("-H ") + shellQuote(QStringLiteral("Content-Type: ") + ct);
        }
        parts << QStringLiteral("--data-raw ") + shellQuote(r.body);
    }
    return parts.join(QStringLiteral(" \\\n  "));
}

// ---- reading a command ----

// Shell-style words: 'single', "double" (with \" \\ \$ \`), $'c-style', \ before a character, \ + newline to continue a line.
// `stop` is set when an unquoted | ; or && ends the command (what follows is not ours).
inline QStringList shellWords(const QString &text, bool *stop = nullptr)
{
    QStringList out;
    QString cur;
    bool inWord = false;
    const int n = text.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\\')) {
            if (i + 1 < n) {
                if (text.at(i + 1) == QLatin1Char('\n')) { ++i; continue; }
                if (text.at(i + 1) == QLatin1Char('\r') && i + 2 < n && text.at(i + 2) == QLatin1Char('\n')) { i += 2; continue; }
                cur += text.at(++i);
                inWord = true;
            }
        } else if (c == QLatin1Char('\'')) {
            const int end = text.indexOf(QLatin1Char('\''), i + 1);
            cur += text.mid(i + 1, (end < 0 ? n : end) - i - 1);
            i = end < 0 ? n : end;
            inWord = true;
        } else if (c == QLatin1Char('$') && i + 1 < n && text.at(i + 1) == QLatin1Char('\'')) {
            i += 2;
            for (; i < n && text.at(i) != QLatin1Char('\''); ++i) {
                if (text.at(i) == QLatin1Char('\\') && i + 1 < n) {
                    const QChar e = text.at(++i);
                    switch (e.unicode()) {
                    case 'n': cur += QLatin1Char('\n'); break;
                    case 't': cur += QLatin1Char('\t'); break;
                    case 'r': cur += QLatin1Char('\r'); break;
                    case 'x': {
                        int v = 0, k = 0;
                        while (k < 2 && i + 1 < n && QChar(text.at(i + 1)).isLetterOrNumber()) {
                            bool ok = false;
                            const int d = QString(text.at(i + 1)).toInt(&ok, 16);
                            if (!ok) break;
                            v = v * 16 + d; ++i; ++k;
                        }
                        cur += QChar(v);
                        break;
                    }
                    default: cur += e; break;
                    }
                } else {
                    cur += text.at(i);
                }
            }
            inWord = true;
        } else if (c == QLatin1Char('"')) {
            for (++i; i < n && text.at(i) != QLatin1Char('"'); ++i) {
                if (text.at(i) == QLatin1Char('\\') && i + 1 < n
                    && QStringLiteral("\"\\$`\n").contains(text.at(i + 1))) {
                    if (text.at(i + 1) != QLatin1Char('\n'))
                        cur += text.at(i + 1);
                    ++i;
                } else {
                    cur += text.at(i);
                }
            }
            inWord = true;
        } else if (c.isSpace()) {
            if (inWord) { out << cur; cur.clear(); inWord = false; }
        } else if (c == QLatin1Char('|') || c == QLatin1Char(';') || (c == QLatin1Char('&') && i + 1 < n && text.at(i + 1) == QLatin1Char('&'))) {
            if (stop) *stop = true;
            break;
        } else {
            cur += c;
            inWord = true;
        }
    }
    if (inWord)
        out << cur;
    return out;
}

struct Parsed {
    RequestSpec request;
    QString error; // "" when the command was understood
};

inline QString urlEncodeForm(const QString &s) { return QString::fromUtf8(QUrl::toPercentEncoding(s)); }

inline Parsed fromCurl(const QString &text)
{
    Parsed res;
    RequestSpec &r = res.request;
    QStringList w = shellWords(text.trimmed());
    if (!w.isEmpty() && w.first() == QLatin1String("$"))
        w.removeFirst();
    if (w.isEmpty() || w.first() != QLatin1String("curl")) {
        res.error = QStringLiteral("Not a curl command");
        return res;
    }
    w.removeFirst();

    static const QStringList takesValue = {
        QStringLiteral("-X"), QStringLiteral("--request"), QStringLiteral("-H"), QStringLiteral("--header"),
        QStringLiteral("-d"), QStringLiteral("--data"), QStringLiteral("--data-raw"), QStringLiteral("--data-binary"), QStringLiteral("--data-ascii"),
        QStringLiteral("--data-urlencode"), QStringLiteral("--json"), QStringLiteral("-u"), QStringLiteral("--user"), QStringLiteral("--url"),
        QStringLiteral("-A"), QStringLiteral("--user-agent"), QStringLiteral("-b"), QStringLiteral("--cookie"), QStringLiteral("-e"), QStringLiteral("--referer"),
        QStringLiteral("-F"), QStringLiteral("--form"), QStringLiteral("-T"), QStringLiteral("--upload-file"),
        // accepted, and ignored: they do not change what is sent
        QStringLiteral("-m"), QStringLiteral("--max-time"), QStringLiteral("--connect-timeout"), QStringLiteral("-o"), QStringLiteral("--output"),
        QStringLiteral("-x"), QStringLiteral("--proxy"), QStringLiteral("-w"), QStringLiteral("--write-out"), QStringLiteral("--retry"),
        QStringLiteral("--cacert"), QStringLiteral("--cert"), QStringLiteral("--key"), QStringLiteral("-c"), QStringLiteral("--cookie-jar")};

    QString method, url;
    QStringList data;       // every -d piece, joined with &
    bool get = false, head = false, jsonOpt = false;
    QVector<KeyValue> headers;
    auto handle = [&](const QString &opt, const QString &val) -> QString {
        if (opt == QLatin1String("-X") || opt == QLatin1String("--request")) method = val.toUpper();
        else if (opt == QLatin1String("-H") || opt == QLatin1String("--header")) {
            const int c = val.indexOf(QLatin1Char(':'));
            if (c > 0) headers.push_back({val.left(c).trimmed(), val.mid(c + 1).trimmed(), true});
        } else if (opt == QLatin1String("-d") || opt == QLatin1String("--data") || opt == QLatin1String("--data-raw")
                   || opt == QLatin1String("--data-binary") || opt == QLatin1String("--data-ascii")) {
            if (opt != QLatin1String("--data-raw") && val.startsWith(QLatin1Char('@')))
                return QStringLiteral("Reading the body from a file (%1) is not supported: paste it instead").arg(val);
            data << val;
        } else if (opt == QLatin1String("--data-urlencode")) {
            const int eq = val.indexOf(QLatin1Char('='));
            data << (eq >= 0 ? val.left(eq + 1) + urlEncodeForm(val.mid(eq + 1)) : urlEncodeForm(val));
        } else if (opt == QLatin1String("--json")) {
            jsonOpt = true;
            data << val;
        } else if (opt == QLatin1String("-u") || opt == QLatin1String("--user")) {
            const int c = val.indexOf(QLatin1Char(':'));
            r.auth.type = QStringLiteral("basic");
            r.auth.user = c < 0 ? val : val.left(c);
            r.auth.password = c < 0 ? QString() : val.mid(c + 1);
        } else if (opt == QLatin1String("--url")) url = val;
        else if (opt == QLatin1String("-A") || opt == QLatin1String("--user-agent")) headers.push_back({QStringLiteral("User-Agent"), val, true});
        else if (opt == QLatin1String("-b") || opt == QLatin1String("--cookie")) headers.push_back({QStringLiteral("Cookie"), val, true});
        else if (opt == QLatin1String("-e") || opt == QLatin1String("--referer")) headers.push_back({QStringLiteral("Referer"), val, true});
        else if (opt == QLatin1String("-F") || opt == QLatin1String("--form") || opt == QLatin1String("-T") || opt == QLatin1String("--upload-file"))
            return QStringLiteral("Multipart forms and file uploads (%1) are not supported yet").arg(opt);
        return {};
    };

    for (int i = 0; i < w.size(); ++i) {
        const QString &t = w.at(i);
        if (t.startsWith(QLatin1String("--"))) {
            const int eq = t.indexOf(QLatin1Char('='));
            const QString opt = eq > 0 ? t.left(eq) : t;
            if (takesValue.contains(opt)) {
                QString val;
                if (eq > 0) val = t.mid(eq + 1);
                else if (i + 1 < w.size()) val = w.at(++i);
                if (const QString err = handle(opt, val); !err.isEmpty()) { res.error = err; return res; }
            } else if (opt == QLatin1String("--get")) get = true;
            else if (opt == QLatin1String("--head")) head = true;
            // anything else (--compressed, --location, --silent, --insecure, …) does not change the request
        } else if (t.startsWith(QLatin1Char('-')) && t.size() > 1) {
            // -sSL, -XPOST, -H'Accept: x', -uuser:pass: cluster of letters, a value-taking one ends it
            for (int k = 1; k < t.size(); ++k) {
                const QString opt = QStringLiteral("-") + t.at(k);
                if (takesValue.contains(opt)) {
                    QString val = t.mid(k + 1);
                    if (val.isEmpty() && i + 1 < w.size())
                        val = w.at(++i);
                    if (const QString err = handle(opt, val); !err.isEmpty()) { res.error = err; return res; }
                    break;
                }
                if (opt == QLatin1String("-G")) get = true;
                else if (opt == QLatin1String("-I")) head = true;
            }
        } else if (url.isEmpty()) {
            url = t;
        }
    }
    if (url.isEmpty()) {
        res.error = QStringLiteral("No URL in the curl command");
        return res;
    }

    QString body = data.join(QLatin1Char('&'));
    if (get && !body.isEmpty()) { // -G: the data goes in the query string
        url += (url.contains(QLatin1Char('?')) ? QStringLiteral("&") : QStringLiteral("?")) + body;
        body.clear();
    }
    r.url = url;
    r.method = !method.isEmpty() ? method : head ? QStringLiteral("HEAD") : (!body.isEmpty() ? QStringLiteral("POST") : QStringLiteral("GET"));
    if (jsonOpt) {
        headers.push_back({QStringLiteral("Content-Type"), QStringLiteral("application/json"), true});
        headers.push_back({QStringLiteral("Accept"), QStringLiteral("application/json"), true});
    }

    // the body mode: what the Content-Type says, else what the text looks like. The type header itself is dropped when the mode
    // already sends it, so a copy of the request round-trips.
    QString ct;
    for (int k = 0; k < headers.size(); ++k)
        if (headers[k].key.compare(QStringLiteral("Content-Type"), Qt::CaseInsensitive) == 0) { ct = headers[k].value.toLower(); }
    if (!body.isEmpty()) {
        r.body = body;
        const QString trimmed = body.trimmed();
        const bool jsonish = (trimmed.startsWith(QLatin1Char('{')) || trimmed.startsWith(QLatin1Char('['))) && !QJsonDocument::fromJson(trimmed.toUtf8()).isNull();
        const bool formish = !body.contains(QLatin1Char(' ')) && body.contains(QLatin1Char('=')) && !jsonish;
        QString mode;
        if (ct.contains(QLatin1String("json"))) mode = QStringLiteral("json");
        else if (ct.contains(QLatin1String("x-www-form-urlencoded"))) mode = QStringLiteral("form");
        else if (ct.startsWith(QLatin1String("text/plain"))) mode = QStringLiteral("raw");
        else if (ct.isEmpty()) mode = jsonish ? QStringLiteral("json") : formish ? QStringLiteral("form") : QStringLiteral("raw");
        else mode = QStringLiteral("raw"); // some other type: keep the header, send the text as is
        r.bodyMode = mode;
        const QString implied = mode == QLatin1String("json") ? QStringLiteral("application/json")
                              : mode == QLatin1String("form") ? QStringLiteral("application/x-www-form-urlencoded")
                              : mode == QLatin1String("raw")  ? QStringLiteral("text/plain; charset=utf-8") : QString();
        for (int k = headers.size() - 1; k >= 0; --k)
            if (headers[k].key.compare(QStringLiteral("Content-Type"), Qt::CaseInsensitive) == 0
                && !implied.isEmpty() && headers[k].value.compare(implied, Qt::CaseInsensitive) == 0)
                headers.removeAt(k);
    }
    r.headers = headers;

    // a name from the path: .../api/v1/members → "members"
    const QUrl u(url);
    QString last = u.path().section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
    r.name = r.method + QLatin1Char(' ') + (last.isEmpty() ? (u.host().isEmpty() ? url : u.host()) : last);
    return res;
}

} // namespace CurlModel
