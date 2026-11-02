#include "platform/AgentCommands.h"
#include "platform/AgentStore.h"
#ifdef Q_OS_LINUX
#include <malloc.h>
#endif
#include "platform/AgentTools.h"
#include "platform/CodingAgent.h"
#include "platform/MarkdownHtml.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace CodingAgentModel;

namespace {
QTemporaryDir &testHome()
{
    static QTemporaryDir home(QDir::tempPath() + "/tst-codingagent-XXXXXX");
    return home;
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray json(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject deltaEvent(const QJsonObject &delta)
{
    return {{"choices", QJsonArray{QJsonObject{{"delta", delta}}}}};
}

QJsonObject toolDelta(int index, const QString &id, const QString &name, const QString &arguments)
{
    return {{"index", index}, {"id", id}, {"type", "function"},
            {"function", QJsonObject{{"name", name}, {"arguments", arguments}}}};
}

QByteArray toolResponse(const QJsonArray &tools)
{
    return "data: " + json(deltaEvent({{"tool_calls", tools}}))
        + "\r\n\r\ndata: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\r\n\r\n"
          "data: [DONE]\r\n\r\n";
}

QStringList toolNames(const QJsonArray &tools, bool anthropic)
{
    QStringList names;
    for (const auto &value : tools) {
        const auto object = value.toObject();
        names.append((anthropic ? object : object.value("function").toObject()).value("name").toString());
    }
    names.sort();
    return names;
}

QVariantMap customProvider(const QString &baseUrl)
{
    return {{"name", "Local test provider"}, {"protocol", "openai"},
            {"baseUrl", baseUrl}, {"modelId", "test-model"}, {"authHeader", "none"}};
}

// A real HTTP endpoint: capture complete requests, then split SSE across TCP writes.
// Unscripted follow-up turns terminate normally instead of creating an agent loop.
class FakeServer : public QObject
{
public:
    struct Request {
        QByteArray headers;
        QJsonObject body;
    };
    QTcpServer server;
    QList<Request> requests;
    QList<QByteArray> responses;
    QList<int> statuses;   // HTTP status per response (default 200)
    QList<int> retryAfter; // Retry-After seconds per response (default none)
    int fragmentSize = 0;

    FakeServer()
    {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer = QByteArray()]() mutable {
                    buffer += socket->readAll();
                    const auto end = buffer.indexOf("\r\n\r\n");
                    if (end < 0 || socket->property("answered").toBool())
                        return;
                    const QByteArray headers = buffer.left(end);
                    qint64 length = 0;
                    for (const auto &line : headers.split('\n')) {
                        if (line.trimmed().toLower().startsWith("content-length:"))
                            length = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
                    }
                    if (buffer.size() - end - 4 < length)
                        return;
                    socket->setProperty("answered", true);
                    const int responseIndex = requests.size();
                    requests.append({headers, QJsonDocument::fromJson(buffer.mid(end + 4, length)).object()});
                    const QByteArray body = responseIndex < responses.size() ? responses[responseIndex]
                        : QByteArray("data: {\"choices\":[{\"delta\":{\"content\":\"Finished\"}}]}\n\n"
                                     "data: [DONE]\n\n");
                    const int code = responseIndex < statuses.size() ? statuses[responseIndex] : 200;
                    socket->write("HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : " Error") + "\r\nContent-Type: text/event-stream\r\nConnection: close\r\n"
                                  + (responseIndex < retryAfter.size() && retryAfter[responseIndex] > 0 ? "Retry-After: " + QByteArray::number(retryAfter[responseIndex]) + "\r\n" : QByteArray())
                                  + "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n");
                    const int size = fragmentSize > 0 ? fragmentSize : int(body.size());
                    int delay = 0;
                    for (qsizetype offset = 0; offset < body.size(); offset += size) {
                        const QByteArray part = body.mid(offset, size);
                        const bool last = offset + size >= body.size();
                        QTimer::singleShot(++delay * 2, socket, [socket, part, last] {
                            socket->write(part);
                            if (last)
                                socket->disconnectFromHost();
                        });
                    }
                });
            }
        });
    }

    bool listen() { return server.listen(QHostAddress::LocalHost, 0); }
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()); }
};
}

class tst_CodingAgent : public QObject
{
    Q_OBJECT
public:
    // QTEST_GUILESS_MAIN calls this before constructing QCoreApplication.
    static void initMain()
    {
        auto &home = testHome();
        if (!home.isValid())
            qFatal("Cannot create isolated CodingAgent test home");
        const QString config = home.path() + "/config";
        const QString data = home.path() + "/data";
        if (!QDir().mkpath(config) || !QDir().mkpath(data)
            || !qputenv("XDG_CONFIG_HOME", config.toUtf8())
            || !qputenv("XDG_DATA_HOME", data.toUtf8()))
            qFatal("Cannot isolate CodingAgent standard paths");
        QStandardPaths::setTestModeEnabled(false);
    }

private slots:
    void initTestCase()
    {
        // Fail closed before constructing any backend or deleting any persisted files.
        QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation), testHome().path() + "/config");
        QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation), testHome().path() + "/data");
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
        CodingAgent::setSaveDelay(0); // writes at every save(): restore tests stay deterministic; the delay has its own test
    }

    void init()
    {
        // Every row gets a fresh backend profile, without touching the user's Notch files.
        QVERIFY(QDir(testHome().path() + "/config").removeRecursively());
        QVERIFY(QDir(testHome().path() + "/data").removeRecursively());
        QVERIFY(QDir().mkpath(testHome().path() + "/config"));
        QVERIFY(QDir().mkpath(testHome().path() + "/data"));
    }

    void presetsAndEditableRoundtrip()
    {
        const auto providers = defaultProviders();
        QCOMPARE(providers.size(), 4);
        QStringList ids;
        for (const auto &provider : providers) {
            QVERIFY(provider.builtIn);
            QVERIFY(!ids.contains(provider.id));
            ids.append(provider.id);
            QVERIFY(provider.modelPresets.contains(provider.modelId));
            QCOMPARE(providerToJson(providerFromJson(providerToJson(provider))), providerToJson(provider));
            if (provider.id == "mimo") {
                QCOMPARE(provider.modelId, QStringLiteral("mimo-v2.6-pro"));
                QCOMPARE(provider.authHeader, QStringLiteral("api-key"));
            } else if (provider.id == "deepseek") {
                QCOMPARE(provider.modelId, QStringLiteral("deepseek-flash"));
            }
        }
        QVERIFY(ids.contains("mimo"));
        QVERIFY(ids.contains("deepseek"));
        auto editable = providerToJson(providers.first());
        editable.insert("modelId", "  arbitrary-future-model  ");
        editable.insert("modelPresets", QJsonArray{"preset", "preset", "", " preset "});
        editable.insert("temperature", 0.7);
        editable.insert("maxTokens", 8192);
        editable.insert("customHeadersJson", "{\"X-Test\":\"yes\"}");
        editable.insert("extraBodyJson", "{\"top_p\":0.8}");
        const auto restored = providerFromJson(editable);
        QCOMPARE(restored.modelId, QStringLiteral("arbitrary-future-model"));
        QCOMPARE(restored.modelPresets, (QStringList{"arbitrary-future-model", "preset"}));
        QCOMPARE(restored.temperature, 0.7);
        QCOMPARE(restored.maxTokens, 8192);
        QCOMPARE(providerToJson(providerFromJson(providerToJson(restored))), providerToJson(restored));
    }

    void tools_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<int>("count");
        QTest::newRow("plan") << QString("plan") << 4;
        QTest::newRow("manual") << QString("manual") << 6;
        QTest::newRow("auto") << QString("auto") << 6;
        QTest::newRow("normalized-plan") << QString(" PLAN ") << 4;
        QTest::newRow("unknown-is-manual") << QString("unknown") << 6;
    }

    void tools()
    {
        QFETCH(QString, mode);
        QFETCH(int, count);
        QStringList expected{"list_files", "read_file", "search_files"};
        if (mode.trimmed().toLower() == "plan")
            expected << "write_plan";
        else
            expected << "propose_edit" << "run_command" << "todo_write";
        expected.sort();
        const auto openai = openAiToolsForMode(mode);
        const auto anthropic = anthropicToolsForMode(mode);
        QCOMPARE(openai.size(), count);
        QCOMPARE(anthropic.size(), count);
        QCOMPARE(toolNames(openai, false), expected);
        QCOMPARE(toolNames(anthropic, true), expected);
        for (int i = 0; i < count; ++i) {
            const auto fn = openai[i].toObject().value("function").toObject();
            QCOMPARE(openai[i].toObject().value("type").toString(), QStringLiteral("function"));
            QCOMPARE(fn.value("parameters"), anthropic[i].toObject().value("input_schema"));
            QCOMPARE(fn.value("parameters").toObject().value("type").toString(), QStringLiteral("object"));
        }
    }

    void protectedRequestKeys_data()
    {
        QTest::addColumn<QString>("protocol");
        QTest::addColumn<bool>("stream");
        QTest::newRow("openai-stream") << QString("openai") << true;
        QTest::newRow("openai-no-stream") << QString("openai") << false;
        QTest::newRow("anthropic-stream") << QString("anthropic") << true;
        QTest::newRow("anthropic-no-stream") << QString("anthropic") << false;
    }

    void protectedRequestKeys()
    {
        QFETCH(QString, protocol);
        QFETCH(bool, stream);
        ProviderConfig provider;
        provider.protocol = protocol;
        provider.modelId = "chosen-model";
        ChatMessage user;
        user.role = "user";
        user.content = "Real prompt";
        const auto baseline = QJsonDocument::fromJson(buildRequestPayload(provider, "plan", "/workspace", {user}, stream)).object();
        provider.extraBodyJson = QString::fromUtf8(json({{"model", "injected-model"},
            {"messages", QJsonArray{}}, {"tools", QJsonArray{}}, {"stream", !stream}, {"top_p", 0.75}}));
        const auto actual = QJsonDocument::fromJson(buildRequestPayload(provider, "plan", "/workspace", {user}, stream)).object();
        for (const auto &key : {"model", "messages", "tools", "stream"})
            QCOMPARE(actual.value(key), baseline.value(key));
        QCOMPARE(actual.value("top_p").toDouble(), 0.75);
        QCOMPARE(actual.value("tools").toArray().size(), 4);
        const auto messages = actual.value("messages").toArray();
        QCOMPARE(messages.last().toObject().value("content").toString(), user.content);
    }

    void maxTokensPayloadField()
    {
        ProviderConfig provider;
        provider.modelId = "test-model";
        provider.maxTokens = 1234;
        QCOMPARE(provider.maxTokensParameter, QStringLiteral("max_tokens"));
        QCOMPARE(providerFromJson({}).maxTokensParameter, QStringLiteral("max_tokens"));
        auto payload = QJsonDocument::fromJson(buildRequestPayload(provider, "manual", "/workspace", {})).object();
        QCOMPARE(payload.value("max_tokens").toInt(), 1234);
        QVERIFY(!payload.contains("max_completion_tokens"));

        provider.maxTokensParameter = "max_completion_tokens";
        const auto restored = providerFromJson(providerToJson(provider));
        QCOMPARE(restored.maxTokensParameter, provider.maxTokensParameter);
        payload = QJsonDocument::fromJson(buildRequestPayload(restored, "manual", "/workspace", {})).object();
        QCOMPARE(payload.value("max_completion_tokens").toInt(), 1234);
        QVERIFY(!payload.contains("max_tokens"));

        const auto providers = defaultProviders();
        const auto mimo = providers.first();
        QCOMPARE(mimo.id, QStringLiteral("mimo"));
        QCOMPARE(mimo.maxTokensParameter, QStringLiteral("max_completion_tokens"));
        payload = QJsonDocument::fromJson(buildRequestPayload(mimo, "manual", "/workspace", {})).object();
        QCOMPARE(payload.value("max_completion_tokens").toInt(), mimo.maxTokens);
        QVERIFY(!payload.contains("max_tokens"));

        // Anthropic's wire protocol always uses max_tokens, regardless of the OpenAI setting.
        provider.protocol = "anthropic";
        payload = QJsonDocument::fromJson(buildRequestPayload(provider, "manual", "/workspace", {})).object();
        QCOMPARE(payload.value("max_tokens").toInt(), 1234);
        QVERIFY(!payload.contains("max_completion_tokens"));
    }

    void endpointUrls_data()
    {
        QTest::addColumn<QString>("protocol");
        QTest::addColumn<QString>("base");
        QTest::addColumn<QString>("chat");
        QTest::addColumn<QString>("models");
        QTest::newRow("openai-v1") << QString("openai") << QString("https://example.test/v1")
            << QString("https://example.test/v1/chat/completions") << QString("https://example.test/v1/models");
        QTest::newRow("openai-root") << QString("openai") << QString("https://example.test/")
            << QString("https://example.test/chat/completions") << QString("https://example.test/models");
        QTest::newRow("openai-full") << QString("openai") << QString(" https://example.test/v1/chat/completions/// ")
            << QString("https://example.test/v1/chat/completions") << QString("https://example.test/v1/models");
        QTest::newRow("anthropic-root") << QString("anthropic") << QString("https://example.test/")
            << QString("https://example.test/v1/messages") << QString("https://example.test/models");
        QTest::newRow("anthropic-v1") << QString("anthropic") << QString("https://example.test/v1///")
            << QString("https://example.test/v1/messages") << QString("https://example.test/v1/models");
        QTest::newRow("claude-full") << QString("claude") << QString("https://example.test/v1/messages/")
            << QString("https://example.test/v1/messages") << QString("https://example.test/v1/models");
        QTest::newRow("local-custom-prefix") << QString("openai") << QString("http://127.0.0.1:12345/custom/v1/")
            << QString("http://127.0.0.1:12345/custom/v1/chat/completions") << QString("http://127.0.0.1:12345/custom/v1/models");
    }

    void endpointUrls()
    {
        QFETCH(QString, protocol);
        QFETCH(QString, base);
        QFETCH(QString, chat);
        QFETCH(QString, models);
        ProviderConfig provider;
        provider.protocol = protocol;
        provider.baseUrl = base;
        QCOMPARE(chatEndpointUrl(provider), QUrl(chat));
        QCOMPARE(modelsEndpointUrl(provider), QUrl(models));
    }

    void workspacePaths()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString root = directory.path() + "/workspace";
        QVERIFY(QDir().mkpath(root + "/sub"));
        bool ok = false;
        QCOMPARE(resolveWorkspacePath(root, "sub/../new.txt", &ok), root + "/new.txt");
        QVERIFY(ok);
        QCOMPARE(resolveWorkspacePath(root, root + "/sub/file.txt", &ok), root + "/sub/file.txt");
        QVERIFY(ok);
        QCOMPARE(resolveWorkspacePath(root, ".", &ok), root);
        QVERIFY(ok);
        for (const auto &path : QStringList{"../outside", "sub/../../outside", directory.path() + "/outside",
                                           root + "-sibling/file.txt"}) {
            ok = true;
            QVERIFY2(resolveWorkspacePath(root, path, &ok).isEmpty(), qPrintable(path));
            QVERIFY(!ok);
        }
    }

    void symlinkEscapes()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString root = directory.path() + "/workspace";
        const QString outside = directory.path() + "/outside";
        QVERIFY(QDir().mkpath(root + "/inside"));
        QVERIFY(QDir().mkpath(outside));
        QVERIFY(writeBytes(outside + "/secret.txt", "secret"));
        QVERIFY(QFile::link(outside, root + "/escape"));
        QVERIFY(QFile::link(outside + "/secret.txt", root + "/secret-link"));
        QVERIFY(QFile::link(root + "/inside", root + "/safe-link"));
        // All symlinks are forbidden, including links whose targets stay in the workspace.
        for (const auto &path : {"escape/secret.txt", "escape/new.txt", "escape/new/sub/file.txt", "secret-link",
                                 "safe-link", "safe-link/new.txt"}) {
            bool ok = true;
            QVERIFY2(resolveWorkspacePath(root, path, &ok).isEmpty(), path);
            QVERIFY(!ok);
        }
        QCOMPARE(readBytes(outside + "/secret.txt"), QByteArray("secret"));
#else
        QSKIP("QFile::link creates shortcuts rather than POSIX symlinks on this platform");
#endif
    }

    void diffCounts_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::addColumn<int>("added");
        QTest::addColumn<int>("removed");
        QTest::newRow("empty") << QString() << QString() << 0 << 0;
        QTest::newRow("unchanged") << QString("a\nb") << QString("a\nb") << 0 << 0;
        QTest::newRow("create") << QString() << QString("a\nb") << 2 << 0;
        QTest::newRow("delete") << QString("a\nb") << QString() << 0 << 2;
        QTest::newRow("replace") << QString("a\nb\nc") << QString("a\nx\nc\nd") << 2 << 1;
        QTest::newRow("duplicates") << QString("a\na\nb") << QString("a\nb") << 0 << 1;
        QTest::newRow("trailing-newline") << QString("a\n") << QString("b\n") << 1 << 1;
        QTest::newRow("bounded-fallback") << QString("a\n").repeated(501) << QString("b\n").repeated(501) << 502 << 502;
    }

    void diffCounts()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        QFETCH(int, added);
        QFETCH(int, removed);
        const auto diff = computeUnifiedDiff("src/example.cpp", before, after);
        QCOMPARE(diff.added, added);
        QCOMPARE(diff.removed, removed);
        QVERIFY(diff.unified.startsWith("--- a/src/example.cpp\n+++ b/src/example.cpp"));
        int plus = 0, minus = 0;
        for (const auto &line : diff.unified.split('\n')) {
            plus += line.startsWith("+ ");
            minus += line.startsWith("- ");
        }
        QCOMPARE(plus, added);
        QCOMPARE(minus, removed);
    }

    void openAiStreamFragments()
    {
        StreamAccumulator stream;
        stream.feedDataLine("", "openai");
        stream.feedDataLine("not-json", "openai");
        stream.feedDataLine("{\"choices\":[]}", "openai");
        stream.feedDataLine(json(deltaEvent({{"content", "Hello "}, {"reasoning_content", "Think "},
            {"tool_calls", QJsonArray{toolDelta(0, "call-0", "read_", "{\"path\":\"")}}})), "openai");
        stream.feedDataLine(json(deltaEvent({{"content", QString::fromUtf8("世界")}, {"reasoning_content", "carefully"},
            {"tool_calls", QJsonArray{toolDelta(1, "call-1", "list_files", "{}"),
                                     toolDelta(0, "", "file", "src/main.cpp\"}")}}})), "openai");
        stream.feedDataLine("{\"choices\":[{\"delta\":{\"content\":null,\"reasoning_content\":null},\"finish_reason\":\"tool_calls\"}]}", "openai");
        QVERIFY(!stream.done);
        stream.feedDataLine(" [DONE] ", "openai");
        QVERIFY(stream.done);
        QVERIFY(stream.errorText.isEmpty());
        QCOMPARE(stream.content, QString::fromUtf8("Hello 世界"));
        QCOMPARE(stream.reasoning, QStringLiteral("Think carefully"));
        QCOMPARE(stream.finishReason, QStringLiteral("tool_calls"));
        QCOMPARE(stream.toolCalls.size(), 2);
        QCOMPARE(stream.toolCalls[0].id, QStringLiteral("call-0"));
        QCOMPARE(stream.toolCalls[0].name, QStringLiteral("read_file"));
        QCOMPARE(stream.toolCalls[0].argumentsJson, QStringLiteral("{\"path\":\"src/main.cpp\"}"));
        QCOMPARE(stream.toolCalls[1].id, QStringLiteral("call-1"));
        QCOMPARE(stream.toolCalls[1].argumentsJson, QStringLiteral("{}"));
    }

    void invalidToolIndex_data()
    {
        QTest::addColumn<int>("index");
        QTest::newRow("negative") << -1;
        QTest::newRow("above-limit") << 64;
        QTest::newRow("large") << 1000000;
    }

    void invalidToolIndex()
    {
        QFETCH(int, index);
        StreamAccumulator stream;
        stream.feedDataLine(json(deltaEvent({{"tool_calls", QJsonArray{toolDelta(0, "valid", "read_file", "{}")}}})), "openai");
        stream.feedDataLine(json(deltaEvent({{"tool_calls", QJsonArray{toolDelta(index, "invalid", "run_command", "{}")}}})), "openai");
        QVERIFY(!stream.errorText.isEmpty());
        QCOMPARE(stream.toolCalls.size(), 1);
        QCOMPARE(stream.toolCalls.first().id, QStringLiteral("valid"));
        QCOMPARE(stream.toolCalls.first().argumentsJson, QStringLiteral("{}"));
    }

    void maximumValidToolIndex()
    {
        StreamAccumulator stream;
        stream.feedDataLine(json(deltaEvent({{"tool_calls", QJsonArray{toolDelta(63, "last", "read_file", "{}")}}})), "openai");
        QVERIFY(stream.errorText.isEmpty());
        QCOMPARE(stream.toolCalls.size(), 64);
        QCOMPARE(stream.toolCalls.last().id, QStringLiteral("last"));
    }

    void anthropicStreamFragments()
    {
        StreamAccumulator stream;
        const auto feed = [&stream](const QJsonObject &event) { stream.feedDataLine(json(event), "anthropic"); };
        feed({{"type", "content_block_delta"}, {"delta", QJsonObject{{"type", "input_json_delta"}, {"partial_json", "ignored"}}}});
        QVERIFY(stream.toolCalls.isEmpty());
        feed({{"type", "content_block_delta"}, {"delta", QJsonObject{{"type", "text_delta"}, {"text", "Hello "}}}});
        feed({{"type", "content_block_delta"}, {"delta", QJsonObject{{"type", "thinking_delta"}, {"thinking", "Think "}}}});
        feed({{"type", "content_block_delta"}, {"delta", QJsonObject{{"type", "text_delta"}, {"text", "world"}}}});
        feed({{"type", "content_block_delta"}, {"delta", QJsonObject{{"type", "thinking_delta"}, {"thinking", "first"}}}});
        feed({{"type", "content_block_start"}, {"index", 2},
              {"content_block", QJsonObject{{"type", "tool_use"}, {"id", "tool-a"}, {"name", "read_file"}}}});
        for (const auto &part : {"{\"path\":", "\"a.cpp\"}"})
            feed({{"type", "content_block_delta"}, {"index", 2},
                  {"delta", QJsonObject{{"type", "input_json_delta"}, {"partial_json", part}}}});
        feed({{"type", "content_block_start"}, {"index", 3},
              {"content_block", QJsonObject{{"type", "tool_use"}, {"id", "tool-b"}, {"name", "list_files"}}}});
        feed({{"type", "content_block_delta"}, {"index", 3},
              {"delta", QJsonObject{{"type", "input_json_delta"}, {"partial_json", "{}"}}}});
        feed({{"type", "message_delta"}, {"delta", QJsonObject{{"stop_reason", "tool_use"}}}});
        feed({{"type", "message_stop"}});
        QVERIFY(stream.done);
        QVERIFY(stream.errorText.isEmpty());
        QCOMPARE(stream.content, QStringLiteral("Hello world"));
        QCOMPARE(stream.reasoning, QStringLiteral("Think first"));
        QCOMPARE(stream.finishReason, QStringLiteral("tool_use"));
        QCOMPARE(stream.toolCalls.size(), 2);
        QCOMPARE(stream.toolCalls[0].id, QStringLiteral("tool-a"));
        QCOMPARE(stream.toolCalls[0].name, QStringLiteral("read_file"));
        QCOMPARE(stream.toolCalls[0].argumentsJson, QStringLiteral("{\"path\":\"a.cpp\"}"));
        QCOMPARE(stream.toolCalls[1].argumentsJson, QStringLiteral("{}"));
    }

    void streamErrorsAndNonStreamingMessage()
    {
        StreamAccumulator stream;
        stream.feedDataLine("{\"choices\":[{\"message\":{\"content\":\"Answer\",\"reasoning_content\":\"Reason\"},\"finish_reason\":\"stop\"}]}", "openai");
        QCOMPARE(stream.content, QStringLiteral("Answer"));
        QCOMPARE(stream.reasoning, QStringLiteral("Reason"));
        QCOMPARE(stream.finishReason, QStringLiteral("stop"));
        stream.feedDataLine("{\"error\":{\"message\":\"Bad key\"}}", "openai");
        QCOMPARE(stream.errorText, QStringLiteral("Bad key"));
        StreamAccumulator anthropic;
        anthropic.feedDataLine("{\"type\":\"error\",\"error\":{\"message\":\"Rate limited\"}}", "anthropic");
        QCOMPARE(anthropic.errorText, QStringLiteral("Rate limited"));
        QVERIFY(!anthropic.done);
    }

    void serialization()
    {
        ToolCall tool;
        tool.id = "call";
        tool.name = "propose_edit";
        tool.argumentsJson = "{\"path\":\"file.txt\",\"new_text\":\"new\"}";
        tool.status = "awaiting_approval";
        tool.summary = "Update file";
        tool.diffText = "- old\n+ new";
        tool.filePath = "file.txt";
        tool.proposedContent = QString::fromUtf8("new\n世界");
        tool.command = "printf harmless";
        tool.output = "output\n";
        tool.addedLines = 2;
        tool.removedLines = 1;
        ChatMessage message;
        message.id = "message";
        message.role = "assistant";
        message.content = "Proposed change";
        message.reasoning = "Reasoning";
        message.toolCallId = "parent-call";
        message.toolName = "propose_edit";
        message.toolCalls = {tool};
        message.timestamp = 1700000000123LL;
        Session session;
        session.id = "session";
        session.title = "Task";
        session.workspace = "/workspace";
        session.mode = "plan";
        session.providerId = "custom";
        session.modelId = "editable-model";
        session.messages = {message};
        session.updatedAt = 1700000000456LL;
        const auto toolObject = toolCallToJson(tool);
        QCOMPARE(toolCallToJson(toolCallFromJson(toolObject)), toolObject);
        const auto messageObject = messageToJson(message);
        QCOMPARE(messageToJson(messageFromJson(messageObject)), messageObject);
        const auto sessionObject = sessionToJson(session);
        const auto diskObject = QJsonDocument::fromJson(json(sessionObject)).object();
        const auto restored = sessionFromJson(diskObject);
        QCOMPARE(sessionToJson(restored), sessionObject);
        QCOMPARE(restored.updatedAt, session.updatedAt);
        QCOMPARE(restored.messages.first().timestamp, message.timestamp);
        QCOMPARE(restored.messages.first().toolCalls.first().proposedContent, tool.proposedContent);
    }

    void backendSessionCrud()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QVERIFY(writeBytes(workspace.filePath("example.txt"), "example"));
        CodingAgent agent;
        QVERIFY(agent.sessions().isEmpty());
        QVERIFY(agent.messages().isEmpty());
        QVERIFY(!agent.busy());
        agent.setWorkspace(QUrl::fromLocalFile(workspace.path()).toString());
        QCOMPARE(agent.workspace(), QFileInfo(workspace.path()).canonicalFilePath());
        QCOMPARE(agent.workspaceFiles().size(), 1);
        QSignalSpy changes(&agent, &CodingAgent::sessionsChanged);
        const auto first = agent.newSession("First task");
        QVERIFY(!first.isEmpty());
        QCOMPARE(agent.activeSessionId(), first);
        QCOMPARE(agent.sessions().first().toMap().value("title").toString(), QStringLiteral("First task"));
        agent.renameSession(first, "Renamed task");
        QCOMPARE(agent.sessions().first().toMap().value("title").toString(), QStringLiteral("Renamed task"));
        const auto second = agent.newSession();
        QVERIFY(second != first);
        QCOMPARE(agent.sessions().size(), 2);
        QCOMPARE(agent.sessions().first().toMap().value("title").toString(), QStringLiteral("New task"));
        agent.selectSession(first);
        QCOMPARE(agent.activeSessionId(), first);
        agent.selectSession("nonexistent");
        QCOMPARE(agent.activeSessionId(), first);
        agent.clearSession();
        QVERIFY(agent.messages().isEmpty());
        agent.deleteSession(second);
        QCOMPARE(agent.sessions().size(), 1);
        QVERIFY(changes.count() >= 5);
        {
            CodingAgent restored;
            QCOMPARE(restored.activeSessionId(), first);
            QCOMPARE(restored.workspace(), agent.workspace());
            QCOMPARE(restored.sessions(), agent.sessions());
        }
        agent.deleteSession(first);
        QVERIFY(agent.activeSessionId().isEmpty());
        QVERIFY(agent.sessions().isEmpty());
    }

    void effortIsSentOnlyWhenChosen()
    {
        QCOMPARE(normalizeEffort("Extra"), QStringLiteral("xhigh"));
        QCOMPARE(normalizeEffort(" MAX "), QStringLiteral("max"));
        QVERIFY(normalizeEffort("turbo").isEmpty());
        ChatMessage user;
        user.role = "user";
        user.content = "hi";
        ProviderConfig openai;
        openai.protocol = "openai";
        openai.modelId = "m";
        ProviderConfig claude = openai;
        claude.protocol = "anthropic";
        const auto none = QJsonDocument::fromJson(buildRequestPayload(openai, "plan", "/w", {user}, true, {}, {})).object();
        QVERIFY(!none.contains("reasoning_effort"));
        const auto o = QJsonDocument::fromJson(buildRequestPayload(openai, "plan", "/w", {user}, true, {}, "high")).object();
        QCOMPARE(o.value("reasoning_effort").toString(), QStringLiteral("high"));
        const auto c = QJsonDocument::fromJson(buildRequestPayload(claude, "plan", "/w", {user}, true, {}, "extra")).object();
        QCOMPARE(c.value("output_config").toObject().value("effort").toString(), QStringLiteral("xhigh"));
        QVERIFY(!c.contains("reasoning_effort"));
        QVERIFY(!QJsonDocument::fromJson(buildRequestPayload(claude, "plan", "/w", {user}, true, {}, "")).object().contains("output_config"));
        CodingAgent agent;
        agent.setEffort("max");
        QCOMPARE(agent.effort(), QStringLiteral("max"));
        { CodingAgent restored; QCOMPARE(restored.effort(), QStringLiteral("max")); }
        agent.setEffort("");
        QVERIFY(agent.effort().isEmpty());
    }

    void modelsAreGroupedByCategory_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("group");
        QTest::newRow("opus") << "claude-opus-4-1-20250805" << "Opus";
        QTest::newRow("sonnet") << "claude-sonnet-4-20250514" << "Sonnet";
        QTest::newRow("haiku") << "claude-3-5-haiku-latest" << "Haiku";
        QTest::newRow("gpt5") << "gpt-5-mini" << "GPT-5";
        QTest::newRow("gpt41") << "gpt-4.1-mini" << "GPT-4.1";
        QTest::newRow("gpt4o") << "gpt-4o-2024-08-06" << "GPT-4o";
        QTest::newRow("o4") << "o4-mini" << "Reasoning";
        QTest::newRow("o3") << "o3" << "Reasoning";
        QTest::newRow("deepseek reasoner") << "deepseek-reasoner" << "Reasoning";
        QTest::newRow("embedding") << "text-embedding-3-large" << "Embeddings";
        QTest::newRow("audio") << "gpt-4o-transcribe" << "Audio";
        QTest::newRow("tts") << "tts-1-hd" << "Audio";
        QTest::newRow("image") << "dall-e-3" << "Images";
        QTest::newRow("mimo pro") << "mimo-v2.6-pro" << "Pro";
        QTest::newRow("mimo flash") << "mimo-v2.6-flash" << "Flash";
        QTest::newRow("deepseek chat") << "deepseek-chat" << "Chat";
        QTest::newRow("unknown") << "something-new" << "Other";
    }
    void modelsAreGroupedByCategory()
    {
        QFETCH(QString, id);
        QFETCH(QString, group);
        QCOMPARE(modelGroup(id), group);
        QVERIFY(modelGroups().contains(group));
    }
    void modelListParsing()
    {
        QCOMPARE(parseModelIds(R"({"data":[{"id":"a"},{"id":" b "},{"id":"a"},{"x":1},{"id":""}]})"), (QStringList{"a", "b"}));
        QVERIFY(parseModelIds("not json").isEmpty());
        QCOMPARE(parseModelIds(R"({"data":[{"id":"1"},{"id":"2"},{"id":"3"}]})", 2).size(), 2);
        CodingAgent agent; // without a key nothing is requested and the presets are the list
        agent.refreshModels();
        QVERIFY(!agent.modelsLoading());
        const auto models = agent.models();
        QVERIFY(!models.isEmpty());
        QCOMPARE(models.first().toMap().value("id").toString(), agent.activeModelId());
    }

    void planReaderGetsADocumentLook()
    {
        MarkdownHtml::Style st;
        st.px = 14;
        st.code = QColor("#ff8a80");
        const QString html = MarkdownHtml::render("# Title\n\n## Context\n\nUse `AgentTab.qml` and [docs](https://example.test).\n\n1. First\n2. Second\n\n```\ncode block\n```\n\n| a | b |\n|---|---|\n| 1 | 2 |\n", st);
        QVERIFY(html.contains("<h1"));
        QVERIFY(html.contains("<h2"));
        QVERIFY(html.contains("AgentTab.qml"));
        QVERIFY(html.contains("#ff8a80")); // inline code is coloured
        QVERIFY(html.contains("background-color")); // and sits on a chip
        QVERIFY(html.contains("https://example.test"));
        QVERIFY(html.contains("<table"));
        QVERIFY(html.contains("border"));
        QVERIFY(!html.contains("<body style")); // the view's own font and colour apply
        QVERIFY(html.contains("<ol"));
    }

    void foldersGroupTasksAndNewTasksKeepTheirFolder()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QDir(root.path()).mkpath("alpha");
        QDir(root.path()).mkpath("beta");
        const QString alpha = QFileInfo(root.filePath("alpha")).canonicalFilePath(), beta = QFileInfo(root.filePath("beta")).canonicalFilePath();
        FakeServer server;
        QVERIFY(server.listen());
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        QVERIFY(!agent.openFolder(root.filePath("missing")));           // not a folder: refused
        QVERIFY(agent.openFolder(alpha));
        QCOMPARE(agent.workspace(), alpha);
        agent.sendPrompt("first task in alpha");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        // "New task" in the same folder: no picking again, and the folder stays
        agent.newSession();
        QCOMPARE(agent.workspace(), alpha);
        QCOMPARE(agent.sessions().first().toMap().value("workspace").toString(), alpha);
        // an empty task in the folder is reused, not doubled
        const int before = agent.sessions().size();
        QVERIFY(agent.openFolder(alpha));
        QCOMPARE(agent.sessions().size(), before);
        // another folder: its own task; the first one keeps its folder
        QVERIFY(agent.openFolder(beta));
        QCOMPARE(agent.workspace(), beta);
        const auto projects = agent.projects();
        QCOMPARE(projects.size(), 2);
        QSet<QString> paths;
        for (const auto &p : projects) paths << p.toMap().value("path").toString();
        QVERIFY(paths.contains(alpha) && paths.contains(beta));
        QCOMPARE(projects.first().toMap().value("path").toString(), beta); // the newest group first
        QVERIFY(projects.first().toMap().value("active").toBool());
        // selecting a task from alpha brings its folder back; a task with no folder adopts the current one instead of blanking it
        const QString alphaTask = [&] { for (const auto &s : agent.sessions()) if (s.toMap().value("workspace").toString() == alpha && s.toMap().value("title").toString() != "New task") return s.toMap().value("id").toString(); return QString(); }();
        QVERIFY(!alphaTask.isEmpty());
        agent.selectSession(alphaTask);
        QCOMPARE(agent.workspace(), alpha);
        { CodingAgent restored; QCOMPARE(restored.workspace(), alpha); QCOMPARE(restored.projects().size(), 2); }
        // names, folding, removing
        agent.renameFolder(beta, "Beta project");
        agent.setFolderCollapsed(beta, true);
        for (const auto &p : agent.projects()) if (p.toMap().value("path").toString() == beta) {
            QCOMPARE(p.toMap().value("name").toString(), QStringLiteral("Beta project"));
            QVERIFY(p.toMap().value("custom").toBool());
            QVERIFY(p.toMap().value("collapsed").toBool());
        }
        { CodingAgent restored; bool found = false; for (const auto &p : restored.projects()) if (p.toMap().value("path").toString() == beta) { found = true; QCOMPARE(p.toMap().value("name").toString(), QStringLiteral("Beta project")); } QVERIFY(found); }
        QVERIFY(!agent.removeFolder(alpha)); // it still has tasks
        QString betaTask;
        for (const auto &s : agent.sessions()) if (s.toMap().value("workspace").toString() == beta) betaTask = s.toMap().value("id").toString();
        agent.deleteSession(betaTask);
        QVERIFY(agent.removeFolder(beta));
        QVERIFY(QDir(beta).exists()); // never deletes files
        QCOMPARE(agent.projects().size(), 1);
        agent.renameSession(alphaTask, "Renamed task");
        QCOMPARE(agent.sessions().first().toMap().value("title").toString().isEmpty(), false);
        bool renamed = false;
        for (const auto &s : agent.sessions()) renamed = renamed || s.toMap().value("title").toString() == "Renamed task";
        QVERIFY(renamed);
    }

    void aTaskWithoutAFolderDoesNotBlankTheCurrentOne()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        CodingAgent agent;
        const QString orphan = agent.newSession(); // made before any folder was chosen
        QVERIFY(agent.workspace().isEmpty());
        agent.setWorkspace(root.path());           // the person picks one: the open task is left behind
        const QString folder = agent.workspace();
        QVERIFY(!folder.isEmpty());
        agent.selectSession(orphan);               // ... and goes back to the old empty task
        QCOMPARE(agent.workspace(), folder);       // the folder stays; the task adopts it
        bool adopted = false;
        for (const auto &s : agent.sessions()) if (s.toMap().value("id").toString() == orphan) adopted = s.toMap().value("workspace").toString() == folder;
        QVERIFY(adopted);
        QVERIFY(agent.folderExists(folder));
        QVERIFY(!agent.folderExists(root.filePath("nope")));
    }

    void folderPickerHelpers()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QDir(root.path()).mkpath("b dir");
        QDir(root.path()).mkpath("a");
        QDir(root.path()).mkpath(".hidden");
        QVERIFY(writeBytes(root.filePath("file.txt"), "x"));
        CodingAgent agent;
        QStringList notes;
        connect(&agent, &CodingAgent::statusMessage, &agent, [&notes](const QString &t) { notes << t; });
        const auto list = agent.listFolders(root.path());
        QCOMPARE(list.size(), 2);                                     // folders only, none hidden, sorted
        QCOMPARE(list[0].toMap().value("name").toString(), QStringLiteral("a"));
        QCOMPARE(list[1].toMap().value("name").toString(), QStringLiteral("b dir"));
        QVERIFY(agent.listFolders(root.filePath("nope")).isEmpty());
        QCOMPARE(agent.parentFolder(root.filePath("a")), QDir(root.path()).absolutePath());
        QVERIFY(!agent.homeFolder().isEmpty());
        const QString made = agent.makeFolder(root.path(), "new project");
        QCOMPARE(made, QDir(root.path()).absoluteFilePath("new project"));
        QVERIFY(QDir(made).exists());
        for (const QString &bad : {QString(""), QString(".."), QString("a/b"), QString("a\\b"), QString("a"), QString("new project")})
            QVERIFY2(agent.makeFolder(root.path(), bad).isEmpty(), qPrintable(bad));
        QVERIFY(!notes.isEmpty());
        QVERIFY(agent.makeFolder(root.filePath("missing"), "x").isEmpty());
        // export into a folder, never overwriting
        FakeServer server;
        QVERIFY(server.listen());
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        QVERIFY(agent.openFolder(root.path()));
        agent.sendPrompt("export me");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        const QString one = agent.exportSessionTo(made), two = agent.exportSessionTo(made);
        QVERIFY(one.endsWith("export me.md") && QFile::exists(one));
        QVERIFY(two.endsWith("export me (2).md") && QFile::exists(two));
    }

    void slashCommandsAndMentions()
    {
        using namespace AgentCommands;
        QVERIFY(parseSlash("/fork").ok);
        QCOMPARE(parseSlash("/model gpt-4.1").args, QStringLiteral("gpt-4.1"));
        QCOMPARE(parseSlash("  /EFFORT high ").name, QStringLiteral("effort"));
        QVERIFY(!parseSlash("/usr/bin/env").ok);
        QVERIFY(!parseSlash("hello /fork").ok);
        QVERIFY(!parseSlash("/").ok);
        QCOMPARE(completeSlash("mo").first().name, QStringLiteral("model"));
        QVERIFY(completeSlash("mo").size() >= 2); // "mode" too
        QCOMPARE(completeSlash("").size(), slashCommands().size());
        QVERIFY(completeSlash("zzz").isEmpty());
        const auto m = mentionTokens("look at @src/a.ts, then @b.md. Mail me@x.com @ alone @");
        QCOMPARE(m.size(), 2);
        QCOMPARE(m[0].path, QStringLiteral("src/a.ts"));
        QCOMPARE(m[1].path, QStringLiteral("b.md"));
        QCOMPARE(m[0].start, 8);
        QCOMPARE(mentionTokens("@x/y.txt").first().length, 8);
        const QStringList files{"src/main.cpp", "src/platform/Main.h", "docs/readme.md", "README.md", "src/qml/AgentTab.qml", "a/b/c/deep.txt"};
        QCOMPARE(rankFiles("main", files).first(), QStringLiteral("src/main.cpp")); // name starts with it, shorter first
        QCOMPARE(rankFiles("readme", files).first(), QStringLiteral("README.md"));
        QVERIFY(rankFiles("agtb", files).contains("src/qml/AgentTab.qml")); // letters in order
        QVERIFY(rankFiles("zzz", files).isEmpty());
        QCOMPARE(rankFiles("", files, 3).size(), 3);
        QCOMPARE(rankFiles("", files, 3).first(), QStringLiteral("README.md")); // shallowest first
    }

    void newDataSurvivesJson()
    {
        ChatMessage m;
        m.id = "m"; m.role = "user"; m.content = "full text with files"; m.shown = "short"; m.attachments = {{"src/a.ts", 12}, {"b.md", 3}};
        const auto back = messageFromJson(messageToJson(m));
        QCOMPARE(back.shown, QStringLiteral("short"));
        QCOMPARE(back.attachments.size(), 2);
        QCOMPARE(back.attachments[0].path, QStringLiteral("src/a.ts"));
        QCOMPARE(back.attachments[0].bytes, qint64(12));
        Session s;
        s.id = "s"; s.messages = {m, m, m}; s.summary = "sum"; s.compactedUpTo = 2; s.lastPromptTokens = 900;
        s.todos = {{"one", "completed"}, {"two", "in_progress"}};
        const auto sb = sessionFromJson(sessionToJson(s));
        QCOMPARE(sb.summary, QStringLiteral("sum"));
        QCOMPARE(sb.compactedUpTo, 2);
        QCOMPARE(sb.lastPromptTokens, 900);
        QCOMPARE(sb.todos.size(), 2);
        QCOMPARE(sb.todos[1].status, QStringLiteral("in_progress"));
        s.compactedUpTo = 99; // never past the messages
        QCOMPARE(sessionFromJson(sessionToJson(s)).compactedUpTo, 3);
        ProviderConfig p;
        p.id = "p"; p.name = "n"; p.baseUrl = "https://x.test"; p.modelId = "claude-sonnet"; p.contextWindow = 50000;
        QCOMPARE(providerFromJson(providerToJson(p)).contextWindow, 50000);
        QCOMPARE(effectiveContextWindow(p), 50000);
        p.contextWindow = 0;
        QCOMPARE(effectiveContextWindow(p), 200000);
        QCOMPARE(defaultContextWindow("gpt-4.1-mini"), 1000000);
        QCOMPARE(defaultContextWindow("something-new"), 128000);
    }

    void todoListHelpers()
    {
        QString err;
        auto list = parseTodos(QJsonDocument::fromJson(R"({"todos":[{"content":"  a  step ","status":"IN_PROGRESS"},{"content":"b","status":"weird"},{"content":"","status":"pending"},{"content":"c","status":"completed"}]})").object(), &err);
        QVERIFY(err.isEmpty());
        QCOMPARE(list.size(), 3);
        QCOMPARE(list[0].content, QStringLiteral("a step"));
        QCOMPARE(list[0].status, QStringLiteral("in_progress"));
        QCOMPARE(list[1].status, QStringLiteral("pending"));
        QCOMPARE(todosDone(list), 1);
        QVERIFY(todoContextPrompt(list).contains("[~] a step"));
        QVERIFY(todoContextPrompt(list).contains("[x] c"));
        QVERIFY(todoContextPrompt({}).isEmpty());
        parseTodos(QJsonDocument::fromJson(R"({"nope":1})").object(), &err);
        QVERIFY(!err.isEmpty());
        QJsonArray many;
        for (int i = 0; i < 31; ++i) many.append(QJsonObject{{"content", QString::number(i)}, {"status", "pending"}});
        QVERIFY(parseTodos({{"todos", many}}, &err).isEmpty());
        QVERIFY(err.contains("30"));
    }

    void usageIsReadFromBothProtocols()
    {
        StreamAccumulator openai;
        openai.feedDataLine(R"({"choices":[{"delta":{"content":"hi"}}]})", "openai");
        QCOMPARE(openai.promptTokens, 0);
        openai.feedDataLine(R"({"choices":[],"usage":{"prompt_tokens":1234,"completion_tokens":56}})", "openai");
        QCOMPARE(openai.promptTokens, 1234);
        QCOMPARE(openai.completionTokens, 56);
        StreamAccumulator claude;
        claude.feedDataLine(R"({"type":"message_start","message":{"usage":{"input_tokens":100,"cache_read_input_tokens":900,"output_tokens":1}}})", "anthropic");
        QCOMPARE(claude.promptTokens, 1000);
        claude.feedDataLine(R"({"type":"message_delta","delta":{},"usage":{"output_tokens":42}})", "anthropic");
        QCOMPARE(claude.completionTokens, 42);
        QCOMPARE(claude.promptTokens, 1000);
    }

    void compactionBuildingBlocks()
    {
        auto msg = [](const QString &role, const QString &text) { ChatMessage m; m.id = text; m.role = role; m.content = text; return m; };
        QList<ChatMessage> h{msg("user", "u0"), msg("assistant", "a0"), msg("tool", "t0"), msg("user", "u1"), msg("assistant", "a1"),
                             msg("user", "u2"), msg("assistant", "a2"), msg("user", "u3"), msg("assistant", "a3")};
        const int cut = compactCutIndex(h, 0, 4); // keeps about the last 4, from a user message
        QVERIFY(cut > 1);
        QCOMPARE(h[cut].role, QStringLiteral("user"));
        QCOMPARE(compactCutIndex(h, 0, 100), -1);                    // nothing old enough
        QCOMPARE(compactCutIndex({msg("user", "x"), msg("assistant", "y")}, 0), -1);
        // a tool result is never the first kept message
        QList<ChatMessage> t{msg("user", "q"), msg("assistant", "call"), msg("tool", "res"), msg("assistant", "ans"), msg("tool", "res2"), msg("assistant", "more"), msg("user", "next"), msg("assistant", "done")};
        const int c2 = compactCutIndex(t, 0, 2);
        QVERIFY(c2 < 0 || t[c2].role == "user");
        const QString flat = flattenForSummary(h, 0, 4);
        QVERIFY(flat.contains("User: u0") && flat.contains("Tool result") && !flat.contains("u3"));

        ProviderConfig openai; openai.protocol = "openai"; openai.modelId = "m"; openai.maxTokens = 500;
        auto o = QJsonDocument::fromJson(buildSummaryPayload(openai, "TRANSCRIPT", "the API names", "OLD")).object();
        QVERIFY(!o.value("stream").toBool());
        QVERIFY(!o.contains("tools"));
        QCOMPARE(o.value("messages").toArray().size(), 2);
        QVERIFY(o.value("messages").toArray()[0].toObject().value("content").toString().contains("the API names"));
        const QString user = o.value("messages").toArray()[1].toObject().value("content").toString();
        QVERIFY(user.contains("TRANSCRIPT") && user.contains("OLD"));
        QCOMPARE(o.value("max_tokens").toInt(), 1024);
        ProviderConfig claude = openai; claude.protocol = "anthropic";
        auto c = QJsonDocument::fromJson(buildSummaryPayload(claude, "T")).object();
        QVERIFY(c.contains("system"));
        QCOMPARE(c.value("messages").toArray().size(), 1);
        QCOMPARE(summaryFromResponse("openai", R"({"choices":[{"message":{"content":" the summary "}}]})"), QStringLiteral("the summary"));
        QCOMPARE(summaryFromResponse("anthropic", R"({"content":[{"type":"text","text":"A"},{"type":"tool_use"},{"type":"text","text":"B"}]})"), QStringLiteral("AB"));
        QVERIFY(summaryFromResponse("openai", "garbage").isEmpty());

        Session s; s.messages = h; s.summary = "S"; s.compactedUpTo = 3;
        const auto hist = historyForRequest(s);
        QCOMPARE(hist.size(), 1 + 6);
        QVERIFY(hist.first().content.contains("S"));
        QCOMPARE(hist[1].content, QStringLiteral("u1"));
        // the person's own commands are history for the agent too
        ChatMessage sh; sh.role = "shell"; sh.content = "git status"; ToolCall tc; tc.command = "git status"; tc.output = "clean\n[exit 0]"; sh.toolCalls = {tc};
        ProviderConfig pc; pc.modelId = "m";
        const auto body = QJsonDocument::fromJson(buildRequestPayload(pc, "manual", "/w", {sh})).object().value("messages").toArray();
        QVERIFY(body.last().toObject().value("content").toString().contains("I ran `git status`"));
        QCOMPARE(body.last().toObject().value("role").toString(), QStringLiteral("user"));
    }

    void attachmentsAreReadThroughTheSandbox()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QDir(workspace.path()).mkpath("src");
        QVERIFY(writeBytes(workspace.filePath("src/a.txt"), "alpha content"));
        QVERIFY(writeBytes(workspace.filePath("b.txt"), "beta content"));
        QVERIFY(writeBytes(workspace.filePath(".env"), "SECRET=1"));
        QVERIFY(writeBytes(workspace.filePath("bin.dat"), QByteArray("a\0b", 3)));
        FakeServer server;
        QVERIFY(server.listen());
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        const auto found = agent.findFiles("a.t");
        QVERIFY(!found.isEmpty());
        QCOMPARE(found.first().toMap().value("path").toString(), QStringLiteral("src/a.txt"));
        QVERIFY(agent.findFiles(".env").isEmpty()); // private files are never offered
        QStringList notes;
        connect(&agent, &CodingAgent::statusMessage, &agent, [&notes](const QString &t) { notes << t; });
        agent.sendPrompt("Compare @src/a.txt with @b.txt, ignore @.env and @../x and @bin.dat", {"src/a.txt"});
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        const auto msgs = agent.messages();
        const auto user = msgs.first().toMap();
        QCOMPARE(user.value("attachments").toList().size(), 2); // a.txt once (picked and typed), b.txt (typed only)
        QVERIFY(user.value("content").toString().contains("<file path=\"src/a.txt\">\nalpha content"));
        QVERIFY(user.value("content").toString().contains("beta content"));
        QVERIFY(!user.value("content").toString().contains("SECRET"));
        QVERIFY(user.value("shown").toString().startsWith("Compare @src/a.txt"));
        QVERIFY(!user.value("shown").toString().contains("alpha content"));
        const QString sent = server.requests.first().body.value("messages").toArray().last().toObject().value("content").toString();
        QVERIFY(sent.contains("alpha content"));
        // a path that does not resolve is simply left as text; one that is private or binary says why
        agent.sendPrompt("again", {".env", "bin.dat", "../outside.txt"});
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QVERIFY(notes.join("|").contains("Could not attach .env"));
        QVERIFY(notes.join("|").contains("bin.dat"));
        QVERIFY(agent.messages()[2].toMap().value("attachments").toList().isEmpty());
    }

    void shellCommandsRunAtOnceAndReachTheAgent()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.setMode("plan"); // allowed in every mode: the person typed it
        QSignalSpy finished(&agent, &CodingAgent::shellFinished);
        agent.runShell("printf hello-from-shell");
        QVERIFY(agent.busy());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QCOMPARE(finished.first().at(1).toInt(), 0);
        QCOMPARE(server.requests.size(), 0); // no model turn
        auto msgs = agent.messages();
        QCOMPARE(msgs.size(), 1);
        QCOMPARE(msgs.first().toMap().value("role").toString(), QStringLiteral("shell"));
        const auto call = msgs.first().toMap().value("toolCalls").toList().first().toMap();
        QCOMPARE(call.value("status").toString(), QStringLiteral("completed"));
        QVERIFY(call.value("output").toString().contains("hello-from-shell"));
        QVERIFY(call.value("output").toString().contains("[exit 0]"));
        agent.runShell("exit 3");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
        QCOMPARE(agent.messages().last().toMap().value("toolCalls").toList().first().toMap().value("status").toString(), QStringLiteral("error"));
        // the next prompt carries what was run
        agent.sendPrompt("what did you see?");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QString all;
        for (const auto &m : server.requests.first().body.value("messages").toArray()) all += m.toObject().value("content").toString() + "\n";
        QVERIFY(all.contains("I ran `printf hello-from-shell`"));
        QVERIFY(all.contains("hello-from-shell"));
        QVERIFY(all.contains("I ran `exit 3`"));
        // a command in progress can be stopped, and nothing is owed to the agent for it
        agent.runShell("sleep 30");
        QTRY_VERIFY_WITH_TIMEOUT(agent.commandRunning(), 2000);
        agent.stop();
        QVERIFY(!agent.busy());
        auto stopped = agent.messages();
        QCOMPARE(stopped.last().toMap().value("role").toString(), QStringLiteral("note")); // "Interrupted"
        QCOMPARE(stopped[stopped.size() - 2].toMap().value("role").toString(), QStringLiteral("shell"));
        QVERIFY(stopped[stopped.size() - 2].toMap().value("toolCalls").toList().first().toMap().value("output").toString().contains("[stopped]"));
        agent.runShell("   "); // nothing to run
        QCOMPARE(agent.messages().size(), stopped.size());
    }

    void modeModelAndEffortChangeWhileTheAgentWorks()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        server.fragmentSize = 20; // the first answer arrives slowly, so the agent is busy
        server.responses.append(toolResponse({toolDelta(0, "ls", "list_files", "{}")}));
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.setMode("manual");
        agent.sendPrompt("look around");
        QTRY_VERIFY_WITH_TIMEOUT(agent.busy(), 2000);
        agent.setMode("plan");                  // all three while it works
        agent.setActiveModelId("faster-model");
        agent.setEffort("high");
        QCOMPARE(agent.mode(), QStringLiteral("plan"));
        QCOMPARE(agent.activeModelId(), QStringLiteral("faster-model"));
        QCOMPARE(agent.effort(), QStringLiteral("high"));
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        // the request already on its way was not changed; the next step uses the new settings
        QCOMPARE(server.requests[0].body.value("model").toString(), QStringLiteral("test-model"));
        QVERIFY(!server.requests[0].body.contains("reasoning_effort"));
        QCOMPARE(server.requests[1].body.value("model").toString(), QStringLiteral("faster-model"));
        QCOMPARE(server.requests[1].body.value("reasoning_effort").toString(), QStringLiteral("high"));
        QVERIFY(toolNames(server.requests[1].body.value("tools").toArray(), false).contains("write_plan"));
        QVERIFY(!toolNames(server.requests[1].body.value("tools").toArray(), false).contains("propose_edit"));
    }

    void switchingModeWithWaitingApprovals()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString file = workspace.filePath("a.txt");
        QVERIFY(writeBytes(file, "original"));
        for (const QString target : {QString("plan"), QString("auto")}) {
            init(); // a fresh configuration for each: an agent restores the tasks of the one before
            FakeServer server;
            QVERIFY(server.listen());
            server.responses.append(toolResponse({toolDelta(0, "edit", "propose_edit", QString::fromUtf8(json({{"path", "a.txt"}, {"new_text", "changed"}})))}));
            QVERIFY(writeBytes(file, "original"));
            CodingAgent agent;
            const auto id = agent.addProvider(customProvider(server.baseUrl()));
            agent.setActiveProviderId(id);
            agent.setWorkspace(workspace.path());
            agent.setMode("manual");
            agent.sendPrompt("change the file");
            QTRY_VERIFY_WITH_TIMEOUT(agent.hasPendingApprovals(), 5000);
            agent.setMode(target);
            QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
            QVERIFY(!agent.hasPendingApprovals());
            // Plan drops what was waiting; Auto (already agreed to in its dialog) takes it
            QCOMPARE(readBytes(file), target == "plan" ? QByteArray("original") : QByteArray("changed"));
            const auto call = agent.messages()[1].toMap().value("toolCalls").toList().first().toMap();
            QCOMPARE(call.value("status").toString(), target == "plan" ? QStringLiteral("rejected") : QStringLiteral("applied"));
        }
    }

    void aCutPlainAnswerIsContinued()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        const auto cut = [](const QString &text) {
            return "data: " + json(deltaEvent({{"content", text}})) + "\n\ndata: {\"choices\":[{\"delta\":{},\"finish_reason\":\"length\"}]}\n\ndata: [DONE]\n\n";
        };
        server.responses = {cut("Part one, "), "data: {\"choices\":[{\"delta\":{\"content\":\"and part two.\"}}]}\n\ndata: [DONE]\n\n"};
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("write something long");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QVERIFY(agent.lastError().isEmpty());
        const auto msgs = agent.messages();
        QCOMPARE(msgs.size(), 4); // the question, the first part, "Continue", the rest
        QCOMPARE(msgs[1].toMap().value("content").toString(), QStringLiteral("Part one, "));
        QCOMPARE(msgs[2].toMap().value("shown").toString(), QStringLiteral("Continue"));
        QCOMPARE(msgs[3].toMap().value("content").toString(), QStringLiteral("and part two."));
        const auto sent = server.requests[1].body.value("messages").toArray();
        QVERIFY(sent.last().toObject().value("content").toString().startsWith("Continue exactly where you stopped"));
        QCOMPARE(sent[sent.size() - 2].toObject().value("content").toString(), QStringLiteral("Part one, "));
        // a limit that keeps cutting does not loop for ever: four "Continue", then the person is told
        server.requests.clear();
        server.responses.clear();
        for (int i = 0; i < 8; ++i) server.responses.append(cut("more "));
        agent.sendPrompt("and again");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 5, 8000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QTest::qWait(150);
        QCOMPARE(server.requests.size(), 5);
        QCOMPARE(agent.messages().last().toMap().value("role").toString(), QStringLiteral("note"));
        QVERIFY(agent.messages().last().toMap().value("content").toString().contains("token limit"));
    }

    void aProviderThatRefusesABiggerLimit()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        const QByteArray cutTools = toolResponse({toolDelta(0, "c", "run_command", "{\"command\":\"")}).replace("\"finish_reason\":\"tool_calls\"", "\"finish_reason\":\"length\"");
        server.responses = {cutTools, R"({"error":{"message":"max_tokens is too large: 16384. This model supports at most 8192 completion tokens."}})", cutTools};
        server.statuses = {200, 400, 200};
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("do a lot");
        QTRY_VERIFY_WITH_TIMEOUT(!agent.lastError().isEmpty(), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QCOMPARE(server.requests.size(), 3);
        const int base = server.requests[0].body.value("max_tokens").toInt();
        QCOMPARE(server.requests[1].body.value("max_tokens").toInt(), base * 2);
        QCOMPARE(server.requests[2].body.value("max_tokens").toInt(), base); // back to what the provider takes
        QVERIFY(agent.lastError().contains("cut by the token limit"));
        QVERIFY(agent.lastError().contains("Max tokens"));
        QVERIFY(!agent.commandRunning());
        QCOMPARE(agent.messages().size(), 1);
    }

    void anHttpErrorSaysWhatTheProviderSaid()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        server.responses = {R"({"error":{"message":"Model not found: gpt-9"}})"};
        server.statuses = {404};
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("hi");
        QTRY_VERIFY_WITH_TIMEOUT(!agent.lastError().isEmpty(), 5000);
        QCOMPARE(agent.lastError(), QStringLiteral("HTTP 404: Model not found: gpt-9"));
    }

    void retryRulesAndBackoff()
    {
        for (int ok : {408, 429, 500, 502, 503, 504, 529}) QVERIFY2(isRetryableStatus(ok), qPrintable(QString::number(ok)));
        for (int no : {200, 400, 401, 402, 403, 404, 422}) QVERIFY2(!isRetryableStatus(no), qPrintable(QString::number(no)));
        QVERIFY(isRetryableMessage("Overloaded"));
        QVERIFY(isRetryableMessage("Rate limit reached, try again in 20s"));
        QVERIFY(isRetryableMessage("The server is temporarily unavailable"));
        QVERIFY(!isRetryableMessage("You exceeded your current quota"));
        QVERIFY(!isRetryableMessage("Invalid API key"));
        QCOMPARE(MaxRetries, 10);
        QCOMPARE(retryDelayMs(1), 1000);
        QCOMPARE(retryDelayMs(2), 2000);
        QCOMPARE(retryDelayMs(4), 8000);
        QCOMPARE(retryDelayMs(6), 30000); // never longer than 30 s
        QCOMPARE(retryDelayMs(10), 30000);
        QCOMPARE(retryDelayMs(1, 7), 7000); // the server asked for a pause
        QCOMPARE(retryDelayMs(1, 9999), 120000);
        QCOMPARE(retryDelayMs(3, 0, 10), 40);
    }

    void aBusyProviderIsAskedAgain()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        const QByteArray fail = R"({"error":{"message":"The server is overloaded"}})";
        server.responses = {fail, fail, "data: {\"choices\":[{\"delta\":{\"content\":\"Third time lucky\"}}]}\n\ndata: [DONE]\n\n"};
        server.statuses = {503, 429, 200};
        CodingAgent agent;
        agent.setRetryBaseDelayForTest(5);
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        QStringList seen;
        connect(&agent, &CodingAgent::retryChanged, &agent, [&] { if (!agent.retryStatus().isEmpty()) seen << agent.retryStatus(); });
        agent.sendPrompt("hello");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 3, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QCOMPARE(seen.size(), 2);
        QVERIFY(seen[0].contains("HTTP 503") && seen[0].contains("1/10"));
        QVERIFY(seen[1].contains("HTTP 429") && seen[1].contains("2/10"));
        QVERIFY(agent.lastError().isEmpty());
        QVERIFY(agent.retryStatus().isEmpty());
        QCOMPARE(agent.messages().last().toMap().value("content").toString(), QStringLiteral("Third time lucky"));
        QCOMPARE(agent.messages().size(), 2); // the failed tries left nothing in the chat
        // the count starts over with the next prompt: two more failures are fine again
        server.responses = {fail, fail, "data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n\ndata: [DONE]\n\n"};
        server.statuses = {503, 503, 200};
        server.requests.clear();
        agent.sendPrompt("again");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 3, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QVERIFY(agent.lastError().isEmpty());
    }

    void aWrongKeyIsNotRetried()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        server.responses = {R"({"error":{"message":"Invalid API key"}})"};
        server.statuses = {401};
        CodingAgent agent;
        agent.setRetryBaseDelayForTest(5);
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("hello");
        QTRY_VERIFY_WITH_TIMEOUT(!agent.lastError().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QTest::qWait(150);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(!agent.lastError().contains("retries"));
        // nor is a full quota, said inside a normal answer stream
        server.requests.clear();
        server.responses = {"data: {\"error\":{\"message\":\"You exceeded your current quota\"}}\n\n"};
        server.statuses = {200};
        agent.sendPrompt("again");
        QTRY_VERIFY_WITH_TIMEOUT(agent.lastError().contains("quota"), 5000);
        QTest::qWait(150);
        QCOMPARE(server.requests.size(), 1);
    }

    void retriesStopAtTen_andStopCancelsTheWait()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        const QByteArray fail = R"({"error":{"message":"Service unavailable"}})";
        for (int i = 0; i < 12; ++i) { server.responses.append(fail); server.statuses.append(503); }
        CodingAgent agent;
        agent.setRetryBaseDelayForTest(1);
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("hello");
        QTRY_VERIFY_WITH_TIMEOUT(!agent.lastError().isEmpty(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QCOMPARE(server.requests.size(), 11); // the first try and ten more
        QVERIFY(agent.lastError().contains("gave up after 10 retries"));
        QVERIFY(agent.retryStatus().isEmpty());
        // Stop while waiting cancels the next try
        server.requests.clear();
        server.responses.clear(); server.statuses.clear();
        for (int i = 0; i < 12; ++i) { server.responses.append(fail); server.statuses.append(503); }
        agent.setRetryBaseDelayForTest(400);
        agent.sendPrompt("again");
        QTRY_VERIFY_WITH_TIMEOUT(!agent.retryStatus().isEmpty(), 5000);
        QVERIFY(agent.busy()); // waiting counts as working: Send shows Stop
        agent.stop();
        QVERIFY(!agent.busy());
        QVERIFY(agent.retryStatus().isEmpty());
        QTest::qWait(900);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(agent.messages().last().toMap().value("content").toString(), QStringLiteral("Interrupted"));
    }

    void theNotchHearsWhatTheAgentNeeds()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QVERIFY(writeBytes(workspace.filePath("a.txt"), "old"));
        FakeServer server;
        QVERIFY(server.listen());
        // 1) an answer: "finished"; 2) a command that needs approval; 3) a plan; 4) an error
        server.responses.append("data: {\"choices\":[{\"delta\":{\"content\":\"All done here\"}}]}\n\ndata: [DONE]\n\n");
        server.responses.append(toolResponse({toolDelta(0, "c1", "run_command", "{\"command\":\"echo hi\"}")}));
        server.responses.append(toolResponse({toolDelta(0, "wp", "write_plan", QString::fromUtf8(json({{"content", "# Plan\n1. x"}})))}));
        server.responses.append("data: {\"choices\":[{\"delta\":{\"content\":\"Plan is ready.\"}}]}\n\ndata: [DONE]\n\n"); // the follow-up after write_plan
        server.responses.append("data: {\"error\":{\"message\":\"quota exceeded\"}}\n\n");
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.setMode("manual");
        QSignalSpy events(&agent, &CodingAgent::agentEvent);
        agent.sendPrompt("say something");
        QTRY_COMPARE_WITH_TIMEOUT(events.count(), 1, 5000);
        QCOMPARE(events.at(0).at(0).toString(), QStringLiteral("done"));
        QVERIFY(events.at(0).at(2).toString().contains("All done here"));
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        agent.sendPrompt("run a command");
        QTRY_COMPARE_WITH_TIMEOUT(events.count(), 2, 5000);
        QCOMPARE(events.at(1).at(0).toString(), QStringLiteral("approval"));
        QVERIFY(events.at(1).at(2).toString().contains("echo hi"));
        QVERIFY(agent.hasPendingApprovals());
        agent.stop();
        QTest::qWait(150);
        QCOMPARE(events.count(), 2); // stopping is not news
        agent.sendPrompt("let's plan this");
        QTRY_COMPARE_WITH_TIMEOUT(events.count(), 3, 5000);
        QCOMPARE(events.at(2).at(0).toString(), QStringLiteral("plan"));
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QTest::qWait(150);
        QCOMPARE(events.count(), 3); // no "finished" on top of "plan ready"
        agent.rejectPlan();
        agent.sendPrompt("and now?");
        QTRY_COMPARE_WITH_TIMEOUT(events.count(), 4, 5000);
        QCOMPARE(events.at(3).at(0).toString(), QStringLiteral("error"));
        QVERIFY(events.at(3).at(2).toString().contains("quota exceeded"));
    }

    void stopKeepsWhatWasWritten()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        server.fragmentSize = 25; // the answer arrives slowly, in small pieces
        QByteArray body;
        for (int i = 0; i < 60; ++i) body += "data: {\"choices\":[{\"delta\":{\"content\":\"word" + QByteArray::number(i) + " \"}}]}\n\n";
        body += "data: [DONE]\n\n";
        server.responses.append(body);
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("Write a long answer");
        QTRY_VERIFY_WITH_TIMEOUT(agent.streamingContent().size() > 8, 5000);
        QVERIFY(agent.busy());
        agent.stop(); // what Esc and the Stop button call
        QVERIFY(!agent.busy());
        QVERIFY(!agent.streaming());
        const auto msgs = agent.messages();
        QCOMPARE(msgs.size(), 3);
        QCOMPARE(msgs[1].toMap().value("role").toString(), QStringLiteral("assistant"));
        QVERIFY(msgs[1].toMap().value("content").toString().startsWith("word0"));
        QCOMPARE(msgs[2].toMap().value("role").toString(), QStringLiteral("note"));
        QCOMPARE(msgs[2].toMap().value("content").toString(), QStringLiteral("Interrupted"));
        // stopping when nothing runs leaves the chat alone, and the notes are never sent to the model
        agent.stop();
        QCOMPARE(agent.messages().size(), 3);
        server.responses.clear();
        agent.sendPrompt("go on");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        for (const auto &m : server.requests.last().body.value("messages").toArray())
            QVERIFY(!m.toObject().value("content").toString().contains("Interrupted"));
    }

    void longOutputIsShownByItsEndButKeptWhole()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        QSignalSpy finished(&agent, &CodingAgent::shellFinished);
        agent.runShell("seq 1 5000");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        const auto call = agent.messages().first().toMap().value("toolCalls").toList().first().toMap();
        const QString shown = call.value("output").toString();
        QVERIFY(shown.startsWith("[… "));
        QVERIFY(shown.contains("earlier lines hidden"));
        QVERIFY(shown.contains("5000\n"));
        QVERIFY(shown.count('\n') <= 152);                     // the panel only lays out the end
        QVERIFY(shown.size() < 13000);
        const QString all = agent.fullText(agent.messages().first().toMap().value("id").toString(), call.value("id").toString());
        QVERIFY(all.startsWith("1\n2\n3\n"));                  // "Copy all output" gives everything
        QVERIFY(all.contains("\n5000\n"));
        QVERIFY(all.size() > shown.size() * 2);
        // and the agent is shown all of it, not the tail
        agent.sendPrompt("what happened?");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 5000);
        QString sent;
        for (const auto &m : server.requests.first().body.value("messages").toArray()) sent += m.toObject().value("content").toString();
        QVERIFY(sent.contains("\n5000\n")); // the end of the output, where results and errors are
        QVERIFY(sent.contains("[output cut: the last 8000 characters]"));
        QVERIFY(!sent.contains("earlier lines hidden"));
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        // short output is untouched
        agent.runShell("printf short");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
        QVERIFY(agent.messages().last().toMap().value("toolCalls").toList().first().toMap().value("output").toString().startsWith("short"));
    }

    void idsFromTheFileNeverReachThePathOfAnotherFolder()
    {
        for (const QString id : {QString("../.."), QString("../../x"), QString("/etc"), QString("a/b"), QString(""), QString(".."), QString("a..b"), QString(".hidden"), QString(64 + 1, 'a')})
            QVERIFY2(!isSafeId(id), qPrintable(id));
        for (const QString id : {QString("s1"), newId(), QString("mimo"), QString("deepseek-2"), QString("a.b_c-d")})
            QVERIFY2(isSafeId(id), qPrintable(id));
        // a task whose id in agent.json is a path gets a new, plain id when loaded, and deleting it touches nothing else
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        QVERIFY(writeBytes(outside.filePath("precious.txt"), "keep me"));
        const QString dir = testHome().path() + "/config/notch/dev";
        QDir().mkpath(dir);
        const QString evil = "../../../../../.." + QFileInfo(outside.path()).absoluteFilePath();
        QJsonObject session{{"id", evil}, {"title", "evil"}, {"workspace", ""}, {"mode", "manual"}, {"messages", QJsonArray{}}, {"plan", "# p"}, {"planStatus", "pending"}};
        QVERIFY(writeBytes(dir + "/agent.json", QJsonDocument(QJsonObject{{"version", 1}, {"providers", QJsonArray{}}, {"sessions", QJsonArray{session}}, {"sessionId", evil}}).toJson()));
        CodingAgent agent;
        QCOMPARE(agent.sessions().size(), 1);
        const QString id = agent.sessions().first().toMap().value("id").toString();
        QVERIFY(isSafeId(id));
        QVERIFY(id != evil);
        agent.deleteSession(id);
        QVERIFY(QFile::exists(outside.filePath("precious.txt")));
        QVERIFY(QDir(outside.path()).exists());
        // an unsafe provider id is not kept either (it would name a saved key)
        ProviderConfig p;
        p.id = "../x"; p.name = "n"; p.baseUrl = "https://x.test"; p.modelId = "m";
        QVERIFY(!providerFromJson(providerToJson(p)).id.isEmpty());
        QVERIFY(!isSafeId(providerFromJson(providerToJson(p)).id));
    }

    void answersCannotLoadPicturesOrOpenOddLinks()
    {
        MarkdownHtml::Style st;
        const QString html = MarkdownHtml::render("Look ![chart](http://evil.test/p.png?d=secret) and [file](file:///etc/passwd), "
                                                  "[app](vscode://x/y), [ok](https://ok.test/a) and <img src=\"http://evil.test/raw\"> <a href=\"http://evil.test\">raw</a>.", st);
        QVERIFY(!html.contains("<img"));
        QVERIFY(!html.contains("evil.test/p.png"));
        QVERIFY(html.contains("[image"));
        QVERIFY(!html.contains("file:///etc/passwd"));
        QVERIFY(!html.contains("vscode://"));
        QVERIFY(html.contains("https://ok.test/a"));
        QVERIFY(!html.contains("href=\"http://evil.test\""));
        QVERIFY(!html.contains("#0000ff")); // what is not a link does not look like one
        QVERIFY(isSafeLink("https://example.test/x"));
        QVERIFY(isSafeLink("http://example.test"));
        QVERIFY(isSafeLink("mailto:a@b.test"));
        for (const char *bad : {"file:///etc/passwd", "smb://host/share", "javascript:alert(1)", "vscode://x", "data:text/html,x", "", "ftp://x.test", "//evil.test"})
            QVERIFY2(!isSafeLink(bad), bad);
        CodingAgent agent;
        QVERIFY(!agent.openLink("file:///etc/passwd")); // refused before anything is launched
    }

    void keysAreReadOnceAndStayCurrent()
    {
        FakeServer server;
        QVERIFY(server.listen());
        CodingAgent agent;
        auto fields = customProvider(server.baseUrl());
        fields.insert("apiKey", "sk-one");
        const auto id = agent.addProvider(fields);
        QVERIFY(agent.providerById(id).value("hasApiKey").toBool());
        const QString secrets = testHome().path() + "/config/notch/dev/secrets.json";
        QVERIFY(readBytes(secrets).contains("sk-one"));
        // reading the providers does not read the file: it still works with the file gone
        QVERIFY(QFile::remove(secrets));
        for (int i = 0; i < 50; ++i) QVERIFY(agent.providers().size() >= 1);
        QVERIFY(agent.providerById(id).value("hasApiKey").toBool());
        // a change goes to the file and to what is kept
        QVERIFY(agent.updateProvider(id, {{"apiKey", "sk-two"}}));
        QVERIFY(readBytes(secrets).contains("sk-two"));
        QVERIFY(agent.updateProvider(id, {{"apiKey", ""}}));
        QVERIFY(!agent.providerById(id).value("hasApiKey").toBool());
        QVERIFY(!readBytes(secrets).contains("sk-two"));
        { CodingAgent other; QVERIFY(!other.providerById(id).value("hasApiKey").toBool()); }
    }

    void savingStaysCheap()
    {
        // 200 tasks of 100 messages (some with long command output): what one small change costs, and what is written for it
        QJsonArray sessions;
        for (int i = 0; i < 200; ++i) {
            QJsonArray msgs;
            for (int j = 0; j < 100; ++j) {
                QJsonObject m{{"id", QString("m%1-%2").arg(i).arg(j)}, {"role", j % 2 ? "assistant" : "user"}, {"content", QString("message %1 ").repeated(40)}, {"toolCalls", QJsonArray{}}};
                if (j % 10 == 5) m.insert("toolCalls", QJsonArray{QJsonObject{{"id", QString("t%1-%2").arg(i).arg(j)}, {"name", "run_command"}, {"status", "completed"}, {"output", QString("line of output\n").repeated(2000)}}});
                msgs.append(m);
            }
            sessions.append(QJsonObject{{"id", QString("s%1").arg(i)}, {"title", QString("task %1").arg(i)}, {"workspace", QString()}, {"mode", "manual"}, {"messages", msgs}});
        }
        const QString dir = testHome().path() + "/config/notch/dev";
        QDir().mkpath(dir);
        QVERIFY(writeBytes(dir + "/agent.json", QJsonDocument(QJsonObject{{"version", 1}, {"providers", QJsonArray{}}, {"sessions", sessions}, {"sessionId", "s0"}}).toJson(QJsonDocument::Compact)));
        QElapsedTimer clock;
        clock.start();
        CodingAgent agent;
        const qint64 loadMs = clock.elapsed();
        QCOMPARE(agent.sessions().size(), 200);
        clock.restart();
        for (int i = 0; i < 20; ++i) agent.renameSession("s7", QString("renamed %1").arg(i));
        const qint64 saveMs = clock.elapsed();
        qInfo() << "BENCH load ms" << loadMs << "20 small changes ms" << saveMs << "agent.json bytes" << QFileInfo(dir + "/agent.json").size();
        QVERIFY(saveMs < 2000); // was ~6400 ms when every change rewrote all 76 MB
        // Linux only: 0 elsewhere
        auto rssMb = [] { QFile f("/proc/self/status"); f.open(QIODevice::ReadOnly); const QByteArray all = f.readAll(); const int at = all.indexOf("VmRSS:"); return at < 0 ? 0 : all.mid(at + 6, 12).simplified().split(' ').first().toInt() / 1024; };
        agent.flush();
        const int before = rssMb();
        agent.releaseIdle();
        const int afterRelease = rssMb();
#ifdef Q_OS_LINUX
        malloc_trim(0);
#endif
        qInfo() << "BENCH rss MB before idle" << before << "after release" << afterRelease << "after trim" << rssMb();
    }

    void storageKeepsOneFilePerTask()
    {
        const QString dir = testHome().path() + "/config/notch/dev";
        QDir().mkpath(dir);
        auto task = [](const QString &id, const QString &title) {
            return QJsonObject{{"id", id}, {"title", title}, {"workspace", QString()}, {"mode", "manual"},
                {"messages", QJsonArray{QJsonObject{{"id", "m1"}, {"role", "user"}, {"content", "hello " + title}, {"toolCalls", QJsonArray{}}}}}};
        };
        // an old all-in-one file is split once; the old file stays as a backup and nothing is lost
        QVERIFY(writeBytes(dir + "/agent.json", QJsonDocument(QJsonObject{{"version", 1}, {"providers", QJsonArray{}},
            {"sessions", QJsonArray{task("a1", "first"), task("b2", "second")}}, {"sessionId", "b2"}}).toJson()));
        {
            CodingAgent agent;
            QCOMPARE(agent.sessions().size(), 2);
            QCOMPARE(agent.activeSessionId(), QStringLiteral("b2"));
        }
        QVERIFY(QFile::exists(dir + "/sessions/a1.json") && QFile::exists(dir + "/sessions/b2.json"));
        QVERIFY(QFile::exists(dir + "/agent.json.v1.bak"));
        QVERIFY(!readBytes(dir + "/agent.json").contains("hello first")); // the settings file no longer holds the tasks
        QCOMPARE(QFileInfo(dir + "/sessions/a1.json").permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther), QFileDevice::Permissions());
        {
            CodingAgent again; // the new layout loads back the same
            QCOMPARE(again.sessions().size(), 2);
            QVERIFY(readBytes(dir + "/sessions/a1.json").contains("hello first"));
        }
        // a change writes the changed task only
        const QDateTime before = QFileInfo(dir + "/sessions/a1.json").lastModified();
        QTest::qWait(1100);
        {
            CodingAgent agent;
            agent.renameSession("b2", "renamed");
            QCOMPARE(QFileInfo(dir + "/sessions/a1.json").lastModified(), before);
            QVERIFY(readBytes(dir + "/sessions/b2.json").contains("renamed"));
        }
        // deferred: many changes, no write until the delay (or flush, or the destructor)
        CodingAgent::setSaveDelay(200);
        {
            CodingAgent agent;
            for (int i = 0; i < 5; ++i) agent.renameSession("a1", QString("late %1").arg(i));
            QVERIFY(!readBytes(dir + "/sessions/a1.json").contains("late 4"));
            QTRY_VERIFY_WITH_TIMEOUT(readBytes(dir + "/sessions/a1.json").contains("late 4"), 3000);
            agent.renameSession("a1", "by flush");
            agent.flush();
            QVERIFY(readBytes(dir + "/sessions/a1.json").contains("by flush"));
            agent.renameSession("a1", "by destructor");
        }
        QVERIFY(readBytes(dir + "/sessions/a1.json").contains("by destructor"));
        CodingAgent::setSaveDelay(0);
        // deleting a task removes its file; an id in the order list that is a path is ignored
        {
            CodingAgent agent;
            agent.deleteSession("a1");
        }
        QVERIFY(!QFile::exists(dir + "/sessions/a1.json"));
        auto settings = QJsonDocument::fromJson(readBytes(dir + "/agent.json")).object();
        settings.insert("sessionOrder", QJsonArray{"../agent", "b2"});
        QVERIFY(writeBytes(dir + "/agent.json", QJsonDocument(settings).toJson()));
        {
            CodingAgent agent;
            QCOMPARE(agent.sessions().size(), 1);
        }
        // a task file over the limit is left alone
        QVERIFY(writeBytes(dir + "/sessions/b2.json", QByteArray(AgentStore::MaxTaskFile + 1, ' ')));
        QCOMPARE(AgentStore::load().sessions.size(), 0);
    }

    void idleLetsGoOfOtherTasksAndTheyComeBack()
    {
        const QString dir = testHome().path() + "/config/notch/dev";
        QDir().mkpath(dir);
        auto task = [](const QString &id) {
            return QJsonObject{{"id", id}, {"title", "t " + id}, {"workspace", QString()}, {"mode", "manual"},
                {"messages", QJsonArray{QJsonObject{{"id", "m1"}, {"role", "user"}, {"content", "hello " + id}, {"toolCalls", QJsonArray{}}}}}};
        };
        QVERIFY(writeBytes(dir + "/agent.json", QJsonDocument(QJsonObject{{"version", 1}, {"providers", QJsonArray{}},
            {"sessions", QJsonArray{task("a1"), task("b2")}}, {"sessionId", "a1"}}).toJson()));
        CodingAgent agent;
        agent.releaseIdle();
        QCOMPARE(agent.messageCount(), 1);                      // the task in use stays whole
        agent.selectSession("b2");
        QCOMPARE(agent.messageCount(), 1);                      // the other one is read back from its file
        QCOMPARE(agent.messages().first().toMap().value("content").toString(), QStringLiteral("hello b2"));
        agent.releaseIdle();
        agent.renameSession("a1", "renamed while let go");      // changing a let-go task reads it back first, so nothing is lost
        agent.selectSession("a1");
        QCOMPARE(agent.messageCount(), 1);
        agent.flush();
        QVERIFY(readBytes(dir + "/sessions/a1.json").contains("hello a1"));
        QVERIFY(readBytes(dir + "/sessions/b2.json").contains("hello b2"));
    }

    void workspaceToolsStayInsideTheFolder()
    {
        QTemporaryDir ws;
        QVERIFY(ws.isValid());
        QVERIFY(writeBytes(ws.path() + "/a.txt", "one\ntwo\n"));
        QVERIFY(writeBytes(ws.path() + "/.env", "SECRET=1"));
        QVERIFY(writeBytes(ws.path() + "/bin.dat", QByteArray("x\0y", 3)));
        QVERIFY(writeBytes(ws.path() + "/big.txt", QByteArray(AgentTools::FileLimit + 1, 'a')));
        QString summary;
        QVERIFY(AgentTools::readFile(ws.path(), {{"path", "a.txt"}}, &summary).contains("2\ttwo"));
        for (const char *bad : {".env", "../outside", "/etc/passwd", "bin.dat", "big.txt"})
            QVERIFY2(AgentTools::readFile(ws.path(), {{"path", bad}}, &summary).startsWith("Error:"), bad);
        QVERIFY(!AgentTools::listFiles(ws.path(), {}, &summary).contains(".env"));
        QVERIFY(!AgentTools::searchFiles(ws.path(), {{"pattern", "SECRET"}}, &summary).contains(".env:"));
        QVERIFY(AgentTools::fileIndex(ws.path()).contains("a.txt") && !AgentTools::fileIndex(ws.path()).contains(".env"));
        ToolCall tc;
        AgentTools::proposeEdit(ws.path(), tc, {{"path", "a.txt"}, {"old_text", "two"}, {"new_text", "2"}});
        QVERIFY(tc.status != "error");
        QVERIFY(writeBytes(ws.path() + "/a.txt", "changed behind our back"));
        QVERIFY(AgentTools::applyEdit(ws.path(), tc).startsWith("Error: file changed"));
        AgentTools::proposeEdit(ws.path(), tc, {{"path", "a.txt"}, {"old_text", "changed"}, {"new_text", "kept"}});
        QCOMPARE(AgentTools::applyEdit(ws.path(), tc), QStringLiteral("Applied a.txt"));
        QCOMPARE(readBytes(ws.path() + "/a.txt"), QByteArray("kept behind our back"));
        ToolCall escape;
        AgentTools::proposeEdit(ws.path(), escape, {{"path", "../x.txt"}, {"new_text", "no"}});
        QCOMPARE(escape.status, QStringLiteral("error"));
    }

    void toolCallsReadAsOneLine()
    {
        auto call = [](const QString &name, const QString &args, const QString &status) { ToolCall t; t.name = name; t.argumentsJson = args; t.status = status; return t; };
        auto h = toolHeadline(call("read_file", R"({"path":"src/loans/service.ts","start_line":10,"end_line":80})", "completed"));
        QCOMPARE(h.verb, QStringLiteral("Read")); QCOMPARE(h.target, QStringLiteral("src/loans/service.ts")); QCOMPARE(h.stat, QStringLiteral("lines 10–80")); QCOMPARE(h.icon, QStringLiteral("eye"));
        h = toolHeadline(call("list_files", "{}", "completed"));
        QCOMPARE(h.verb, QStringLiteral("Listed")); QCOMPARE(h.target, QStringLiteral("."));
        auto search = call("search_files", R"({"pattern":"lateFee"})", "completed");
        search.output = "a.ts:3: lateFee\nb.ts:9: lateFee\n";
        h = toolHeadline(search);
        QCOMPARE(h.verb, QStringLiteral("Searched")); QVERIFY(h.target.contains("lateFee")); QCOMPARE(h.stat, QStringLiteral("2 hits"));
        auto edit = call("propose_edit", R"({"path":"a.txt","new_text":"x"})", "awaiting_approval");
        edit.filePath = "src/a.txt"; edit.addedLines = 12; edit.removedLines = 3; edit.originalExists = true;
        h = toolHeadline(edit);
        QCOMPARE(h.verb, QStringLiteral("Proposed edit to")); QCOMPARE(h.stat, QStringLiteral("+12 −3")); QCOMPARE(h.tone, QStringLiteral("accent"));
        edit.status = "applied";
        h = toolHeadline(edit);
        QCOMPARE(h.verb, QStringLiteral("Edited")); QCOMPARE(h.tone, QStringLiteral("ok"));
        edit.originalExists = false;
        QCOMPARE(toolHeadline(edit).verb, QStringLiteral("Created"));
        edit.status = "rejected";
        QCOMPARE(toolHeadline(edit).tone, QStringLiteral("warn"));
        auto cmd = call("run_command", "{}", "completed");
        cmd.command = "bun test"; cmd.output = "2 pass\n1 fail\n[exit 1]";
        h = toolHeadline(cmd);
        QCOMPARE(h.verb, QStringLiteral("Ran")); QCOMPARE(h.stat, QStringLiteral("exit 1")); QCOMPARE(h.tone, QStringLiteral("bad"));
        cmd.output = "ok\n[exit 0]";
        QCOMPARE(toolHeadline(cmd).tone, QStringLiteral("ok"));
        cmd.status = "awaiting_approval";
        QCOMPARE(toolHeadline(cmd).verb, QStringLiteral("Wants to run"));
        auto todo = call("todo_write", "{}", "completed"); todo.summary = "To-do updated · 3/7";
        QCOMPARE(toolHeadline(todo).stat, QStringLiteral("3/7"));
        QCOMPARE(toolHeadline(call("write_plan", "{}", "completed")).verb, QStringLiteral("Wrote plan"));
        QCOMPARE(toolHeadline(call("mystery", "{}", "error")).stat, QStringLiteral("failed"));
        const QString longPath = "src/" + QString("deeply/nested/").repeated(8) + "file.ts";
        const auto lh = toolHeadline(call("read_file", QString::fromUtf8(json({{"path", longPath}})), "completed"));
        QVERIFY(lh.target.size() <= 56 && lh.target.contains("…") && lh.target.endsWith("file.ts"));
    }

    void theEndOfARunSaysWhatChanged()
    {
        auto msg = [](const QString &role, const QString &text) { ChatMessage m; m.id = text; m.role = role; m.content = text; return m; };
        auto edit = [](const QString &path, const QString &status, int add, int rem, bool existed) {
            ToolCall t; t.id = path + status; t.name = "propose_edit"; t.status = status; t.filePath = path; t.addedLines = add; t.removedLines = rem; t.originalExists = existed; return t;
        };
        auto cmd = [](const QString &c, const QString &status, const QString &out) { ToolCall t; t.id = c; t.name = "run_command"; t.command = c; t.status = status; t.output = out; return t; };
        auto read = [](const QString &status) { ToolCall t; t.id = "r" + status; t.name = "read_file"; t.status = status; return t; };
        ChatMessage old = msg("assistant", "old run");
        old.toolCalls = {edit("old.txt", "applied", 9, 9, true)};
        ChatMessage a = msg("assistant", "work");
        a.toolCalls = {read("completed"), edit("src/a.ts", "applied", 12, 3, true), edit("src/new.ts", "applied", 40, 0, false), edit("src/a.ts", "applied", 2, 1, true),
                       edit("src/skipped.ts", "rejected", 5, 5, true), cmd("bun test", "completed", "1 fail\n[exit 1]"), cmd("bun build", "completed", "ok\n[exit 0]")};
        ChatMessage b = msg("assistant", "more");
        b.toolCalls = {cmd("lint", "error", "boom"), read("rejected")}; // a declined read is not news
        QList<ChatMessage> run{msg("user", "first request"), old, msg("user", "fix it"), a, msg("tool", "result"), b};
        const RunSummary r = summarizeRun(run);
        QCOMPARE(r.files.size(), 2);                                  // a file edited twice is one line
        QCOMPARE(r.files[0].path, QStringLiteral("src/a.ts"));
        QCOMPARE(r.files[0].verb, QStringLiteral("Edited"));
        QCOMPARE(r.files[0].added, 14); QCOMPARE(r.files[0].removed, 4);
        QCOMPARE(r.files[1].verb, QStringLiteral("Created"));
        QCOMPARE(r.added, 54); QCOMPARE(r.removed, 4);
        QCOMPARE(r.commands, 3);
        QCOMPARE(r.failedCommands, 2);
        QCOMPARE(r.failed, (QStringList{"bun test", "lint"}));
        QCOMPARE(r.declined, 1);
        QVERIFY(r.any());
        // only this run counts, and "Continue" after a cut answer does not start a new one
        ChatMessage cont = msg("user", "Continue exactly"); cont.shown = "Continue";
        QCOMPARE(summarizeRun(QList<ChatMessage>{msg("user", "q"), a, cont, b}).files.size(), 2);
        QVERIFY(!summarizeRun(QList<ChatMessage>{msg("user", "just a question"), msg("assistant", "an answer")}).any());
        QVERIFY(!summarizeRun({}).any());
        // the agent is also told to close with a few words about it
        QVERIFY(systemPromptFor("manual", "/w").contains("easy to scan"));
        QVERIFY(systemPromptFor("auto", "/w").contains("**Changed**"));
        QVERIFY(!systemPromptFor("plan", "/w").contains("easy to scan"));
    }

    void detailLevelIsRememberedAndLinesComeWithMessages()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        server.responses.append(toolResponse({toolDelta(0, "ls", "list_files", "{}")}));
        CodingAgent agent;
        QCOMPARE(agent.detail(), QStringLiteral("headings"));     // the light view is the default
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("look");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        const auto call = agent.messages()[1].toMap().value("toolCalls").toList().first().toMap();
        QCOMPARE(call.value("verb").toString(), QStringLiteral("Listed"));
        QCOMPARE(call.value("icon").toString(), QStringLiteral("folder"));
        agent.setDetail("FULL");
        QCOMPARE(agent.detail(), QStringLiteral("full"));
        { CodingAgent restored; QCOMPARE(restored.detail(), QStringLiteral("full")); }
        agent.setDetail("nonsense");
        QCOMPARE(agent.detail(), QStringLiteral("headings"));
        QVERIFY(AgentCommands::parseSlash("/details full").ok);
    }

    void todoListFromTheAgent()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        const QString args = QString::fromUtf8(json({{"todos", QJsonArray{QJsonObject{{"content", "Read the code"}, {"status", "completed"}},
                                                                          QJsonObject{{"content", "Write the fix"}, {"status", "in_progress"}},
                                                                          QJsonObject{{"content", "Run the tests"}, {"status", "pending"}}}}}));
        server.responses.append(toolResponse({toolDelta(0, "td", "todo_write", args)}));
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.setMode("manual");
        QSignalSpy changed(&agent, &CodingAgent::todosChanged);
        agent.sendPrompt("Fix the bug in three steps");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QVERIFY(changed.count() > 0);
        const auto todos = agent.todos();
        QCOMPARE(todos.size(), 3);
        QCOMPARE(todos[1].toMap().value("status").toString(), QStringLiteral("in_progress"));
        const auto call = agent.messages()[1].toMap().value("toolCalls").toList().first().toMap();
        QCOMPARE(call.value("status").toString(), QStringLiteral("completed")); // no approval needed
        QVERIFY(call.value("summary").toString().contains("1/3"));
        // the list rides along with the next request, so it survives anything
        const QString system = server.requests.last().body.value("messages").toArray().first().toObject().value("content").toString();
        QVERIFY(system.contains("Your current to-do list"));
        QVERIFY(system.contains("[~] Write the fix"));
        QVERIFY(system.contains("todo_write"));
        { CodingAgent restored; QCOMPARE(restored.todos().size(), 3); }
        // Plan Mode keeps to its own tool
        agent.setMode("plan");
        QVERIFY(!toolNames(openAiToolsForMode("plan"), false).contains("todo_write"));
        agent.clearSession();
        QVERIFY(agent.todos().isEmpty());
    }

    void forkCopiesTheTask()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("First question");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        const QString original = agent.activeSessionId();
        const QString copy = agent.forkSession();
        QVERIFY(!copy.isEmpty() && copy != original);
        QCOMPARE(agent.activeSessionId(), copy);
        QCOMPARE(agent.messages().size(), 2);
        QCOMPARE(agent.sessions().size(), 2);
        QVERIFY(agent.sessions().first().toMap().value("title").toString().startsWith("Fork of"));
        agent.sendPrompt("Only in the fork");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QCOMPARE(agent.messages().size(), 4);
        agent.selectSession(original);
        QCOMPARE(agent.messages().size(), 2); // the original is untouched
        QVERIFY(agent.lastAnswer().contains("Finished"));
        // export and notes
        agent.addNote("a divider");
        QCOMPARE(agent.messages().last().toMap().value("role").toString(), QStringLiteral("note"));
        const QString out = workspace.filePath("export.md");
        QVERIFY(agent.exportSession(out));
        const QByteArray md = readBytes(out);
        QVERIFY(md.contains("## You") && md.contains("First question") && md.contains("## Agent") && md.contains("a divider"));
    }

    void fullContextCompactsByItself()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        auto fields = customProvider(server.baseUrl());
        fields.insert("contextWindow", 4096); // about 3500 tokens (14 KB) is "nearly full"
        CodingAgent agent;
        const auto id = agent.addProvider(fields);
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        QCOMPARE(agent.contextWindow(), 4096);
        const QString big(3500, 'x');
        for (int i = 1; i <= 4; ++i) {
            agent.sendPrompt(QString("question %1 ").arg(i) + big);
            QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), i, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        }
        QCOMPARE(agent.messages().size(), 8);
        QVERIFY(agent.contextUsed() > 3000);
        // the fifth request finds the context nearly full: a summary is asked for first (no tools, no stream), then the real turn goes out
        server.responses.append(QByteArray()); // placeholders for requests 0..3 (already served)
        server.responses.clear();
        for (int i = 0; i < 4; ++i) server.responses.append("data: {\"choices\":[{\"delta\":{\"content\":\"Finished\"}}]}\n\ndata: [DONE]\n\n");
        server.responses.append(R"({"choices":[{"message":{"content":"SUMMARY: the user asked four questions."}}]})");
        QSignalSpy notes(&agent, &CodingAgent::statusMessage);
        agent.sendPrompt("question 5 " + big);
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 6, 8000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 8000);
        const auto summaryRequest = server.requests[4].body;
        QVERIFY(!summaryRequest.value("stream").toBool());
        QVERIFY(!summaryRequest.contains("tools"));
        QVERIFY(summaryRequest.value("messages").toArray().last().toObject().value("content").toString().contains("question 1"));
        const auto real = server.requests[5].body.value("messages").toArray();
        QString joined;
        for (const auto &m : real) joined += m.toObject().value("content").toString() + "\n";
        QVERIFY(joined.contains("SUMMARY: the user asked four questions."));
        QVERIFY(!joined.contains("question 1 ")); // the old messages are no longer sent
        QVERIFY(joined.contains("question 5"));
        QVERIFY(joined.contains("question 4")); // the recent ones are kept
        bool sawNote = false;
        for (const auto &m : agent.messages()) sawNote = sawNote || m.toMap().value("role").toString() == "note";
        QVERIFY(sawNote);
        bool told = false;
        for (const auto &n : notes) told = told || n.at(0).toString().contains("summarized");
        QVERIFY(told);
        { CodingAgent restored; QVERIFY(restored.contextUsed() < agent.contextWindow()); }
        // manual /compact with nothing old enough says so instead of asking
        agent.clearSession();
        agent.compactNow();
        QCOMPARE(server.requests.size(), 6);
    }

    void asksForPlan_data()
    {
        QTest::addColumn<QString>("prompt");
        QTest::addColumn<bool>("yes");
        QTest::newRow("english") << "Make a plan to add auth" << true;
        QTest::newRow("planning") << "I am planning a refactor, let us talk" << true;
        QTest::newRow("indonesian plan") << "buatkan plan untuk fitur login" << true;
        QTest::newRow("rencana") << "Bikin rencana migrasi database" << true;
        QTest::newRow("discuss") << "Let's discuss the architecture first" << true;
        QTest::newRow("diskusi") << "diskusi dulu soal struktur folder" << true;
        QTest::newRow("fix") << "Fix the failing test in loans" << false;
        QTest::newRow("planner is another word") << "Explain how the planner works" << false;
        QTest::newRow("airplane") << "Rename airplane to jet" << false;
        QTest::newRow("a file called plan.md") << "Format plan.md with prettier" << false;
    }
    void asksForPlan()
    {
        QFETCH(QString, prompt);
        QFETCH(bool, yes);
        QCOMPARE(CodingAgentModel::asksForPlan(prompt), yes);
    }

    void planIsWrittenThenAccepted()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        const QString plan = "# Plan\n1. Add the login route\n2. Test it";
        server.responses.append(toolResponse({toolDelta(0, "wp", "write_plan", QString::fromUtf8(json({{"content", plan}})))}));
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.setMode("manual");
        agent.sendPrompt("Please make a plan for the login page");
        QCOMPARE(agent.mode(), QStringLiteral("plan")); // asking for a plan lowered the mode by itself
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QCOMPARE(agent.planStatus(), QStringLiteral("pending"));
        QCOMPARE(agent.plan(), plan);
        QCOMPARE(readBytes(agent.planPath()), plan.toUtf8());
        QVERIFY(agent.planPath().endsWith("/plan.md"));
        QVERIFY(!QFile::exists(workspace.filePath("plan.md"))); // Plan Mode leaves the workspace alone
        { CodingAgent restored; QCOMPARE(restored.planStatus(), QStringLiteral("pending")); QCOMPARE(restored.plan(), plan); }

        agent.acceptPlan("manual");
        QCOMPARE(agent.mode(), QStringLiteral("manual"));
        QCOMPARE(agent.planStatus(), QStringLiteral("accepted"));
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 3, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        const auto sent = server.requests.last().body.value("messages").toArray();
        const QString last = sent.last().toObject().value("content").toString();
        QVERIFY(last.contains("--- plan.md ---"));
        QVERIFY(last.contains("Add the login route"));
        QVERIFY(toolNames(server.requests.last().body.value("tools").toArray(), false).contains("propose_edit"));
        agent.acceptPlan("auto"); // nothing pending any more: ignored
        QCOMPARE(agent.mode(), QStringLiteral("manual"));
    }

    void commentsAndRevisionsSeeThePlan()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        server.responses.append(toolResponse({toolDelta(0, "wp", "write_plan", QString::fromUtf8(json({{"content", "# Plan\n1. First step"}})))}));
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("Let's plan the work");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QCOMPARE(agent.planStatus(), QStringLiteral("pending"));

        // a plain comment: the agent gets the plan and the discuss-or-revise rules, and the plan stays pending
        agent.sendPrompt("Why is step 1 first?");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 3, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        const QString system = server.requests.last().body.value("messages").toArray().first().toObject().value("content").toString();
        QVERIFY(system.contains("A plan is waiting"));
        QVERIFY(system.contains("1. First step"));
        QVERIFY(system.contains("discuss first, or revise the plan"));
        QCOMPARE(agent.planStatus(), QStringLiteral("pending"));
        QCOMPARE(agent.mode(), QStringLiteral("plan"));

        // Revise: an explicit request
        agent.revisePlan("make it shorter");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 4, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QCOMPARE(server.requests.last().body.value("messages").toArray().last().toObject().value("content").toString(),
                 QStringLiteral("Please revise the plan: make it shorter"));

        // rejected, then reopened, and readable the whole time
        agent.rejectPlan();
        QCOMPARE(agent.planStatus(), QStringLiteral("rejected"));
        QVERIFY(!agent.plan().isEmpty());
        agent.reopenPlan();
        QCOMPARE(agent.planStatus(), QStringLiteral("pending"));
        agent.acceptPlan("manual");
        QCOMPARE(agent.planStatus(), QStringLiteral("accepted"));
        QVERIFY(!agent.plan().isEmpty()); // the plan stays readable after it is accepted
        agent.reopenPlan(); // only a rejected plan reopens
        QCOMPARE(agent.planStatus(), QStringLiteral("accepted"));
    }

    void planCanBeRejected()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        server.responses.append(toolResponse({toolDelta(0, "wp", "write_plan", QString::fromUtf8(json({{"content", "# Plan\n- nothing"}})))}));
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("Let's discuss a plan");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QCOMPARE(agent.planStatus(), QStringLiteral("pending"));
        agent.rejectPlan();
        QCOMPARE(agent.planStatus(), QStringLiteral("rejected"));
        QCOMPARE(agent.mode(), QStringLiteral("plan"));
        QVERIFY(agent.messages().last().toMap().value("content").toString().contains("reject"));
        QTest::qWait(150);
        QCOMPARE(server.requests.size(), 2); // a rejection starts no work
        agent.acceptPlan("manual"); // too late
        QCOMPARE(agent.planStatus(), QStringLiteral("rejected"));
        const QString file = agent.planPath();
        QVERIFY(QFile::exists(file));
        agent.deleteSession(agent.activeSessionId());
        QVERIFY(!QFile::exists(file));
    }

    void systemPromptCarriesTheWorkingPrinciples()
    {
        for (const char *mode : {"plan", "manual", "auto"}) {
            const QString prompt = systemPromptFor(mode, "/tmp/ws");
            QVERIFY(prompt.contains("Think before coding"));
            QVERIFY(prompt.contains("Simplicity first"));
            QVERIFY(prompt.contains("Surgical changes"));
            QVERIFY(prompt.contains("Goal-driven execution"));
            QVERIFY(prompt.contains("/tmp/ws"));
        }
    }

    void pastedKeyIsTrimmedAndPresetsAreFilledIn()
    {
        CodingAgent agent;
        const auto presets = agent.presets();
        QCOMPARE(presets.size(), 4);
        for (const auto &v : presets) {
            const auto p = v.toMap();
            QVERIFY(p.value("baseUrl").toString().startsWith("https://"));
            QVERIFY(!p.value("modelId").toString().isEmpty());
            QVERIFY(!p.value("authHeader").toString().isEmpty());
        }
        // A key pasted with a trailing newline would otherwise make an invalid HTTP header.
        auto fields = customProvider("https://example.test/v1");
        fields.insert("apiKey", "  sk-pasted\n");
        const auto id = agent.addProvider(fields);
        QVERIFY(!id.isEmpty());
        QVERIFY(agent.updateProvider(id, {{"apiKey", "\tsk-again \r\n"}}));
        QVERIFY(!readBytes(testHome().path() + "/config/notch/dev/secrets.json").contains("sk-pasted\n"));
        QVERIFY(readBytes(testHome().path() + "/config/notch/dev/secrets.json").contains("\"sk-again\""));
        QVERIFY(agent.removeProvider(id));
    }

    void backendProviderCrudAndKeyMasking()
    {
        CodingAgent agent;
        const int originalCount = agent.providers().size();
        const QString key = "test-secret-not-for-qml";
        auto fields = customProvider("https://example.test/v1");
        fields.insert("apiKey", key);
        const auto id = agent.addProvider(fields);
        QVERIFY(!id.isEmpty());
        QCOMPARE(agent.providers().size(), originalCount + 1);
        auto view = agent.providerById(id);
        QVERIFY(view.value("hasApiKey").toBool());
        QVERIFY(!view.contains("apiKey"));
        QVERIFY(!json(QJsonObject::fromVariantMap(view)).contains(key.toUtf8()));
        agent.setActiveProviderId(id);
        QCOMPARE(agent.activeProviderId(), id);
        QVERIFY(!json(QJsonObject::fromVariantMap(agent.activeProvider())).contains(key.toUtf8()));
        QVERIFY(!QJsonDocument(QJsonArray::fromVariantList(agent.providers())).toJson().contains(key.toUtf8()));
        agent.setActiveModelId("arbitrary-model-not-in-presets");
        QCOMPARE(agent.activeModelId(), QStringLiteral("arbitrary-model-not-in-presets"));
        QVERIFY(agent.updateProvider(id, {{"name", "Renamed"}, {"temperature", 0.8}, {"maxTokens", 8192}}));
        QCOMPARE(agent.providerById(id).value("name").toString(), QStringLiteral("Renamed"));
        const auto config = readBytes(testHome().path() + "/config/notch/dev/agent.json");
        QVERIFY(!config.isEmpty());
        QVERIFY(!config.contains(key.toUtf8()));
        {
            CodingAgent restored;
            QCOMPARE(restored.activeProviderId(), id);
            QCOMPARE(restored.activeModelId(), agent.activeModelId());
            QVERIFY(restored.providerById(id).value("hasApiKey").toBool());
            QVERIFY(restored.updateProvider(id, {{"apiKey", ""}}));
            QVERIFY(!restored.providerById(id).value("hasApiKey").toBool());
        }
        { CodingAgent fresh; QVERIFY(!fresh.providerById(id).value("hasApiKey").toBool()); } // one running agent keeps its own copy of the keys
        QVERIFY(agent.updateProvider(id, {{"apiKey", key}}));
        QVERIFY(agent.removeProvider(id));
        QVERIFY(agent.providerById(id).isEmpty());
        QCOMPARE(agent.providers().size(), originalCount);
        QVERIFY(agent.activeProviderId() != id);
        QVERIFY(!readBytes(testHome().path() + "/config/notch/dev/secrets.json").contains(key.toUtf8()));
        QVERIFY(!agent.updateProvider("missing", {{"name", "No"}}));
        QVERIFY(!agent.removeProvider("missing"));
    }

    void backendInvalidProvider_data()
    {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QVariant>("value");
        QTest::newRow("remote-http") << QString("baseUrl") << QVariant("http://example.test/v1");
        QTest::newRow("url-credentials") << QString("baseUrl") << QVariant("https://user:password@example.test/v1");
        QTest::newRow("url-query") << QString("baseUrl") << QVariant("https://example.test/v1?key=secret");
        QTest::newRow("empty-model") << QString("modelId") << QVariant("");
        QTest::newRow("bad-json") << QString("extraBodyJson") << QVariant("not-json");
        QTest::newRow("array-json") << QString("customHeadersJson") << QVariant("[]");
        QTest::newRow("header-injection") << QString("customHeadersJson") << QVariant("{\"X-Test\":\"ok\\r\\nInjected: yes\"}");
        QTest::newRow("low-token-limit") << QString("maxTokens") << QVariant(1);
    }

    void backendInvalidProvider()
    {
        QFETCH(QString, field);
        QFETCH(QVariant, value);
        CodingAgent agent;
        const auto before = agent.providers();
        auto fields = customProvider("https://example.test/v1");
        fields.insert(field, value);
        QVERIFY(agent.addProvider(fields).isEmpty());
        QCOMPARE(agent.providers(), before);
        QVERIFY(!agent.updateProvider(agent.activeProviderId(), {{field, value}}));
        QCOMPARE(agent.providers(), before);
    }

    void backendFragmentedStream()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        server.fragmentSize = 7;
        server.responses.append("data: " + json(deltaEvent({{"content", QString::fromUtf8("Hello 世界")}, {"reasoning_content", "A reason"}}))
                                + "\r\n\r\ndata: [DONE]\r\n\r\n");
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        QVERIFY(!id.isEmpty());
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        QSignalSpy chunks(&agent, &CodingAgent::streamChunkChanged);
        agent.sendPrompt("Say hello");
        QVERIFY(agent.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(server.requests.first().headers.startsWith("POST /v1/chat/completions HTTP/1.1"));
        QCOMPARE(server.requests.first().body.value("model").toString(), QStringLiteral("test-model"));
        QVERIFY(server.requests.first().body.value("stream").toBool());
        QVERIFY(agent.lastError().isEmpty());
        QCOMPARE(agent.messages().size(), 2);
        const auto answer = agent.messages().last().toMap();
        QCOMPARE(answer.value("role").toString(), QStringLiteral("assistant"));
        QCOMPARE(answer.value("content").toString(), QString::fromUtf8("Hello 世界"));
        QCOMPARE(answer.value("reasoning").toString(), QStringLiteral("A reason"));
        QVERIFY(chunks.count() >= 2); // redraws are throttled, so not one per piece
        QVERIFY(agent.streamingContent().isEmpty());
        QVERIFY(agent.streamingReasoning().isEmpty());
        agent.clearSession();
        QVERIFY(agent.messages().isEmpty());
    }

    void backendPlanEnforcement()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString file = workspace.filePath("existing.txt");
        QVERIFY(writeBytes(file, "original"));
        FakeServer server;
        QVERIFY(server.listen());
        server.responses.append(toolResponse({
            toolDelta(0, "edit", "propose_edit", QString::fromUtf8(json({{"path", "existing.txt"}, {"new_text", "modified"}}))),
            toolDelta(1, "shell", "run_command", "{\"command\":\"printf forbidden > command-marker.txt\"}")
        }));
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        QVERIFY(!id.isEmpty());
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.setMode("plan");
        agent.sendPrompt("Only plan; never edit or run commands");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QVERIFY(agent.lastError().isEmpty());
        QCOMPARE(toolNames(server.requests.first().body.value("tools").toArray(), false),
                 (QStringList{"list_files", "read_file", "search_files", "write_plan"}));
        QCOMPARE(readBytes(file), QByteArray("original"));
        QVERIFY(!QFile::exists(workspace.filePath("command-marker.txt")));
        QVERIFY(!agent.commandRunning());
        QVERIFY(!agent.hasPendingApprovals());
        const auto messages = agent.messages();
        QCOMPARE(messages.size(), 5);
        const auto assistant = messages[1].toMap();
        const auto calls = assistant.value("toolCalls").toList();
        QCOMPARE(calls.size(), 2);
        for (const auto &value : calls) {
            const auto call = value.toMap();
            QCOMPARE(call.value("status").toString(), QStringLiteral("error"));
            QVERIFY(call.value("output").toString().contains("Plan Mode"));
            agent.approveToolCall(assistant.value("id").toString(), call.value("id").toString());
        }
        agent.approveAllPending();
        QCOMPARE(readBytes(file), QByteArray("original"));
        QVERIFY(!QFile::exists(workspace.filePath("command-marker.txt")));
        for (int i = 2; i <= 3; ++i) {
            QCOMPARE(messages[i].toMap().value("role").toString(), QStringLiteral("tool"));
            QVERIFY(messages[i].toMap().value("content").toString().contains("Plan Mode"));
        }
        const auto followup = server.requests[1].body.value("messages").toArray();
        QCOMPARE(followup.size(), 5); // system, user, assistant, two error results
        QCOMPARE(followup.last().toObject().value("role").toString(), QStringLiteral("tool"));
    }

    void backendManualApproval()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString file = workspace.filePath("existing.txt");
        QVERIFY(writeBytes(file, "original"));
        FakeServer server;
        QVERIFY(server.listen());
        server.responses.append(toolResponse({
            toolDelta(0, "edit", "propose_edit", "{\"path\":\"existing.txt\",\"new_text\":\"approved\"}"),
            toolDelta(1, "shell", "run_command", "{\"command\":\"printf forbidden > command-marker.txt\"}")
        }));
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        QVERIFY(!id.isEmpty());
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("Propose an edit and command");
        QTRY_VERIFY_WITH_TIMEOUT(agent.hasPendingApprovals(), 5000);
        QVERIFY(!agent.busy());
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(server.requests.first().body.value("tools").toArray().size(), 6);
        QCOMPARE(readBytes(file), QByteArray("original"));
        QVERIFY(!QFile::exists(workspace.filePath("command-marker.txt")));
        const auto assistant = agent.messages().last().toMap();
        const QString messageId = assistant.value("id").toString();
        const auto calls = assistant.value("toolCalls").toList();
        QCOMPARE(calls.size(), 2);
        QCOMPARE(calls.first().toMap().value("status").toString(), QStringLiteral("awaiting_approval"));
        QVERIFY(!calls.first().toMap().value("diffText").toString().isEmpty());
        QVERIFY(agent.newSession().isEmpty());
        QVERIFY(!agent.updateProvider(id, {{"modelId", "changed"}}));
        agent.approveToolCall(messageId, "edit");
        QCOMPARE(readBytes(file), QByteArray("approved"));
        QVERIFY(agent.hasPendingApprovals());
        QCOMPARE(server.requests.size(), 1);
        agent.rejectToolCall(messageId, "shell");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QVERIFY(!agent.hasPendingApprovals());
        QVERIFY(agent.lastError().isEmpty());
        QVERIFY(!QFile::exists(workspace.filePath("command-marker.txt")));
        const auto finalCalls = agent.messages()[1].toMap().value("toolCalls").toList();
        QCOMPARE(finalCalls[0].toMap().value("status").toString(), QStringLiteral("applied"));
        QCOMPARE(finalCalls[1].toMap().value("status").toString(), QStringLiteral("rejected"));
    }

    void backendAutoMultipleEditsOneFollowup()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QVERIFY(writeBytes(workspace.filePath("first.txt"), "original first"));
        QVERIFY(writeBytes(workspace.filePath("second.txt"), "original second"));
        FakeServer server;
        QVERIFY(server.listen());
        server.responses.append(toolResponse({
            toolDelta(0, "edit-first", "propose_edit", "{\"path\":\"first.txt\",\"new_text\":\"updated first\"}"),
            toolDelta(1, "edit-second", "propose_edit", "{\"path\":\"second.txt\",\"new_text\":\"updated second\"}"),
            toolDelta(2, "edit-new", "propose_edit", "{\"path\":\"nested/new.txt\",\"new_text\":\"created\"}")
        }));
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        QVERIFY(!id.isEmpty());
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.setMode("auto");
        agent.sendPrompt("Apply all three edits");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        // Drain queued completion callbacks: eventual idleness alone can hide duplicate turns.
        QTest::qWait(150);
        QCOMPARE(server.requests.size(), 2);
        QVERIFY(!agent.busy());
        QVERIFY(!agent.hasPendingApprovals());
        QVERIFY(agent.lastError().isEmpty());
        QCOMPARE(readBytes(workspace.filePath("first.txt")), QByteArray("updated first"));
        QCOMPARE(readBytes(workspace.filePath("second.txt")), QByteArray("updated second"));
        QCOMPARE(readBytes(workspace.filePath("nested/new.txt")), QByteArray("created"));
        const auto messages = agent.messages();
        QCOMPARE(messages.size(), 6);
        const auto calls = messages[1].toMap().value("toolCalls").toList();
        QCOMPARE(calls.size(), 3);
        const auto followup = server.requests[1].body.value("messages").toArray();
        QCOMPARE(followup.size(), 6); // system, user, assistant, three edit results
        for (int i = 0; i < 3; ++i) {
            const auto call = calls[i].toMap();
            QCOMPARE(call.value("status").toString(), QStringLiteral("applied"));
            const auto result = messages[i + 2].toMap();
            QCOMPARE(result.value("role").toString(), QStringLiteral("tool"));
            QCOMPARE(result.value("toolCallId"), call.value("id"));
            QVERIFY(result.value("content").toString().startsWith("Applied "));
            const auto sentResult = followup[i + 3].toObject();
            QCOMPARE(sentResult.value("role").toString(), QStringLiteral("tool"));
            QCOMPARE(sentResult.value("tool_call_id").toString(), call.value("id").toString());
            QCOMPARE(sentResult.value("content").toString(), result.value("content").toString());
        }
        QCOMPARE(messages.last().toMap().value("content").toString(), QStringLiteral("Finished"));
    }

    void backendStaleEdit_data()
    {
        QTest::addColumn<QString>("externalChange");
        QTest::newRow("content-changed") << QString("replace");
        QTest::newRow("file-created") << QString("create");
        QTest::newRow("file-deleted") << QString("delete");
    }

    void backendStaleEdit()
    {
        QFETCH(QString, externalChange);
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString file = workspace.filePath("target.txt");
        if (externalChange != "create")
            QVERIFY(writeBytes(file, "original"));
        FakeServer server;
        QVERIFY(server.listen());
        server.responses.append(toolResponse({
            toolDelta(0, "stale-edit", "propose_edit", "{\"path\":\"target.txt\",\"new_text\":\"agent replacement\"}")
        }));
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        QVERIFY(!id.isEmpty());
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("Propose a replacement");
        QTRY_VERIFY_WITH_TIMEOUT(agent.hasPendingApprovals(), 5000);
        QCOMPARE(server.requests.size(), 1);
        const QString messageId = agent.messages().last().toMap().value("id").toString();
        if (externalChange == "delete")
            QVERIFY(QFile::remove(file));
        else
            QVERIFY(writeBytes(file, "external")); // Same length as original; must compare content.
        agent.approveToolCall(messageId, "stale-edit");
        QVERIFY(!agent.hasPendingApprovals());
        const auto call = agent.messages()[1].toMap().value("toolCalls").toList().first().toMap();
        QCOMPARE(call.value("status").toString(), QStringLiteral("error"));
        QVERIFY(call.value("output").toString().contains("file changed since proposal"));
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QVERIFY(agent.lastError().isEmpty());
        if (externalChange == "delete")
            QVERIFY(!QFile::exists(file));
        else
            QCOMPARE(readBytes(file), QByteArray("external"));
        const auto result = agent.messages()[2].toMap();
        QCOMPARE(result.value("role").toString(), QStringLiteral("tool"));
        QCOMPARE(result.value("toolCallId").toString(), QStringLiteral("stale-edit"));
        QCOMPARE(result.value("content"), call.value("output"));
    }

    void backendStopPending_data()
    {
        QTest::addColumn<bool>("queueFollowup");
        QTest::newRow("pending-approvals") << false;
        QTest::newRow("already-queued-followup") << true;
    }

    void backendStopPending()
    {
        QFETCH(bool, queueFollowup);
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString file = workspace.filePath("existing.txt");
        QVERIFY(writeBytes(file, "original"));
        FakeServer server;
        QVERIFY(server.listen());
        server.responses.append(toolResponse({
            toolDelta(0, "edit", "propose_edit", "{\"path\":\"existing.txt\",\"new_text\":\"forbidden\"}"),
            toolDelta(1, "shell", "run_command", "{\"command\":\"printf forbidden > command-marker.txt\"}")
        }));
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        QVERIFY(!id.isEmpty());
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("Propose operations to stop");
        QTRY_VERIFY_WITH_TIMEOUT(agent.hasPendingApprovals(), 5000);
        QVERIFY(!agent.busy());
        const QString messageId = agent.messages().last().toMap().value("id").toString();
        if (queueFollowup) {
            agent.rejectToolCall(messageId, "edit");
            agent.rejectToolCall(messageId, "shell");
            QVERIFY(!agent.hasPendingApprovals());
            // Stop synchronously, before the queued zero-delay continuation can run.
        }
        agent.stop();
        QVERIFY(!agent.hasPendingApprovals());
        QVERIFY(!agent.busy());
        QVERIFY(!agent.streaming());
        QVERIFY(!agent.commandRunning());
        const auto stoppedMessages = agent.messages();
        QCOMPARE(stoppedMessages.size(), 5); // the user message, the agent's tool calls, two results, the "Interrupted" note
        QCOMPARE(stoppedMessages.last().toMap().value("role").toString(), QStringLiteral("note"));
        const auto calls = stoppedMessages[1].toMap().value("toolCalls").toList();
        QCOMPARE(calls.size(), 2);
        for (int i = 0; i < 2; ++i) {
            const auto call = calls[i].toMap();
            QCOMPARE(call.value("status").toString(), QStringLiteral("rejected"));
            const auto result = stoppedMessages[i + 2].toMap();
            QCOMPARE(result.value("role").toString(), QStringLiteral("tool"));
            QCOMPARE(result.value("toolCallId"), call.value("id"));
            if (!queueFollowup)
                QVERIFY(result.value("content").toString().contains("Stopped by user"));
        }
        agent.approveToolCall(messageId, "edit");
        agent.approveToolCall(messageId, "shell");
        agent.approveAllPending();
        agent.stop();
        QTest::qWait(200);
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(agent.messages(), stoppedMessages);
        QCOMPARE(readBytes(file), QByteArray("original"));
        QVERIFY(!QFile::exists(workspace.filePath("command-marker.txt")));
        QVERIFY(!agent.busy());
        QVERIFY(agent.lastError().isEmpty());
    }

    void backendLiteralSearchAndReadEnforcement()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString workspace = directory.filePath("workspace");
        QVERIFY(QDir().mkpath(workspace));
        QVERIFY(writeBytes(workspace + "/document.txt", "aXb\nA.B literal\nlast line\n"));
        QVERIFY(writeBytes(workspace + "/.env", "a.b private-secret"));
        QVERIFY(writeBytes(directory.filePath("outside.txt"), "a.b outside-secret"));
        QVERIFY(writeBytes(workspace + "/binary.dat", QByteArray("a.b") + '\0' + "binary-secret"));
        FakeServer server;
        QVERIFY(server.listen());
        server.responses.append(toolResponse({
            toolDelta(0, "search", "search_files", "{\"pattern\":\"a.b\"}"),
            toolDelta(1, "read", "read_file", "{\"path\":\"document.txt\",\"start_line\":2,\"end_line\":2}"),
            toolDelta(2, "private", "read_file", "{\"path\":\".env\"}"),
            toolDelta(3, "outside", "read_file", "{\"path\":\"../outside.txt\"}"),
            toolDelta(4, "binary", "read_file", "{\"path\":\"binary.dat\"}")
        }));
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        QVERIFY(!id.isEmpty());
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace);
        agent.setMode("plan");
        agent.sendPrompt("Search literally and read only allowed workspace text");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QVERIFY(!agent.hasPendingApprovals());
        QVERIFY(agent.lastError().isEmpty());
        const auto messages = agent.messages();
        QCOMPARE(messages.size(), 8);
        const auto calls = messages[1].toMap().value("toolCalls").toList();
        QCOMPARE(calls.size(), 5);
        QCOMPARE(calls[0].toMap().value("status").toString(), QStringLiteral("completed"));
        const auto search = calls[0].toMap().value("output").toString();
        QVERIFY(search.contains("document.txt:2: A.B literal"));
        QVERIFY(!search.contains("aXb"));
        QVERIFY(!search.contains("secret"));
        QCOMPARE(calls[1].toMap().value("status").toString(), QStringLiteral("completed"));
        const auto read = calls[1].toMap().value("output").toString();
        QVERIFY(read.startsWith("2\tA.B literal"));
        QVERIFY(!read.contains("aXb"));
        QVERIFY(!read.contains("last line"));
        for (int i = 2; i < 5; ++i) {
            QCOMPARE(calls[i].toMap().value("status").toString(), QStringLiteral("error"));
            QVERIFY(calls[i].toMap().value("output").toString().startsWith("Error:"));
        }
        const auto followup = QJsonDocument(server.requests[1].body).toJson();
        QVERIFY(!followup.contains("private-secret"));
        QVERIFY(!followup.contains("outside-secret"));
        QVERIFY(!followup.contains("binary-secret"));
    }

    void backendTruncatedToolsDiscarded_data()
    {
        QTest::addColumn<QString>("protocol");
        QTest::newRow("openai-length") << QString("openai");
        QTest::newRow("anthropic-max-tokens") << QString("anthropic");
    }

    void backendTruncatedToolsDiscarded()
    {
        QFETCH(QString, protocol);
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString file = workspace.filePath("existing.txt");
        QVERIFY(writeBytes(file, "original"));
        FakeServer server;
        QVERIFY(server.listen());
        QByteArray response;
        const auto appendEvent = [&response](const QJsonObject &event) {
            response += "data: " + json(event) + "\n\n";
        };
        const QString editArguments = "{\"path\":\"existing.txt\",\"new_text\":\"must not apply\"}";
        if (protocol == "openai") {
            appendEvent(deltaEvent({{"content", "Discarded partial answer"}, {"tool_calls", QJsonArray{
                toolDelta(0, "complete-edit", "propose_edit", editArguments),
                toolDelta(1, "partial-tool", "run_command", "{\"command\":\"")}}}));
            appendEvent({{"choices", QJsonArray{QJsonObject{{"delta", QJsonObject{}}, {"finish_reason", "length"}}}}});
            response += "data: [DONE]\n\n";
        } else {
            appendEvent({{"type", "content_block_delta"},
                         {"delta", QJsonObject{{"type", "text_delta"}, {"text", "Discarded partial answer"}}}});
            appendEvent({{"type", "content_block_start"}, {"index", 1},
                         {"content_block", QJsonObject{{"type", "tool_use"}, {"id", "complete-edit"}, {"name", "propose_edit"}}}});
            appendEvent({{"type", "content_block_delta"}, {"index", 1},
                         {"delta", QJsonObject{{"type", "input_json_delta"}, {"partial_json", editArguments}}}});
            appendEvent({{"type", "content_block_start"}, {"index", 2},
                         {"content_block", QJsonObject{{"type", "tool_use"}, {"id", "partial-tool"}, {"name", "run_command"}}}});
            appendEvent({{"type", "content_block_delta"}, {"index", 2},
                         {"delta", QJsonObject{{"type", "input_json_delta"}, {"partial_json", "{\"command\":\""}}}});
            appendEvent({{"type", "message_delta"}, {"delta", QJsonObject{{"stop_reason", "max_tokens"}}}});
            appendEvent({{"type", "message_stop"}});
        }
        server.responses.append(response);
        if (protocol == "anthropic") {
            for (int i = 0; i < 2; ++i) // the second try, and the next prompt
                server.responses.append("data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"Finished\"}}\n\n"
                                        "data: {\"type\":\"message_stop\"}\n\n");
        }
        CodingAgent agent;
        auto fields = customProvider(server.baseUrl());
        fields.insert("protocol", protocol);
        const auto id = agent.addProvider(fields);
        QVERIFY(!id.isEmpty());
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.setMode("auto");
        agent.sendPrompt("First prompt");
        const QString session = agent.activeSessionId();
        // the cut tool calls are never run; the same step is asked again with a bigger token limit, and that one goes through
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QVERIFY(agent.lastError().isEmpty());
        QVERIFY(!agent.hasPendingApprovals());
        QVERIFY(!agent.commandRunning());
        QVERIFY(agent.streamingContent().isEmpty());
        QCOMPARE(readBytes(file), QByteArray("original"));
        const int first = server.requests[0].body.value("max_tokens").toInt(), second = server.requests[1].body.value("max_tokens").toInt();
        QVERIFY(first > 0);
        QCOMPARE(second, first * 2);
        QCOMPARE(agent.messages().size(), 2); // the cut attempt left nothing in the chat
        QCOMPARE(agent.messages().first().toMap().value("role").toString(), QStringLiteral("user"));
        QCOMPARE(agent.messages().last().toMap().value("content").toString(), QStringLiteral("Finished"));
        QCOMPARE(agent.activeSessionId(), session);

        // the bigger limit is kept for the next prompt, and the history never carries the discarded tool calls
        server.requests.clear();
        agent.sendPrompt("Next prompt");
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QCOMPARE(server.requests[0].body.value("max_tokens").toInt(), second);
        for (const auto &value : server.requests[0].body.value("messages").toArray()) {
            const auto message = value.toObject();
            QVERIFY(!message.contains("tool_calls"));
            QVERIFY(!message.contains("tool_call_id"));
        }
        QCOMPARE(readBytes(file), QByteArray("original"));
    }

    void backendQueuedContinuationLocksContext()
    {
        QTemporaryDir workspace;
        QTemporaryDir otherWorkspace;
        QVERIFY(workspace.isValid());
        QVERIFY(otherWorkspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        server.responses.append(toolResponse({
            toolDelta(0, "edit", "propose_edit", "{\"path\":\"new.txt\",\"new_text\":\"proposed\"}")
        }));
        CodingAgent agent;
        const auto provider = agent.addProvider(customProvider(server.baseUrl()));
        const auto otherProvider = agent.addProvider(customProvider(server.baseUrl()));
        QVERIFY(!provider.isEmpty());
        QVERIFY(!otherProvider.isEmpty());
        agent.setActiveProviderId(provider);
        agent.setWorkspace(workspace.path());
        const auto otherSession = agent.newSession("Other task");
        const auto session = agent.newSession("Active task");
        QVERIFY(!otherSession.isEmpty());
        QVERIFY(!session.isEmpty());
        agent.sendPrompt("Propose an edit");
        QTRY_VERIFY_WITH_TIMEOUT(agent.hasPendingApprovals(), 5000);
        const QString message = agent.messages().last().toMap().value("id").toString();
        agent.rejectToolCall(message, "edit");
        // No event processing here: only a queued continuation keeps the backend busy.
        QVERIFY(agent.busy());
        QVERIFY(!agent.streaming());
        QVERIFY(!agent.commandRunning());
        QVERIFY(!agent.hasPendingApprovals());
        const auto sessions = agent.sessions();
        const auto providers = agent.providers();
        const auto messages = agent.messages();
        const auto model = agent.activeModelId();
        const auto root = agent.workspace();
        agent.selectSession(otherSession);
        QVERIFY(agent.newSession("Blocked task").isEmpty());
        agent.deleteSession(session);
        agent.deleteSession(otherSession);
        agent.clearSession();
        agent.setActiveProviderId(otherProvider);
        QVERIFY(!agent.updateProvider(provider, {{"modelId", "blocked-model"}}));
        QVERIFY(!agent.removeProvider(otherProvider));
        QVERIFY(agent.addProvider(customProvider(server.baseUrl())).isEmpty());
        agent.setWorkspace(otherWorkspace.path());
        agent.sendPrompt("Blocked prompt");
        QCOMPARE(agent.activeSessionId(), session);
        QCOMPARE(agent.sessions(), sessions);
        QCOMPARE(agent.activeProviderId(), provider);
        QCOMPARE(agent.activeModelId(), model);
        QCOMPARE(agent.providers(), providers);
        QCOMPARE(agent.workspace(), root);
        QCOMPARE(agent.mode(), QStringLiteral("manual"));
        QCOMPARE(agent.messages(), messages);
        QCOMPARE(server.requests.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!agent.busy(), 5000);
        QTest::qWait(100);
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(agent.activeSessionId(), session);
        QVERIFY(agent.lastError().isEmpty());
        QCOMPARE(server.requests[1].body.value("model").toString(), model);
        QCOMPARE(server.requests[1].body.value("tools").toArray().size(), 6);
        QVERIFY(!QFile::exists(workspace.filePath("new.txt")));
        QVERIFY(!QFile::exists(otherWorkspace.filePath("new.txt")));
    }

    void backendStopCommandKillsDescendants()
    {
#ifndef Q_OS_UNIX
        QSKIP("Process-group cancellation requires a UNIX shell");
#else
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeServer server;
        QVERIFY(server.listen());
        // The child itself announces readiness, proving it exists before stop().
        const QString command = "(printf 'descendant-started\\n'; sleep 0.4; touch marker)& wait";
        server.responses.append(toolResponse({
            toolDelta(0, "shell", "run_command", QString::fromUtf8(json({{"command", command}})))
        }));
        CodingAgent agent;
        const auto id = agent.addProvider(customProvider(server.baseUrl()));
        QVERIFY(!id.isEmpty());
        agent.setActiveProviderId(id);
        agent.setWorkspace(workspace.path());
        agent.sendPrompt("Propose a background command");
        QTRY_VERIFY_WITH_TIMEOUT(agent.hasPendingApprovals(), 5000);
        const QString message = agent.messages().last().toMap().value("id").toString();
        agent.approveToolCall(message, "shell");
        QTRY_VERIFY_WITH_TIMEOUT(agent.messages()[1].toMap().value("toolCalls").toList().first().toMap()
                                     .value("output").toString().contains("descendant-started"), 5000);
        QVERIFY(agent.commandRunning());
        QVERIFY(!QFile::exists(workspace.filePath("marker")));
        agent.stop();
        QVERIFY(!agent.commandRunning());
        QVERIFY(!agent.busy());
        QVERIFY(!agent.hasPendingApprovals());
        const auto stoppedMessages = agent.messages();
        QCOMPARE(stoppedMessages.size(), 4); // + the "Interrupted" note
        QCOMPARE(stoppedMessages[1].toMap().value("toolCalls").toList().first().toMap()
                     .value("status").toString(), QStringLiteral("rejected"));
        QTest::qWait(650);
        QVERIFY(!QFile::exists(workspace.filePath("marker")));
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(agent.messages(), stoppedMessages);
        QVERIFY(agent.lastError().isEmpty());
#endif
    }

    void backendRestartDropsElevatedConsent()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QString sessionId;
        {
            CodingAgent agent;
            agent.setWorkspace(workspace.path());
            agent.setMode("auto");
            sessionId = agent.newSession("Auto task");
            QVERIFY(!sessionId.isEmpty());
            QCOMPARE(agent.mode(), QStringLiteral("auto"));
        }
        CodingAgent restored;
        QCOMPARE(restored.activeSessionId(), sessionId);
        QCOMPARE(restored.mode(), QStringLiteral("manual"));
        restored.selectSession(sessionId);
        QCOMPARE(restored.mode(), QStringLiteral("manual"));
        QVERIFY(!restored.hasPendingApprovals());
        QVERIFY(!restored.busy());
    }
};

QTEST_GUILESS_MAIN(tst_CodingAgent)
#include "tst_codingagent.moc"
