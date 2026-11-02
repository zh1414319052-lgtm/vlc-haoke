/* Native state/policy tests; isolated temporary storage, no UI or network. */
#include "../update_controller.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
using namespace HaokeUpdates;
static int checked = 0;
static void expect(bool condition) { ++checked; if (!condition) { std::fprintf(stderr, "check %d failed\n", checked); std::exit(1); } }
template<typename F> static void rejects(F fn) { bool rejected = false; try { fn(); } catch (const Error &) { rejected = true; } expect(rejected); }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); QTemporaryDir dir; expect(dir.isValid());
    Config c; c.softwareId = "native-state-test"; c.applicationId = "org.haoke.fixture"; c.channel = "stable";
    c.publisher = "CN=Isolated Test"; c.certificateSha256 = QString(64, 'a'); c.versionCode = 1;
    c.versionName = "1"; c.architecture = "x64"; c.osVersion = "10.0"; c.publicKeys.insert("isolated", QByteArray(32, 'a'));
    StateStore store(dir.path(), c);
    UpdateState s = store.mutate([](UpdateState &) {}); expect(!s.checkpoint.present && s.lastCheckAt == 0);
    const qint64 now = 2000000000;
    expect(!suppressed(s, 5, now));
    s.promptedVersionCode = 5; expect(suppressed(s, 5, now)); expect(!suppressed(s, 6, now));
    s.snoozedVersionCode = 5; s.snoozedUntil = now + 86400000;
    expect(suppressed(s, 5, now)); expect(!suppressed(s, 5, s.snoozedUntil));
    s.ignoredVersionCode = 5; expect(suppressed(s, 5, now + 172800000)); expect(!suppressed(s, 6, now));
    s.lastCheckAt = now; s.checkpoint.present = true; s.checkpoint.revision = 4; s.checkpoint.maxVersionCode = 5; s.checkpoint.digest = QByteArray(32, 'x');
    store.mutate([&](UpdateState &x) { x = s; });
    StateStore reopened(dir.path(), c); UpdateState loaded = reopened.mutate([](UpdateState &) {});
    expect(loaded.checkpoint.present && loaded.checkpoint.revision == 4 && loaded.checkpoint.digest == s.checkpoint.digest && loaded.checkpoint.maxVersionCode == 5);
    expect(loaded.ignoredVersionCode == 5 && loaded.snoozedUntil == s.snoozedUntil && loaded.promptedVersionCode == 5 && loaded.lastCheckAt == now);
    Config other = c; other.channel = "qa"; StateStore qa(dir.path(), other); expect(qa.path() != store.path());
    expect(!qa.mutate([](UpdateState &) {}).checkpoint.present);
    other = c; other.publisher += " other"; expect(StateStore(dir.path(), other).path() != store.path());
    other = c; other.certificateSha256 = QString(64, 'b'); expect(StateStore(dir.path(), other).path() != store.path());
    other = c; other.softwareId += "other"; expect(StateStore(dir.path(), other).path() != store.path());
    other = c; other.applicationId += "other"; expect(StateStore(dir.path(), other).path() != store.path());
    QJsonObject o = stateJSON(s, store.scope()); o.remove("lastCheckAt"); o.insert("lastAutomaticAt", 123.0);
    UpdateState migrated = parseState(o, store.scope()); expect(migrated.lastCheckAt == 123 && migrated.checkpoint.digest == s.checkpoint.digest && migrated.ignoredVersionCode == 5 && migrated.snoozedUntil == s.snoozedUntil);
    o = stateJSON(s, store.scope()); o.insert("scope", "wrong"); rejects([&]() { parseState(o, store.scope()); });
    o = stateJSON(s, store.scope()); o.insert("lastCheckAt", -1.0); rejects([&]() { parseState(o, store.scope()); });
    o = stateJSON(s, store.scope()); QJsonObject cp = o.value("checkpoint").toObject(); cp.insert("digest", "bad"); o.insert("checkpoint", cp); rejects([&]() { parseState(o, store.scope()); });
    QLockFile held(store.path() + ".lock"); expect(held.tryLock()); rejects([&]() { store.mutate([](UpdateState &) {}); }); held.unlock();
    QFile f(store.path()); expect(f.open(QIODevice::WriteOnly | QIODevice::Truncate)); f.write("broken"); f.close();
    rejects([&]() { store.mutate([](UpdateState &) {}); });
    // A file in place of the expected directory is a deterministic write failure.
    QString denied = QDir(dir.path()).filePath("not-a-directory"); QFile marker(denied); expect(marker.open(QIODevice::WriteOnly)); marker.close();
    rejects([&]() { StateStore(denied, c).mutate([](UpdateState &) {}); });
    std::printf("native update state: %d checks passed\n", checked); return 0;
}
