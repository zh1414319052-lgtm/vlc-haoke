/* Read-only WinHTTP cancellation/drain test. No website writes or trust keys. */
#include "../update_controller.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cstdio>
#include <cstdlib>
using namespace HaokeUpdates;
static int checked = 0;
static void expect(bool ok) { ++checked; if (!ok) { std::fprintf(stderr, "transport check %d failed\n", checked); std::exit(1); } }
static Config config() {
    Config c; c.softwareId = "isolated-transport-test"; c.applicationId = "org.haoke.isolated-transport-not-registered"; c.channel = "qa";
    c.publisher = "CN=Isolated Test"; c.certificateSha256 = QString(64, 'a'); c.publicKeys.insert("isolated", QByteArray(32, 'a'));
    c.versionCode = 1; c.versionName = "1"; c.osVersion = "10.0"; c.architecture = "x64"; return c;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); const Config c = config();
    FetchThread *before = new FetchThread(c); before->cancel(); before->start(); expect(before->wait(12000)); expect(before->error == "CANCELLED"); delete before;
    FetchThread *actual = new FetchThread(c); actual->start(); expect(actual->wait(12000));
    std::printf("read-only nonexistent feed result: %s; network started %d\n", actual->error.toUtf8().constData(), actual->hasStartedNetwork());
    expect(actual->hasStartedNetwork()); expect(actual->error == "HTTP_REJECTED"); delete actual;
    qint64 longest = 0; int active = 0;
    for (int i = 0; i < 4; ++i) {
        FetchThread *job = new FetchThread(c); job->start(); QElapsedTimer ready; ready.start();
        while (job->isRunning() && !job->hasStartedNetwork() && ready.elapsed() < 2000) QThread::msleep(5);
        QThread::msleep(50 * (i + 1));
        if (job->hasActiveRequest()) ++active;
        QElapsedTimer drain; drain.start(); job->cancel(); expect(job->wait(12000));
        longest = qMax(longest, drain.elapsed()); expect(job->data.isEmpty()); delete job;
    }
    expect(active > 0);
    // Destructor itself must cancel and drain, even when caller forgets wait().
    FetchThread *owned = new FetchThread(c); owned->start(); QElapsedTimer started; started.start();
    while (owned->isRunning() && !owned->hasStartedNetwork() && started.elapsed() < 2000) QThread::msleep(5);
    QThread::msleep(100);
    QElapsedTimer destructor; destructor.start(); delete owned; expect(destructor.elapsed() < 12000);
    std::printf("native WinHTTP: %d checks passed; %d active cancellations; longest drain %lldms; destructor %lldms\n", checked, active, static_cast<long long>(longest), static_cast<long long>(destructor.elapsed())); return 0;
}
