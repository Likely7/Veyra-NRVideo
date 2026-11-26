#include "veyra/base/PortableLayout.h"
#include "veyra/base/PortableUpdateManifest.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <openssl/evp.h>
#include <filesystem>

using namespace veyra::portable;

namespace {
class Key {
public:
    EVP_PKEY* key = nullptr;
    QByteArray pub;
    bool ok = false;
    Key() {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
        ok = ctx && EVP_PKEY_keygen_init(ctx) == 1 && EVP_PKEY_keygen(ctx, &key) == 1;
        EVP_PKEY_CTX_free(ctx);
        pub.resize(32);
        size_t len = pub.size();
        ok = ok && EVP_PKEY_get_raw_public_key(key, reinterpret_cast<unsigned char*>(pub.data()), &len) == 1 && len == 32;
    }
    ~Key() { EVP_PKEY_free(key); }
    QByteArray sign(const QByteArray& message) const {
        EVP_MD_CTX* md = EVP_MD_CTX_new();
        QByteArray signature(64, 0);
        size_t len = static_cast<size_t>(signature.size());
        const bool signedOk = md && EVP_DigestSignInit(md, nullptr, nullptr, nullptr, key) == 1
            && EVP_DigestSign(md, reinterpret_cast<unsigned char*>(signature.data()), &len,
                              reinterpret_cast<const unsigned char*>(message.constData()),
                              static_cast<size_t>(message.size())) == 1
            && len == 64;
        EVP_MD_CTX_free(md);
        return signedOk ? signature : QByteArray{};
    }
};

QByteArray payloadOf(const QString& version, const QJsonArray& packages, const QString& notes = QStringLiteral("hello"),
                     const QString& arch = QStringLiteral("windows-x64")) {
    const QJsonObject object{{"schema", 1}, {"product", "Veyra-portable"}, {"arch", arch},
                             {"channel", "stable"}, {"version", version}, {"notes", notes}, {"packages", packages}};
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray envelope(const Key& key, const QByteArray& payload, QByteArray signature = {}) {
    if (signature.isEmpty()) signature = key.sign(payload);
    const QJsonObject object{{"payload", QString::fromLatin1(payload.toBase64())},
                             {"signature", QString::fromLatin1(signature.toBase64())}};
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject packageRow(const QString& version, const QString& flavor, const QString& asset, qint64 size = 100,
                       qint64 expanded = 200) {
    return {{"flavor", flavor}, {"asset", asset}, {"sha256", QString(64, QLatin1Char('a'))},
            {"size", size}, {"expandedSize", expanded}};
}
}

class PortableUpdateRulesTests : public QObject {
    Q_OBJECT
private slots:
    void versionsAndRollback() {
        const auto newer = compareRelease("2.0.9", "v2.0.10");
        const auto older = compareRelease("2.0.10", "v2.0.9");
        const auto same = compareRelease("2.0.7", "v2.0.7");
        const auto localLabel = compareRelease("2.0.7-test-r2", "v2.0.7");
        const auto localQuad = compareRelease("2.0.7.0", "v2.0.8");
        QVERIFY(newer && *newer == 1);
        QVERIFY(older && *older == -1);
        QVERIFY(same && *same == 0);
        QVERIFY(localLabel && *localLabel == 0);
        QVERIFY(localQuad && *localQuad == 1);
        QVERIFY(!compareRelease("2.0.7", "v2.0.01"));
        QVERIFY(!compareRelease("2.0.7", "v2.0.65536"));
        QVERIFY(!compareRelease("2.0.7", "v2.0.8-beta"));
        QVERIFY(!compareRelease("2.0.7", "v2.0"));
        QVERIFY(!compareRelease("dev", "v2.0.8"));
        QVERIFY(!compareRelease("2.0.7", ""));
        QVERIFY(startupShouldRollback(true, 0, 1));
        QVERIFY(!startupShouldRollback(false, 0, 1));
        QVERIFY(startupShouldRollback(false, 1, 14999));
        QVERIFY(!startupShouldRollback(false, 1, 15000));
        QVERIFY(!startupShouldRollback(false, 42, 1));
        QVERIFY(!startupShouldRollback(false, applyExitCode, 1));
        QVERIFY(!startupShouldRollback(false, overlayRestartExitCode, 1));
        QVERIFY(!startupShouldRollback(false, applyRestartExitCode, 1));
    }
    void archiveAndStaging() {
        QVERIFY(safeArchiveEntry("Veyra-2.0.1-NVIDIA-win64-portable/versions/2.0.1/veyra_qml_ui.exe"));
        QVERIFY(safeArchiveEntry("Veyra-2.0.1-NVIDIA-win64-portable/"));
        QVERIFY(!safeArchiveEntry(""));
        QVERIFY(!safeArchiveEntry("../evil.dll"));
        QVERIFY(!safeArchiveEntry("foo/../../outside.dll"));
        QVERIFY(!safeArchiveEntry("/absolute.dll"));
        QVERIFY(!safeArchiveEntry("C:/Windows/evil.dll"));
        QVERIFY(!safeArchiveEntry("foo:stream"));
        QVERIFY(!safeArchiveEntry("foo/./bar"));
        QVERIFY(!safeArchiveEntry(std::string(241, 'a')));
        const std::filesystem::path base("C:/Users/a/AppData/Local/Veyra/update-staging");
        QVERIFY(stagingDirectoryId(base, base / "0123456789abcdef"));
        QVERIFY(!stagingDirectoryId(base, base / "0123456789ABCDEF"));
        QVERIFY(!stagingDirectoryId(base, base / "0123456789abcdef" / "extra"));
        QVERIFY(!stagingDirectoryId(base, base / ".." / "0123456789abcdef"));
        QVERIFY(!stagingDirectoryId(base, std::filesystem::path("D:/elsewhere/0123456789abcdef")));
    }
    void hostsDigestsAndNotes() {
        const QUrl asset(QStringLiteral("https://objects.githubusercontent.com/release/file"));
        QVERIFY(allowedRedirect(asset));
        QVERIFY(allowedRedirect(QUrl(QStringLiteral("https://release-assets.githubusercontent.com/a"))));
        QVERIFY(!allowedRedirect(QUrl(QStringLiteral("https://evil.example/a"))));
        QVERIFY(!allowedRedirect(QUrl(QStringLiteral("https://objects.githubusercontent.com.evil/a"))));
        QVERIFY(!allowedRedirect(QUrl(QStringLiteral("http://objects.githubusercontent.com/a"))));
        QVERIFY(!allowedRedirect(QUrl(QStringLiteral("https://user@objects.githubusercontent.com/a"))));
        QVERIFY(!allowedRedirect(QUrl(QStringLiteral("https://objects.githubusercontent.com/a#frag"))));
        QVERIFY(!allowedRedirect(QUrl(QStringLiteral("https://objects.githubusercontent.com:443/a"))));
        QVERIFY(!allowedRedirect(QUrl(QStringLiteral("https://api.github.com/repos/Likely7/Veyra-NRVideo/releases/latest"))));
        QVERIFY(allowedApiRedirect(QUrl(QStringLiteral("https://api.github.com/repos/Likely7/Veyra-NRVideo/releases/latest"))));
        QVERIFY(allowedApiRedirect(QUrl(QStringLiteral("https://gh-proxy.com/https://api.github.com/repos/Likely7/Veyra-NRVideo/releases/latest"))));
        QVERIFY(allowedApiRedirect(QUrl(QStringLiteral("https://ghfast.top/https://api.github.com/x"))));
        QVERIFY(!allowedApiRedirect(asset));
        QVERIFY(!allowedApiRedirect(QUrl(QStringLiteral("https://evil.example/latest"))));
        QVERIFY(!allowedApiRedirect(QUrl(QStringLiteral("http://api.github.com/repos/x"))));
        const QByteArray hash(32, 'a');
        QVERIFY(publishedDigestAgrees(false, false, {}, hash));
        QVERIFY(!publishedDigestAgrees(true, false, {}, hash));
        QVERIFY(publishedDigestAgrees(true, true, {}, hash));
        QVERIFY(publishedDigestAgrees(true, true, hash, hash));
        QVERIFY(!publishedDigestAgrees(true, true, QByteArray(32, 'b'), hash));
        const auto notes = sanitizeReleaseNotes(QString(600, QLatin1Char('n')) + QChar(1) + QStringLiteral("\r\n<b>ok</b>"));
        QCOMPARE(notes.size(), 480);
        QVERIFY(!notes.contains(QChar(1)));
        const auto marked = sanitizeReleaseNotes(QStringLiteral("line\r\n<b>ok</b>"));
        QCOMPARE(marked, QStringLiteral("line\n<b>ok</b>"));
    }
    void manifestSelection() {
        Key key;
        QVERIFY(key.ok);
        const auto version = QStringLiteral("2.0.1");
        const auto asset = packageAssetName(version, QStringLiteral("NVIDIA"));
        const auto good = envelope(key, payloadOf(version, {packageRow(version, QStringLiteral("NVIDIA"), asset)}));
        const auto selected = readUpdateManifest(good, QStringLiteral("NVIDIA"), QStringLiteral("2.0.0"), version, key.pub);
        QVERIFY(selected);
        QCOMPARE(selected->asset, asset);
        QCOMPARE(selected->version, version);
        QCOMPARE(selected->digest, QByteArray::fromHex(QByteArray(64, 'a')));
        QVERIFY(!publishedDigestAgrees(true, true, QByteArray(32, 'b'), selected->digest));
        QVERIFY(publishedDigestAgrees(true, true, selected->digest, selected->digest));
        QVERIFY(!readUpdateManifest(good, QStringLiteral("NVIDIA"), QStringLiteral("2.0.1"), version, key.pub));
        QVERIFY(!readUpdateManifest(good, QStringLiteral("NVIDIA"), QStringLiteral("9.0.0"), version, key.pub));
        QVERIFY(!readUpdateManifest(good, QStringLiteral("AMD"), QStringLiteral("2.0.0"), version, key.pub));
        QVERIFY(!readUpdateManifest(good, QStringLiteral("NVIDIA"), QStringLiteral("2.0.0"), QStringLiteral("9.9.9"), key.pub));
        auto tampered = good;
        tampered[20] = static_cast<char>(tampered[20] ^ 0x1);
        QVERIFY(!readUpdateManifest(tampered, QStringLiteral("NVIDIA"), QStringLiteral("2.0.0"), version, key.pub));
        const auto badSignature = envelope(key, payloadOf(version, {packageRow(version, QStringLiteral("NVIDIA"), asset)}),
                                            QByteArray(64, '\x2'));
        QVERIFY(!readUpdateManifest(badSignature, QStringLiteral("NVIDIA"), QStringLiteral("2.0.0"), version, key.pub));
        const auto traversal = envelope(key, payloadOf(version, {packageRow(version, QStringLiteral("NVIDIA"),
                                                                             QStringLiteral("../") + asset)}));
        QVERIFY(!readUpdateManifest(traversal, QStringLiteral("NVIDIA"), QStringLiteral("2.0.0"), version, key.pub));
        const auto uppercase = packageRow(version, QStringLiteral("NVIDIA"), asset);
        auto upper = uppercase;
        upper["sha256"] = QString(64, QLatin1Char('A'));
        QVERIFY(!readUpdateManifest(envelope(key, payloadOf(version, {upper})), QStringLiteral("NVIDIA"),
                                    QStringLiteral("2.0.0"), version, key.pub));
        QVERIFY(!readUpdateManifest(envelope(key, payloadOf(version, {packageRow(version, QStringLiteral("NVIDIA"), asset, 0)})),
                                    QStringLiteral("NVIDIA"), QStringLiteral("2.0.0"), version, key.pub));
        QVERIFY(!readUpdateManifest(envelope(key, payloadOf(version, {packageRow(version, QStringLiteral("NVIDIA"), asset),
                                                                      packageRow(version, QStringLiteral("NVIDIA"), asset)})),
                                    QStringLiteral("NVIDIA"), QStringLiteral("2.0.0"), version, key.pub));
        QVERIFY(!readUpdateManifest(envelope(key, payloadOf(version, {packageRow(version, QStringLiteral("NVIDIA"), asset)},
                                                           QStringLiteral("hello"), QStringLiteral("windows-arm64"))),
                                    QStringLiteral("NVIDIA"), QStringLiteral("2.0.0"), version, key.pub));
        const auto notes = sanitizeReleaseNotes(QString(500, QLatin1Char('n')));
        const auto trimmed = readUpdateManifest(envelope(key, payloadOf(version, {packageRow(version, QStringLiteral("NVIDIA"), asset)},
                                                                        QString(500, QLatin1Char('n')))),
                                                QStringLiteral("NVIDIA"), QStringLiteral("2.0.0"), version, key.pub);
        QVERIFY(trimmed);
        QCOMPARE(trimmed->notes.size(), 480);
        QCOMPARE(trimmed->notes, notes.left(480));
        const QByteArray empty;
        QVERIFY(!readUpdateManifest(empty, QStringLiteral("NVIDIA"), QStringLiteral("2.0.0"), version, key.pub));
    }
    void authenticodePolicy() {
        QVERIFY(acceptAuthenticode(0, 0));
        QVERIFY(acceptAuthenticode(static_cast<long>(0x800B0100L), 0x800B0100L));
        QVERIFY(acceptAuthenticode(static_cast<long>(0x800B0100L), 0x800B0001L));
        QVERIFY(acceptAuthenticode(static_cast<long>(0x800B0100L), 0x800B0003L));
        QVERIFY(!acceptAuthenticode(static_cast<long>(0x800B0100L), 0x80096010L));
        QVERIFY(!acceptAuthenticode(static_cast<long>(0x800B0004L), 0));
    }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    PortableUpdateRulesTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "PortableUpdateRulesTests.moc"
