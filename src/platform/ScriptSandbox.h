#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include "platform/ApiTesterModel.h"

// Pure string builders for the ApiTester JS sandbox preludes. Extracted from ApiTester::runScript
// so the embedded JS lives in one place and the controller stays readable. No QJSEngine here —
// the caller still owns the engine and the member state that the scripts mutate.
namespace ScriptSandbox {

// Shared env + console seed for both pre-request and test scripts.
inline QString commonPrelude(const QJsonObject &varsObject)
{
    const QString envSeed = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("v"), varsObject}}).toJson(QJsonDocument::Compact));
    return QStringLiteral(
               "var __v = %1;\n"
               "var env = {\n"
               "  get: function(k) { return Object.prototype.hasOwnProperty.call(__v, k) ? __v[k] : undefined; },\n"
               "  set: function(k, v) { __v[k] = String(v); },\n"
               "  unset: function(k) { delete __v[k]; }\n"
               "};\n"
               "var console = { log: function() {\n"
               "  var parts = []; for (var i = 0; i < arguments.length; ++i) parts.push(String(arguments[i]));\n"
               "  __log__.push(parts.join(' '));\n"
               "} };\n")
        .arg(envSeed);
}

// response / test / expect helpers — only for isTest scripts.
inline QString testPrelude(const QVariantMap &response)
{
    const QString bodyJson =
        QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("b"), QJsonValue::fromVariant(response.value(QStringLiteral("body")))}})
                              .toJson(QJsonDocument::Compact));
    const QString hdrJson =
        QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("h"), QJsonValue::fromVariant(response.value(QStringLiteral("headers")))}})
                              .toJson(QJsonDocument::Compact));
    return QStringLiteral(
               "var __b = %1;\n"
               "var __h = %2;\n"
               "var response = {\n"
               "  status: %3, time: %4,\n"
               "  get ok() { return this.status >= 200 && this.status < 300; },\n"
               "  headers: {\n"
               "    get: function(n) { n = String(n).toLowerCase(); for (var i = 0; i < __h.length; ++i) if (__h[i].key.toLowerCase() === n) return __h[i].value; return null; },\n"
               "    all: function() { return __h; }\n"
               "  },\n"
               "  text: function() { return __b; },\n"
               "  json: function() { return JSON.parse(__b); }\n"
               "};\n"
               "function test(name, fn) {\n"
               "  try { fn(); __tests__.push({ name: name, passed: true, message: '' }); }\n"
               "  catch (e) { __tests__.push({ name: name, passed: false, message: String(e) }); }\n"
               "}\n"
               "function expect(actual) {\n"
               "  var a = actual;\n"
               "  var api = {\n"
               "    toBe: function(v) { if (a !== v) throw new Error('expected ' + JSON.stringify(v) + ' got ' + JSON.stringify(a)); },\n"
               "    toBeTruthy: function() { if (!a) throw new Error('expected truthy'); },\n"
               "    toBeFalsy: function() { if (a) throw new Error('expected falsy'); },\n"
               "    toBeNull: function() { if (a !== null) throw new Error('expected null'); },\n"
               "    toBeDefined: function() { if (a === undefined) throw new Error('expected defined'); },\n"
               "    toBeUndefined: function() { if (a !== undefined) throw new Error('expected undefined'); },\n"
               "    toContain: function(s) { if (typeof a === 'string' ? a.indexOf(s) < 0 : !a || a.indexOf(s) < 0) throw new Error('expected to contain ' + s); },\n"
               "    toBeGreaterThan: function(n) { if (!(a > n)) throw new Error('expected > ' + n); },\n"
               "    toBeLessThan: function(n) { if (!(a < n)) throw new Error('expected < ' + n); },\n"
               "    toBeType: function(t) { var got = Array.isArray(a) ? 'array' : typeof a; if (got !== t) throw new Error('expected ' + t + ' got ' + got); }\n"
               "  };\n"
               "  api.not = { toBe: function(v) { if (a === v) throw new Error('expected not ' + JSON.stringify(v)); } };\n"
               "  return api;\n"
               "}\n")
        .arg(bodyJson, hdrJson)
        .arg(response.value(QStringLiteral("status")).toInt())
        .arg(response.value(QStringLiteral("durationMs")).toLongLong());
}

} // namespace ScriptSandbox
