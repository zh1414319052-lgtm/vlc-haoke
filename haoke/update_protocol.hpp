/* Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later */
/* Haoke signed notifications v2, native Windows adapter. GPL-2.0-or-later.
 * No signing secrets, installation, telemetry or server-supplied trust keys. */
#ifndef HAOKE_UPDATE_PROTOCOL_HPP
#define HAOKE_UPDATE_PROTOCOL_HPP
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextCodec>
#include <QStringList>
#include <gcrypt.h>
#include <cmath>
#include <stdexcept>

namespace HaokeUpdates {
struct Error : std::runtime_error { explicit Error(const char *code) : std::runtime_error(code) {} };
inline void require(bool ok, const char *code) { if (!ok) throw Error(code); }
struct Config {
    QString softwareId, applicationId, channel, publisher, certificateSha256;
    QMap<QString, QByteArray> publicKeys; // raw 32-byte, pinned by trusted build configuration
    qint64 versionCode;
    QString versionName, osVersion, architecture;
};
struct Checkpoint { qint64 revision = 0, maxVersionCode = 0; QByteArray digest; bool present = false; };
struct Receipt { QString availability; QJsonObject payload, release; Checkpoint checkpoint; qint64 expiresAt = 0; };
inline bool match(const QString &s, const char *pattern) { return QRegularExpression(QString::fromLatin1(pattern)).match(s).hasMatch(); }
inline bool integer(const QJsonValue &v, qint64 minimum = 0) {
    const double d = v.toDouble(-1);
    return v.isDouble() && std::isfinite(d) && d >= minimum && d <= 9007199254740991.0 && std::floor(d) == d;
}
inline void keys(const QJsonObject &obj, const char *list) {
    QStringList expected = QString::fromLatin1(list).split(','); expected.sort();
    require(obj.keys() == expected, "INVALID_FIELDS");
}
inline bool plain(const QJsonValue &v, int maximum) {
    if (!v.isString() || v.toString().size() > maximum) return false;
    for (QChar c : v.toString()) {
        ushort u = c.unicode();
        if (u < 9 || (u > 10 && u < 13) || (u > 13 && u < 32) || u == 127 || u == '<' || u == '>') return false;
    }
    return true;
}
inline QByteArray base64(const QJsonValue &value, int exact, int maximum) {
    require(value.isString(), "INVALID_BASE64");
    const QString text = value.toString();
    require(text.size() <= ((maximum + 2) / 3) * 4 && match(text, "\\A(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?\\z"), "INVALID_BASE64");
    const QByteArray result = QByteArray::fromBase64(text.toLatin1());
    require(result.size() <= maximum && (exact == 0 || result.size() == exact) && result.toBase64() == text.toLatin1(), "INVALID_BASE64");
    return result;
}
inline QJsonObject objectJSON(const QByteArray &bytes, const char *code) {
    QTextCodec::ConverterState state;
    QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
    require(state.invalidChars == 0 && state.remainingChars == 0, code);
    QJsonParseError parse; QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    require(parse.error == QJsonParseError::NoError && document.isObject(), code);
    return document.object();
}
inline QByteArray quote(const QString &text) {
    QJsonArray a; a.append(text); const QByteArray b = QJsonDocument(a).toJson(QJsonDocument::Compact); return b.mid(1, b.size() - 2);
}
inline QByteArray canonical(const QJsonValue &v) {
    if (v.isObject()) {
        const QJsonObject o = v.toObject(); QByteArray b("{"); bool first = true;
        for (const QString &key : o.keys()) { if (!first) b += ','; first = false; b += quote(key) + ':' + canonical(o.value(key)); }
        return b + '}';
    }
    if (v.isArray()) { QByteArray b("["); bool first = true; for (const QJsonValue &x : v.toArray()) { if (!first) b += ','; first = false; b += canonical(x); } return b + ']'; }
    if (v.isString()) return quote(v.toString());
    if (v.isBool()) return v.toBool() ? "true" : "false";
    if (v.isNull()) return "null";
    require(integer(v), "INVALID_NUMBER"); return QByteArray::number(static_cast<qint64>(v.toDouble()));
}
inline qint64 time(const QJsonValue &v) {
    require(v.isString() && match(v.toString(), "\\A[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{3}Z\\z"), "INVALID_TIME");
    const QString text = v.toString(); QDateTime d = QDateTime::fromString(text, "yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"); d.setTimeSpec(Qt::UTC);
    require(d.isValid() && d.toString("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'") == text, "INVALID_TIME"); return d.toMSecsSinceEpoch();
}
inline QList<int> osParts(const QString &v) {
    require(match(v, "\\A[0-9]{1,5}(?:\\.[0-9]{1,5}){0,3}\\z"), "INVALID_OS_VERSION");
    QList<int> parts; for (const QString &p : v.split('.')) parts.append(p.toInt()); return parts;
}
inline bool supportsOS(const QString &actual, const QString &minimum) {
    const QList<int> a = osParts(actual), b = osParts(minimum);
    for (int i = 0; i < qMax(a.size(), b.size()); ++i) if (a.value(i) != b.value(i)) return a.value(i) > b.value(i);
    return true;
}
inline QJsonObject identity(const Config &c) {
    QJsonObject o; o.insert("kind", "authenticode"); o.insert("publisher", c.publisher); o.insert("certificateSha256", c.certificateSha256); return o;
}
inline void validateConfig(const Config &c) {
    require(match(c.softwareId, "\\A[A-Za-z0-9][A-Za-z0-9_-]{0,79}\\z") && match(c.applicationId, "\\A[A-Za-z0-9][A-Za-z0-9._-]{0,127}\\z") && (c.channel == "stable" || c.channel == "qa"), "INVALID_CONFIGURATION");
    require(plain(c.publisher, 500) && !c.publisher.trimmed().isEmpty() && match(c.certificateSha256, "\\A[a-f0-9]{64}\\z"), "INVALID_IDENTITY");
    require(!c.publicKeys.isEmpty(), "MISSING_PIN");
    for (auto it = c.publicKeys.constBegin(); it != c.publicKeys.constEnd(); ++it) require(!it.key().isEmpty() && it.value().size() == 32, "MISSING_PIN");
    require(c.versionCode >= 0 && c.versionCode <= 9007199254740991LL && QStringList({"x86", "x64", "arm64"}).contains(c.architecture), "INVALID_DEVICE"); osParts(c.osVersion);
}
inline bool verifySignature(const QByteArray &data, const QByteArray &signature, const QByteArray &key) {
    if (!gcry_check_version("1.8.0") || signature.size() != 64 || key.size() != 32) return false;
    gcry_sexp_t pk = nullptr, sig = nullptr, message = nullptr;
    gcry_error_t e = gcry_sexp_build(&pk, nullptr, "(public-key(ecc(curve Ed25519)(flags eddsa)(q %b)))", key.size(), key.constData());
    if (!e) e = gcry_sexp_build(&sig, nullptr, "(sig-val(eddsa(r %b)(s %b)))", 32, signature.constData(), 32, signature.constData() + 32);
    if (!e) e = gcry_sexp_build(&message, nullptr, "(data(flags eddsa)(hash-algo sha512)(value %b))", data.size(), data.constData());
    if (!e) e = gcry_pk_verify(sig, message, pk);
    gcry_sexp_release(message); gcry_sexp_release(sig); gcry_sexp_release(pk); return e == 0;
}
inline Checkpoint acceptCheckpoint(const Receipt &r, const Checkpoint &previous) {
    Checkpoint next = r.checkpoint;
    if (previous.present) {
        require(previous.revision >= 0 && previous.maxVersionCode >= 0 && previous.digest.size() == 32, "INVALID_CHECKPOINT");
        require(next.revision >= previous.revision, "REVISION_ROLLBACK");
        require(next.revision != previous.revision || next.digest == previous.digest, "REVISION_CONTENT_CHANGED");
        if (!r.release.isEmpty()) require(next.maxVersionCode >= previous.maxVersionCode, "VERSION_ROLLBACK");
        next.maxVersionCode = qMax(previous.maxVersionCode, next.maxVersionCode);
    }
    return next;
}
inline Receipt verifyEnvelope(const QByteArray &bytes, const Config &c, const Checkpoint &previous = Checkpoint(), qint64 now = QDateTime::currentMSecsSinceEpoch()) {
    validateConfig(c); require(bytes.size() <= 65536, "INVALID_ENVELOPE");
    const QJsonObject envelope = objectJSON(bytes, "INVALID_ENVELOPE"); keys(envelope, "algorithm,keyId,payload,signature");
    require(envelope.value("algorithm") == "Ed25519" && envelope.value("keyId").isString() && c.publicKeys.contains(envelope.value("keyId").toString()), "UNTRUSTED_KEY");
    const QByteArray payloadBytes = base64(envelope.value("payload"), 0, 32768), signature = base64(envelope.value("signature"), 64, 64);
    require(verifySignature(payloadBytes, signature, c.publicKeys.value(envelope.value("keyId").toString())), "INVALID_SIGNATURE");
    Receipt r; r.payload = objectJSON(payloadBytes, "INVALID_PAYLOAD"); const QJsonObject &p = r.payload;
    keys(p, "schemaVersion,platform,softwareId,applicationId,channel,identity,revision,issuedAt,expiresAt,status,release");
    require(p.value("schemaVersion") == 2, "UNKNOWN_SCHEMA");
    require(p.value("platform") == "Windows" && p.value("softwareId") == c.softwareId && p.value("applicationId") == c.applicationId && p.value("channel") == c.channel && p.value("identity").isObject() && p.value("identity").toObject() == identity(c), "IDENTITY_MISMATCH");
    require(integer(p.value("revision")) && QStringList({"none", "published", "withdrawn"}).contains(p.value("status").toString()), "INVALID_STATE");
    const qint64 issued = time(p.value("issuedAt")); r.expiresAt = time(p.value("expiresAt"));
    require(issued <= now + 300000 && r.expiresAt > now && r.expiresAt > issued && r.expiresAt - issued <= 172800000, "EXPIRED_OR_FUTURE");
    const qint64 revision = static_cast<qint64>(p.value("revision").toDouble());
    if (p.value("status") != "published") {
        require(p.value("release").isNull() && (p.value("status") == "none" ? revision == 0 : revision > 0), "INVALID_STATE"); r.availability = p.value("status").toString();
    } else {
        require(revision > 0 && p.value("release").isObject(), "INVALID_STATE"); r.release = p.value("release").toObject(); const QJsonObject &rel = r.release;
        keys(rel, "releaseId,softwareId,versionCode,versionName,publishedAt,notes,pageUrl,artifact");
        require(match(rel.value("releaseId").toString(), "\\A[A-Za-z0-9][A-Za-z0-9_-]{0,79}\\z") && rel.value("softwareId") == c.softwareId && integer(rel.value("versionCode"), 1) && plain(rel.value("versionName"), 80) && !rel.value("versionName").toString().trimmed().isEmpty(), "INVALID_RELEASE");
        require(time(rel.value("publishedAt")) <= issued + 300000, "INVALID_TIME");
        require(rel.value("notes").isObject(), "INVALID_NOTES"); const QJsonObject notes = rel.value("notes").toObject(); keys(notes, "zh,en"); require(plain(notes.value("zh"), 4000) && plain(notes.value("en"), 4000), "INVALID_NOTES");
        require(rel.value("artifact").isObject(), "INVALID_ARTIFACT"); const QJsonObject art = rel.value("artifact").toObject(); keys(art, "distribution,sha256,sizeBytes,minOS,architectures,actionUrl");
        const QString suffix = c.softwareId + "/Windows/" + c.channel + "/" + rel.value("releaseId").toString();
        // Materialize the builders: VLC enables QT_USE_QSTRINGBUILDER, and
        // Qt 5.6 QJsonValue has no comparison overload for QStringBuilder.
        const QString expectedPage = "https://haoke.org/software/" + c.softwareId + "/releases/v2/Windows/" + c.channel + "/" + rel.value("releaseId").toString();
        const QString expectedAction = "https://haoke.org/api/client-updates/v2/releases/" + suffix + "/download";
        require(rel.value("pageUrl").isString() && rel.value("pageUrl").toString() == expectedPage && art.value("actionUrl").isString() && art.value("actionUrl").toString() == expectedAction, "UNTRUSTED_URL");
        require(art.value("distribution") == "direct" && match(art.value("sha256").toString(), "\\A[a-f0-9]{64}\\z") && integer(art.value("sizeBytes"), 1), "INVALID_ARTIFACT");
        require(art.value("minOS").isString() && art.value("architectures").isArray(), "INVALID_ARTIFACT"); osParts(art.value("minOS").toString());
        QStringList architectures; for (const QJsonValue &a : art.value("architectures").toArray()) { require(a.isString() && QStringList({"x86", "x64", "arm64"}).contains(a.toString()) && !architectures.contains(a.toString()), "INVALID_ARCHITECTURE"); architectures.append(a.toString()); }
        require(!architectures.isEmpty(), "INVALID_ARCHITECTURE");
        r.availability = rel.value("versionCode").toDouble() <= c.versionCode ? "current" : !architectures.contains(c.architecture) || !supportsOS(c.osVersion, art.value("minOS").toString()) ? "incompatible" : "available";
    }
    QJsonObject business = p; business.remove("issuedAt"); business.remove("expiresAt");
    r.checkpoint.present = true; r.checkpoint.revision = revision; r.checkpoint.digest = QCryptographicHash::hash(canonical(business), QCryptographicHash::Sha256);
    r.checkpoint.maxVersionCode = r.release.isEmpty() ? 0 : static_cast<qint64>(r.release.value("versionCode").toDouble());
    r.checkpoint = acceptCheckpoint(r, previous); return r;
}
inline QString feedURL(const Config &c) { return "https://haoke.org/api/client-updates/v2/Windows/" + c.applicationId + "/" + c.channel; }
} // namespace HaokeUpdates
#endif
