/* Native fixture and negative protocol tests, GPL-2.0-or-later. */
#include "../update_protocol.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
extern "C" int __cdecl fstat64i32(int, struct _stat64i32 *);
using namespace HaokeUpdates;
static int checks = 0;
static void check(bool condition, const char *label) { ++checks; if (!condition) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); } }
template<class F> static void rejects(F operation, const char *expected) {
    try { operation(); check(false, expected); } catch (const Error &e) { check(QByteArray(e.what()) == expected, e.what()); }
}
static QByteArray compact(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
struct TestSigner {
    gcry_sexp_t key = nullptr, privateKey = nullptr;
    QByteArray publicKey;
    TestSigner() {
        gcry_sexp_t parameters = nullptr;
        check(gcry_check_version("1.8.0") != nullptr, "libgcrypt version");
        check(!gcry_sexp_build(&parameters, nullptr, "(genkey(ecc(curve Ed25519)(flags eddsa)))"), "key parameters");
        check(!gcry_pk_genkey(&key, parameters), "ephemeral test key"); gcry_sexp_release(parameters);
        privateKey = gcry_sexp_find_token(key, "private-key", 0);
        gcry_sexp_t q = gcry_sexp_find_token(key, "q", 0); size_t length = 0;
        const char *data = gcry_sexp_nth_data(q, 1, &length); publicKey = QByteArray(data, static_cast<int>(length));
        if (publicKey.size() == 33 && static_cast<unsigned char>(publicKey[0]) == 0x40) publicKey.remove(0, 1);
        gcry_sexp_release(q); check(publicKey.size() == 32, "test public key length");
    }
    ~TestSigner() { gcry_sexp_release(privateKey); gcry_sexp_release(key); }
    QByteArray envelope(const QJsonObject &p) {
        const QByteArray bytes = compact(p); gcry_sexp_t data = nullptr, signature = nullptr;
        check(!gcry_sexp_build(&data, nullptr, "(data(flags eddsa)(hash-algo sha512)(value %b))", bytes.size(), bytes.constData()), "sign parameters");
        check(!gcry_pk_sign(&signature, data, privateKey), "test sign"); gcry_sexp_release(data);
        QByteArray raw;
        for (const char *name : {"r", "s"}) { gcry_sexp_t part = gcry_sexp_find_token(signature, name, 0); size_t length = 0; const char *value = gcry_sexp_nth_data(part, 1, &length); check(length == 32, "test signature part"); raw.append(value, static_cast<int>(length)); gcry_sexp_release(part); }
        gcry_sexp_release(signature); QJsonObject e; e.insert("algorithm", "Ed25519"); e.insert("keyId", "ephemeral"); e.insert("payload", QString::fromLatin1(bytes.toBase64())); e.insert("signature", QString::fromLatin1(raw.toBase64())); return compact(e);
    }
};
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv); check(argc == 2, "fixture argument");
    QFile file(QString::fromLocal8Bit(argv[1])); check(file.open(QIODevice::ReadOnly), "fixture opened");
    const QString fixturePath = QFileInfo(file).absoluteFilePath();
    int fd = _wopen(reinterpret_cast<const wchar_t *>(fixturePath.utf16()), _O_RDONLY | _O_BINARY);
    check(fd >= 0, "CRT Unicode fixture open"); struct _stat64i32 statResult;
    check(fstat64i32(fd, &statResult) == 0 && statResult.st_size == file.size(), "new contrib stat ABI adapter"); _close(fd);
    QJsonObject f = objectJSON(file.readAll(), "FIXTURE_JSON");
    Config c; c.softwareId = f.value("softwareId").toString(); c.applicationId = f.value("applicationId").toString(); c.channel = "stable";
    const QJsonObject identity = f.value("identities").toObject().value("Windows").toObject();
    c.publisher = identity.value("publisher").toString(); c.certificateSha256 = identity.value("certificateSha256").toString();
    c.publicKeys.insert(f.value("keyId").toString(), QByteArray::fromBase64(f.value("publicKeyBase64").toString().toLatin1()));
    c.versionCode = 0; c.versionName = "test"; c.osVersion = "10.0.26200"; c.architecture = "x64";
    const qint64 now = static_cast<qint64>(f.value("testNow").toDouble());
    QByteArray published; QJsonObject payload; Checkpoint publishedCheckpoint, sequential;
    for (const QJsonValue &v : f.value("fixtures").toArray()) {
        const QJsonObject fixture = v.toObject(); const QByteArray e = compact(fixture.value("envelope").toObject());
        if (fixture.value("platform") != "Windows") { rejects([&] { verifyEnvelope(e, c, Checkpoint(), now); }, "IDENTITY_MISMATCH"); continue; }
        Receipt result = verifyEnvelope(e, c, sequential, now); sequential = result.checkpoint;
        check(result.availability == (fixture.value("phase") == "published" ? "available" : fixture.value("phase").toString()), "server Windows state");
        if (fixture.value("phase") == "published") { published = e; payload = result.payload; publishedCheckpoint = result.checkpoint; }
    }
    check(!published.isEmpty(), "server published fixture");
    check(publishedCheckpoint.digest.toHex() == "0dcef384fe8a41fc092ff36d00c5b8590fff1d43793659df31b7ce819f67efbe", "canonical digest matches official JS SDK");
    check(sequential.maxVersionCode == publishedCheckpoint.maxVersionCode, "withdrawal preserves maximum version");
    rejects([&] { verifyEnvelope(published, c, sequential, now); }, "REVISION_ROLLBACK");
    TestSigner signer; Config local = c; local.publicKeys.insert("ephemeral", signer.publicKey);
    QByteArray signedLocal = signer.envelope(payload);
    Receipt current = verifyEnvelope(signedLocal, local, Checkpoint(), now); check(current.availability == "available", "ephemeral signed receipt");
    QJsonObject changed = payload; changed.insert("issuedAt", "2026-10-05T01:00:00.000Z"); changed.insert("expiresAt", "2026-10-06T01:00:00.000Z");
    Receipt renewed = verifyEnvelope(signer.envelope(changed), local, publishedCheckpoint, now + 3600000);
    check(renewed.checkpoint.digest == publishedCheckpoint.digest, "same revision renewal");
    changed = payload; QJsonObject rel = changed.value("release").toObject(); QJsonObject notes = rel.value("notes").toObject(); notes.insert("en", "changed"); rel.insert("notes", notes); changed.insert("release", rel);
    rejects([&] { verifyEnvelope(signer.envelope(changed), local, publishedCheckpoint, now); }, "REVISION_CONTENT_CHANGED");
    Checkpoint future = publishedCheckpoint; ++future.revision;
    rejects([&] { verifyEnvelope(published, c, future, now); }, "REVISION_ROLLBACK");
    future = publishedCheckpoint; future.maxVersionCode = publishedCheckpoint.maxVersionCode + 1;
    rejects([&] { verifyEnvelope(published, c, future, now); }, "VERSION_ROLLBACK");
    Config other = c; other.channel = "qa"; rejects([&] { verifyEnvelope(published, other, Checkpoint(), now); }, "IDENTITY_MISMATCH");
    other = c; other.softwareId += "-other"; rejects([&] { verifyEnvelope(published, other, Checkpoint(), now); }, "IDENTITY_MISMATCH");
    other = c; other.publisher += " other"; rejects([&] { verifyEnvelope(published, other, Checkpoint(), now); }, "IDENTITY_MISMATCH");
    other = c; other.certificateSha256.fill('b'); rejects([&] { verifyEnvelope(published, other, Checkpoint(), now); }, "IDENTITY_MISMATCH");
    other = c; other.applicationId += ".other"; rejects([&] { verifyEnvelope(published, other, Checkpoint(), now); }, "IDENTITY_MISMATCH");
    other = c; other.publicKeys.clear(); other.publicKeys.insert("other", signer.publicKey); rejects([&] { verifyEnvelope(published, other, Checkpoint(), now); }, "UNTRUSTED_KEY");
    other = c; other.publicKeys[c.publicKeys.firstKey()] = signer.publicKey; rejects([&] { verifyEnvelope(published, other, Checkpoint(), now); }, "INVALID_SIGNATURE");
    rejects([&] { verifyEnvelope(published, c, Checkpoint(), now + 172800000); }, "EXPIRED_OR_FUTURE");
    rejects([&] { verifyEnvelope(QByteArray(65537, 'x'), c, Checkpoint(), now); }, "INVALID_ENVELOPE");
    QJsonObject envelope = objectJSON(published, "JSON"); envelope.insert("extra", true);
    rejects([&] { verifyEnvelope(compact(envelope), c, Checkpoint(), now); }, "INVALID_FIELDS");
    envelope = objectJSON(published, "JSON"); envelope.insert("payload", QString(envelope.value("payload").toString() + "\n"));
    rejects([&] { verifyEnvelope(compact(envelope), c, Checkpoint(), now); }, "INVALID_BASE64");
    envelope = objectJSON(published, "JSON"); QByteArray altered = QByteArray::fromBase64(envelope.value("payload").toString().toLatin1()); altered[20] = altered[20] ^ 1; envelope.insert("payload", QString::fromLatin1(altered.toBase64()));
    rejects([&] { verifyEnvelope(compact(envelope), c, Checkpoint(), now); }, "INVALID_SIGNATURE");
    changed = payload; changed.insert("schemaVersion", 9); rejects([&] { verifyEnvelope(signer.envelope(changed), local, Checkpoint(), now); }, "UNKNOWN_SCHEMA");
    changed = payload; changed.insert("revision", -1); rejects([&] { verifyEnvelope(signer.envelope(changed), local, Checkpoint(), now); }, "INVALID_STATE");
    changed = payload; changed.insert("status", "none"); rejects([&] { verifyEnvelope(signer.envelope(changed), local, Checkpoint(), now); }, "INVALID_STATE");
    changed = payload; rel = changed.value("release").toObject(); rel.insert("pageUrl", "https://example.com/"); changed.insert("release", rel); rejects([&] { verifyEnvelope(signer.envelope(changed), local, Checkpoint(), now); }, "UNTRUSTED_URL");
    changed = payload; rel = changed.value("release").toObject(); notes = rel.value("notes").toObject(); notes.insert("zh", "<script>"); rel.insert("notes", notes); changed.insert("release", rel); rejects([&] { verifyEnvelope(signer.envelope(changed), local, Checkpoint(), now); }, "INVALID_NOTES");
    changed = payload; changed.insert("expiresAt", "2026-10-08T00:00:00.000Z"); rejects([&] { verifyEnvelope(signer.envelope(changed), local, Checkpoint(), now); }, "EXPIRED_OR_FUTURE");
    changed = payload; changed.insert("issuedAt", "2026-10-05T00:06:00.000Z"); rejects([&] { verifyEnvelope(signer.envelope(changed), local, Checkpoint(), now); }, "EXPIRED_OR_FUTURE");
    changed = payload; changed.insert("issuedAt", "2026-02-30T00:00:00.000Z"); rejects([&] { verifyEnvelope(signer.envelope(changed), local, Checkpoint(), now); }, "INVALID_TIME");
    other = c; other.versionCode = publishedCheckpoint.maxVersionCode; check(verifyEnvelope(published, other, Checkpoint(), now).availability == "current", "already current");
    other = c; other.osVersion = "1.0"; check(verifyEnvelope(published, other, Checkpoint(), now).availability == "incompatible", "OS incompatible");
    other = c; other.architecture = "arm64"; check(verifyEnvelope(published, other, Checkpoint(), now).availability == "incompatible", "architecture incompatible");
    check(supportsOS("10.0.26200", "10.0") && !supportsOS("10.0", "10.0.26200"), "OS padding");
    std::printf("PASS %d native Windows update checks (real server fixtures + isolated signing)\n", checks);
}
