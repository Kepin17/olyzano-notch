#include "app/CloudController.h"
#include "app/NotchController.h"
#include "app/ShareController.h"
#include "services/Invite.h"
#include "services/ShareCrypto.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// The cloud shelf from the app's side, against the real relay (server/relay.py) with a fake S3 behind it: a host and a member (the same app
// acting as each, one after the other), the shelf key travelling in the invitation, and what the host decides: pause (with a reason),
// let back in, remove.
class TestCloud : public QObject
{
    Q_OBJECT
    QProcess m_relay;
    QTemporaryDir m_home;

    QJsonObject ask(const QString &line)
    {
        m_relay.write((line + QLatin1Char('\n')).toUtf8());
        if (!m_relay.waitForReadyRead(15000))
            return {};
        return QJsonDocument::fromJson(m_relay.readLine()).object();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        qputenv("XDG_CONFIG_HOME", m_home.filePath(QStringLiteral("config")).toUtf8()); // the secrets, the servers: not the real ones
        qputenv("XDG_DATA_HOME", m_home.filePath(QStringLiteral("data")).toUtf8());
    }

    void sharedShelfRoundTripAndTheHostsControl()
    {
        const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty())
            QSKIP("python3 is not available");
        m_relay.start(python, {QStringLiteral("-I"), QStringLiteral(NOTCH_SOURCE_DIR "/server/tests/serve_for_client.py")});
        QVERIFY2(m_relay.waitForStarted(5000), "python3 did not start");
        QVERIFY2(m_relay.waitForReadyRead(20000), m_relay.readAllStandardError().constData());
        const QJsonObject server = QJsonDocument::fromJson(m_relay.readLine()).object();
        const QString url = server.value(QStringLiteral("url")).toString(), hostKey = server.value(QStringLiteral("hostKey")).toString();
        QVERIFY(!url.isEmpty());

        QTemporaryDir data;
        NotchController notch(data.path());
        QVERIFY(notch.start());
        ShareController share(&notch);
        CloudController cloud(&notch, &share);
        QVERIFY(!cloud.enabled());

        // ---- the host: switch it on, send a file ----
        QSignalSpy tested(&share, &ShareController::serverTested);
        QCOMPARE(share.addServer(QStringLiteral("home"), url, hostKey), QString());
        QTRY_VERIFY_WITH_TIMEOUT(tested.count() >= 1, 10000);
        const QString hostId = share.servers().first().toMap().value(QStringLiteral("id")).toString();
        cloud.setServerId(hostId);
        cloud.setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(cloud.state(), QStringLiteral("ready"), 15000);
        QVERIFY(cloud.isHost());
        QVERIFY(!share.shelfKey(hostId).isEmpty()); // the host made the shelf's key

        QTemporaryDir src;
        QFile f(src.filePath(QStringLiteral("plan.txt")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("secret contents of the plan");
        f.close();
        cloud.uploadUrls({QUrl::fromLocalFile(f.fileName())});
        QTRY_COMPARE_WITH_TIMEOUT(cloud.count(), 1, 20000);
        QCOMPARE(cloud.entries()->index(0, 0).data(CloudModel::NameRole).toString(), QStringLiteral("plan.txt"));
        const QJsonObject stored = ask(QStringLiteral("stored"));
        QCOMPARE(stored.value(QStringLiteral("count")).toInt(), 1);
        QVERIFY(!stored.value(QStringLiteral("plaintext")).toBool()); // the store holds ciphertext
        QCOMPARE(stored.value(QStringLiteral("bad_signatures")).toInt(), 0);

        // ---- it comes back, readable ----
        cloud.download(cloud.entries()->index(0, 0).data(CloudModel::IdRole).toString());
        QTRY_COMPARE_WITH_TIMEOUT(notch.count(), 1, 20000);
        QTRY_COMPARE_WITH_TIMEOUT(notch.copying(), 0, 20000);
        const QString local = notch.storedPath(notch.visibleIds().first());
        QFile got(local);
        QVERIFY2(got.open(QIODevice::ReadOnly), qPrintable(local));
        QCOMPARE(got.readAll(), QByteArray("secret contents of the plan"));

        // ---- a member joins with an invitation that carries the key ----
        const QString invite = ask(QStringLiteral("invite alice")).value(QStringLiteral("link")).toString();
        QVERIFY(invite.contains(QLatin1String("/join#")));
        const Invite::Parsed plain = Invite::parse(invite);
        QVERIFY(plain.shelfKey.isEmpty()); // the relay's own link has no key
        const QString withKey = Invite::withShelfKey(invite, share.shelfKey(hostId));
        QCOMPARE(Invite::parse(withKey).shelfKey, share.shelfKey(hostId));
        QCOMPARE(Invite::parse(withKey).token, plain.token);
        QSignalSpy joined(&share, &ShareController::joined);
        share.joinServer(withKey);
        QTRY_VERIFY_WITH_TIMEOUT(joined.count() == 1, 10000);
        QVERIFY2(joined.first().first().toBool(), qPrintable(joined.first().at(1).toString()));
        QString memberId;
        for (const QVariant &s : share.servers())
            if (s.toMap().value(QStringLiteral("id")).toString() != hostId)
                memberId = s.toMap().value(QStringLiteral("id")).toString();
        QVERIFY(!memberId.isEmpty());
        QCOMPARE(share.shelfKey(memberId), share.shelfKey(hostId));

        cloud.setServerId(memberId);
        QTRY_COMPARE_WITH_TIMEOUT(cloud.state(), QStringLiteral("ready"), 15000);
        QVERIFY(!cloud.isHost());
        QTRY_COMPARE_WITH_TIMEOUT(cloud.count(), 1, 15000);
        QCOMPARE(cloud.entries()->index(0, 0).data(CloudModel::NameRole).toString(), QStringLiteral("plan.txt")); // read with the shared key
        QCOMPARE(cloud.entries()->index(0, 0).data(CloudModel::MineRole).toBool(), false);
        QVERIFY(cloud.quota() > 0);

        // ---- Share via QR from the cloud shelf: fetch it, then share it with a key of its own ----
        QSignalSpy readyToShare(&cloud, &CloudController::readyToShare);
        const int before = notch.count();
        cloud.download(cloud.entries()->index(0, 0).data(CloudModel::IdRole).toString(), true);
        QTRY_COMPARE_WITH_TIMEOUT(readyToShare.count(), 1, 20000);
        const QString localId = readyToShare.first().first().toString();
        QVERIFY(!localId.isEmpty());
        QCOMPARE(notch.count(), before + 1);
        const QString sharedPath = notch.storedPath(localId); // available at once, so a share can start
        QVERIFY2(!sharedPath.isEmpty(), "the fetched file must be Available");
        QFile shared(sharedPath);
        QVERIFY(shared.open(QIODevice::ReadOnly));
        QCOMPARE(shared.readAll(), QByteArray("secret contents of the plan"));
        const QString sessionId = share.start({localId}, QStringLiteral("relay"), 3600, QStringLiteral("none"), QString(), false, QString(), memberId);
        QVERIFY(!sessionId.isEmpty());
        QString link;
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (int r = 0; r < share.rowCount(); ++r)
                    if (share.index(r).data(ShareController::IdRole).toString() == sessionId
                        && share.index(r).data(ShareController::StatusRole).toString() == QLatin1String("ready")) {
                        link = share.index(r).data(ShareController::UrlRole).toString();
                        return true;
                    }
                return false;
            }(),
            30000);
        QVERIFY2(link.contains(QLatin1String("/s/")) && link.contains(u'#'), qPrintable(link));
        QVERIFY(!link.contains(ShareCrypto::keyToFragment(share.shelfKey(memberId)))); // the shelf's key never goes into a link

        // ---- the host pauses the member, with a reason ----
        ask(QStringLiteral("disable alice over the agreed space"));
        share.testServer(memberId);
        QTRY_COMPARE_WITH_TIMEOUT(cloud.state(), QStringLiteral("disabled"), 10000);
        QVERIFY2(cloud.stateMessage().contains(QLatin1String("over the agreed space")), qPrintable(cloud.stateMessage()));
        QVERIFY(!cloud.usable());
        QSignalSpy refused(&cloud, &CloudController::failed);
        cloud.uploadUrls({QUrl::fromLocalFile(f.fileName())}); // nothing is sent while paused
        QCOMPARE(refused.count(), 1);
        QVERIFY(refused.first().first().toString().contains(QLatin1String("over the agreed space")));

        // ---- let back in ----
        ask(QStringLiteral("enable alice"));
        share.testServer(memberId);
        QTRY_COMPARE_WITH_TIMEOUT(cloud.state(), QStringLiteral("ready"), 10000);
        QVERIFY(cloud.usable());

        // ---- removed, with a reason ----
        ask(QStringLiteral("remove alice left the team"));
        share.testServer(memberId);
        QTRY_COMPARE_WITH_TIMEOUT(cloud.state(), QStringLiteral("removed"), 10000);
        QVERIFY2(cloud.stateMessage().contains(QLatin1String("left the team")), qPrintable(cloud.stateMessage()));

        ask(QStringLiteral("quit"));
        m_relay.waitForFinished(5000);
    }

    void blobsOpenOnlyWithTheirKey()
    {
        const QByteArray key = ShareCrypto::newKey(), other = ShareCrypto::newKey();
        const QByteArray sealed = ShareCrypto::sealBlob(key, "name=plan.txt");
        QVERIFY(!sealed.isNull());
        QVERIFY(!sealed.contains("plan.txt"));
        QCOMPARE(ShareCrypto::openBlob(key, sealed), QByteArray("name=plan.txt"));
        QVERIFY(ShareCrypto::openBlob(other, sealed).isNull());
        QByteArray changed = sealed;
        changed[changed.size() - 1] = char(changed.at(changed.size() - 1) ^ 1);
        QVERIFY(ShareCrypto::openBlob(key, changed).isNull());
        QVERIFY(!ShareCrypto::openBlob(key, QByteArray()).isNull() == false);
        QCOMPARE(ShareCrypto::openBlob(key, ShareCrypto::sealBlob(key, QByteArray())), QByteArray(""));
        QCOMPARE(ShareCrypto::keyFromFragment(ShareCrypto::keyToFragment(key)), key);
        QVERIFY(ShareCrypto::keyFromFragment(QStringLiteral("short")).isEmpty());
    }

    void invitationsMayCarryTheKey()
    {
        const QString link = QStringLiteral("https://share.example.com/join#AAAAAAAAAAAAAAAAAAAAAA");
        const QByteArray key = ShareCrypto::newKey();
        const Invite::Parsed p = Invite::parse(Invite::withShelfKey(link, key));
        QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
        QCOMPARE(p.token, QStringLiteral("AAAAAAAAAAAAAAAAAAAAAA"));
        QCOMPARE(p.shelfKey, key);
        QVERIFY(!Invite::parse(link + QStringLiteral("~notakey")).error.isEmpty());
        QVERIFY(Invite::parse(link).shelfKey.isEmpty());
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TestCloud test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_cloud.moc"
