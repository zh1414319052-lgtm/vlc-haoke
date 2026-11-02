/* Haoke edition modifications, 2026-11-02. SPDX-License-Identifier: GPL-2.0-or-later */
/* Native Haoke signed update UI/lifecycle. GPL-2.0-or-later.
 * Fixed HTTPS origin, bounded foreground checks; never installs media/software. */
#ifndef HAOKE_UPDATE_CONTROLLER_HPP
#define HAOKE_UPDATE_CONTROLLER_HPP
// VLC's checked msleep(d) macro must not rewrite QThread::msleep's declaration.
// Restore it immediately after the Qt headers; callers retain VLC semantics.
#ifdef msleep
#pragma push_macro("msleep")
#undef msleep
#define HAOKE_RESTORE_VLC_MSLEEP
#endif
#include "update_protocol.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#ifdef HAOKE_RESTORE_VLC_MSLEEP
#pragma pop_macro("msleep")
#undef HAOKE_RESTORE_VLC_MSLEEP
#endif
#include <atomic>
#include <functional>
#include <mutex>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

namespace HaokeUpdates {
struct UpdateState {
    Checkpoint checkpoint;
    qint64 lastCheckAt = 0, promptedVersionCode = 0, ignoredVersionCode = 0;
    qint64 snoozedVersionCode = 0, snoozedUntil = 0;
};
inline bool suppressed(const UpdateState &s, qint64 version, qint64 now) {
    if (s.ignoredVersionCode == version) return true;
    if (s.snoozedVersionCode == version && s.snoozedUntil > 0) return now < s.snoozedUntil;
    return s.promptedVersionCode == version;
}
inline QJsonObject stateJSON(const UpdateState &s, const QString &scope) {
    QJsonObject o; o.insert("scope", scope); o.insert("schemaVersion", 1);
    o.insert("lastCheckAt", static_cast<double>(s.lastCheckAt));
    o.insert("promptedVersionCode", static_cast<double>(s.promptedVersionCode));
    o.insert("ignoredVersionCode", static_cast<double>(s.ignoredVersionCode));
    o.insert("snoozedVersionCode", static_cast<double>(s.snoozedVersionCode));
    o.insert("snoozedUntil", static_cast<double>(s.snoozedUntil));
    if (s.checkpoint.present) {
        QJsonObject c; c.insert("revision", static_cast<double>(s.checkpoint.revision));
        c.insert("maxVersionCode", static_cast<double>(s.checkpoint.maxVersionCode));
        c.insert("digest", QString::fromLatin1(s.checkpoint.digest.toHex())); o.insert("checkpoint", c);
    } else o.insert("checkpoint", QJsonValue(QJsonValue::Null));
    return o;
}
inline UpdateState parseState(const QJsonObject &o, const QString &scope) {
    require(o.value("scope") == scope && o.value("schemaVersion") == 1, "INVALID_STORED_STATE");
    UpdateState s;
    auto read = [&o](const char *key, const char *legacy = nullptr) {
        QJsonValue v = o.value(QString::fromLatin1(key));
        if (v.isUndefined() && legacy) v = o.value(QString::fromLatin1(legacy));
        if (v.isUndefined() || v.isNull()) return qint64(0);
        require(integer(v), "INVALID_STORED_STATE"); return static_cast<qint64>(v.toDouble());
    };
    s.lastCheckAt = read("lastCheckAt", "lastAutomaticAt");
    s.promptedVersionCode = read("promptedVersionCode"); s.ignoredVersionCode = read("ignoredVersionCode");
    s.snoozedVersionCode = read("snoozedVersionCode"); s.snoozedUntil = read("snoozedUntil");
    QJsonValue c = o.value("checkpoint");
    require(c.isNull() || c.isObject(), "INVALID_STORED_STATE");
    if (c.isObject()) {
        QJsonObject cp = c.toObject(); keys(cp, "revision,maxVersionCode,digest");
        require(integer(cp.value("revision")) && integer(cp.value("maxVersionCode")) && match(cp.value("digest").toString(), "\\A[a-f0-9]{64}\\z"), "INVALID_STORED_STATE");
        s.checkpoint.present = true; s.checkpoint.revision = static_cast<qint64>(cp.value("revision").toDouble());
        s.checkpoint.maxVersionCode = static_cast<qint64>(cp.value("maxVersionCode").toDouble());
        s.checkpoint.digest = QByteArray::fromHex(cp.value("digest").toString().toLatin1());
    }
    return s;
}
class StateStore {
    QString directory_, file_, scope_;
public:
    StateStore(const QString &directory, const Config &c) : directory_(directory) {
        QJsonObject binding; binding.insert("softwareId", c.softwareId); binding.insert("platform", "Windows");
        binding.insert("applicationId", c.applicationId); binding.insert("channel", c.channel); binding.insert("identity", identity(c));
        scope_ = QString::fromUtf8(canonical(binding));
        file_ = QDir(directory).filePath("haoke-updates-v2-" + QString::fromLatin1(QCryptographicHash::hash(scope_.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".json");
    }
    QString path() const { return file_; }
    QString scope() const { return scope_; }
    UpdateState mutate(const std::function<void(UpdateState &)> &change) const {
        require(QDir().mkpath(directory_), "STORAGE_UNAVAILABLE");
        QLockFile lock(file_ + ".lock"); lock.setStaleLockTime(30000);
        require(lock.tryLock(50), "STORAGE_BUSY");
        UpdateState state; QFile input(file_);
        if (input.exists()) {
            require(input.open(QIODevice::ReadOnly) && input.size() <= 16384, "STORAGE_UNAVAILABLE");
            state = parseState(objectJSON(input.readAll(), "INVALID_STORED_STATE"), scope_); input.close();
        }
        change(state);
        const QByteArray bytes = QJsonDocument(stateJSON(state, scope_)).toJson(QJsonDocument::Compact);
        QSaveFile output(file_); output.setDirectWriteFallback(false);
        require(output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit(), "STORAGE_UNAVAILABLE");
        return state;
    }
};

/* QThread has no parent while it runs. Controller retains every owned worker
 * through cancellation and drains all remaining workers before plugin unload.
 * Finished workers are normally reclaimed by the GUI's deferred deletion. */
class FetchThread : public QThread {
    QString path_;
    std::atomic<bool> cancelled_;
    std::atomic<bool> networkStarted_;
    std::mutex requestMutex_;
    HINTERNET request_ = nullptr;
    void closeRequest() {
        HINTERNET request = nullptr;
        { std::lock_guard<std::mutex> guard(requestMutex_); request = request_; request_ = nullptr; }
        if (request) WinHttpCloseHandle(request);
    }
protected:
    void run() override {
        HINTERNET session = nullptr, connection = nullptr, request = nullptr;
        try {
            require(!cancelled_, "CANCELLED");
            session = WinHttpOpen(L"Haoke-Update/2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            require(session != nullptr, "NETWORK_UNAVAILABLE");
            require(WinHttpSetTimeouts(session, 3000, 3000, 3000, 3000), "NETWORK_UNAVAILABLE");
            // TLS 1.2 is available throughout the supported Windows 10 range.
            // Newer SDK headers alone do not prove host support for TLS 1.3.
            DWORD tls = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            require(WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof(tls)), "NETWORK_UNAVAILABLE");
            connection = WinHttpConnect(session, L"haoke.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
            require(connection != nullptr && !cancelled_, "NETWORK_UNAVAILABLE");
            request = WinHttpOpenRequest(connection, L"GET", reinterpret_cast<LPCWSTR>(path_.utf16()), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            require(request != nullptr, "NETWORK_UNAVAILABLE");
            {
                std::lock_guard<std::mutex> guard(requestMutex_);
                if (cancelled_) { WinHttpCloseHandle(request); request = nullptr; throw Error("CANCELLED"); }
                request_ = request;
            }
            DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            require(WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy)), "NETWORK_UNAVAILABLE");
            DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS;
#ifdef WINHTTP_DISABLE_AUTHENTICATION
            disabled |= WINHTTP_DISABLE_AUTHENTICATION;
#endif
            require(WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)), "NETWORK_UNAVAILABLE");
            DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
            require(WinHttpSetOption(request, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon)), "NETWORK_UNAVAILABLE");
            networkStarted_ = true;
            require(WinHttpSendRequest(request, L"Accept: application/json\r\nCache-Control: no-store\r\n", -1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr), "NETWORK_UNAVAILABLE");
            DWORD status = 0, length = sizeof(status);
            require(WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX) && status == 200, "HTTP_REJECTED");
            wchar_t contentLength[64]; length = sizeof(contentLength);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, contentLength, &length, WINHTTP_NO_HEADER_INDEX)) {
                bool ok = false; qulonglong value = QString::fromWCharArray(contentLength).toULongLong(&ok);
                require(ok && value <= 65536, "ENVELOPE_TOO_LARGE");
            }
            QByteArray received;
            for (;;) {
                require(!cancelled_, "CANCELLED"); DWORD count = 0; char buffer[4096];
                require(WinHttpReadData(request, buffer, sizeof(buffer), &count), "NETWORK_UNAVAILABLE");
                if (count == 0) break;
                require(received.size() + static_cast<int>(count) <= 65536, "ENVELOPE_TOO_LARGE"); received.append(buffer, static_cast<int>(count));
            }
            require(!cancelled_, "CANCELLED"); data = received;
        } catch (const Error &e) { error = QString::fromLatin1(e.what()); }
          catch (...) { error = "NETWORK_UNAVAILABLE"; }
        closeRequest();
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
    }
public:
    QByteArray data;
    QString error;
    explicit FetchThread(const Config &c) : QThread(nullptr), path_("/api/client-updates/v2/Windows/" + c.applicationId + "/" + c.channel), cancelled_(false), networkStarted_(false) {
        validateConfig(c);
        QObject::connect(this, &QThread::finished, this, &QObject::deleteLater);
    }
    ~FetchThread() override { cancel(); if (isRunning()) wait(); }
    void cancel() { cancelled_ = true; closeRequest(); }
    bool hasActiveRequest() { std::lock_guard<std::mutex> guard(requestMutex_); return request_ != nullptr; }
    bool hasStartedNetwork() const { return networkStarted_; }
};

class Controller : public QObject {
    QPointer<QWidget> window_;
    Config config_;
    std::function<bool()> idle_;
    StateStore store_;
    QPointer<FetchThread> flight_;
    QList<QPointer<FetchThread>> workers_;
    QPointer<QMessageBox> dialog_;
    QTimer settle_, timeout_, retry_;
    quint64 generation_ = 0;
    int retries_ = 0;
    bool eligible_ = false, foreground_ = false, manual_ = false, presenting_ = false, dialogManual_ = false, valid_ = true, closing_ = false;
    bool pendingCheck_ = false, pendingManual_ = false;
    QString text(const char *zh, const char *en) const {
        return qgetenv("LANGUAGE").toLower().startsWith("zh") ? QString::fromUtf8(zh) : QString::fromUtf8(en);
    }
    QString title() const { return text("浩科版更新", "Haoke updates"); }
    bool foreground() const {
        return window_ && !closing_ && window_->isVisible() && !window_->isMinimized() && qApp && QApplication::applicationState() == Qt::ApplicationActive && window_->isActiveWindow();
    }
    bool safe(bool manual = false) const {
        return valid_ && foreground() && QApplication::activeModalWidget() == nullptr && QApplication::activePopupWidget() == nullptr && (manual || idle_());
    }
    void cancel() {
        ++generation_; settle_.stop(); timeout_.stop(); retry_.stop(); manual_ = false;
        pendingCheck_ = false; pendingManual_ = false;
        if (flight_) { flight_->cancel(); flight_.clear(); }
    }
    void status(const QString &message) {
        if (!foreground() || presenting_) return;
        presenting_ = true; dialogManual_ = true;
        QPointer<Controller> alive(this);
        QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Information, title(), message, QMessageBox::NoButton, window_));
        QPushButton *ok=box->addButton(text("确定", "OK"), QMessageBox::AcceptRole);
        box->setDefaultButton(ok); box->setEscapeButton(ok);
        dialog_ = box; box->setTextFormat(Qt::PlainText); box->exec();
        if (!alive) return;
        dialog_.clear(); presenting_ = false; if (box) delete box;
    }
    void failed(bool manual) {
        if (manual) status(text("暂时无法安全检查更新，请稍后重试。", "Updates could not be checked safely. Please try again later."));
        else scheduleRetry(60000);
    }
    void scheduleRetry(int minimum) {
        if (!eligible_ || !safe() || retries_ >= 2 || closing_) return;
        const int delay = qMax(minimum, retries_ == 0 ? 60000 : 300000); ++retries_;
        retry_.start(delay);
    }
    void present(const Receipt &r, bool manual) {
        if (!safe(manual) || r.expiresAt <= QDateTime::currentMSecsSinceEpoch()) return;
        const qint64 version = static_cast<qint64>(r.release.value("versionCode").toDouble());
        const quint64 generation = generation_;
        bool shown = false;
        qint64 previousPrompted = 0, previousSnoozedVersion = 0, previousSnoozedUntil = 0;
        try {
            store_.mutate([&](UpdateState &s) {
                if (!safe(manual) || generation != generation_ || (!manual && suppressed(s, version, QDateTime::currentMSecsSinceEpoch()))) return;
                require(s.checkpoint.present && s.checkpoint.revision == r.checkpoint.revision && s.checkpoint.digest == r.checkpoint.digest, "NO_CURRENT_OFFER");
                previousPrompted = s.promptedVersionCode; previousSnoozedVersion = s.snoozedVersionCode; previousSnoozedUntil = s.snoozedUntil;
                s.promptedVersionCode = version; s.snoozedVersionCode = 0; s.snoozedUntil = 0; shown = true;
            });
        } catch (...) { failed(manual); return; }
        if (!shown) return;
        // Persistence is synchronous and the UI thread has not yielded. The
        // actual modal-open boundary follows immediately, so unseen reminders
        // are never consumed by a delayed asynchronous storage completion.
        if (!safe(manual) || generation != generation_ || r.expiresAt <= QDateTime::currentMSecsSinceEpoch()) {
            try { store_.mutate([&](UpdateState &s) {
                if (s.promptedVersionCode == version && s.snoozedVersionCode == 0 && s.snoozedUntil == 0) {
                    s.promptedVersionCode = previousPrompted; s.snoozedVersionCode = previousSnoozedVersion; s.snoozedUntil = previousSnoozedUntil;
                }
            }); } catch (...) { /* Never display an unpersisted offer. */ }
            return;
        }
        presenting_ = true; dialogManual_ = manual;
        const QJsonObject notes = r.release.value("notes").toObject();
        const bool zh = qgetenv("LANGUAGE").toLower().startsWith("zh");
        QPointer<Controller> alive(this);
        QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Information, title(), text("发现新版本：", "New version: ") + r.release.value("versionName").toString() + "\n\n" + notes.value(zh ? "zh" : "en").toString(), QMessageBox::NoButton, window_));
        box->setTextFormat(Qt::PlainText);
        QPushButton *view = box->addButton(text("查看版本页", "View version page"), QMessageBox::AcceptRole);
        QPushButton *later = box->addButton(text("稍后", "Later"), QMessageBox::ActionRole);
        QPushButton *ignore = box->addButton(text("忽略此版本", "Ignore this version"), QMessageBox::DestructiveRole);
        QPushButton *close = box->addButton(text("关闭", "Close"), QMessageBox::RejectRole); box->setEscapeButton(close);
        dialog_ = box; box->exec();
        if (!alive) return;
        dialog_.clear(); presenting_ = false;
        if (!box) return;
        const bool clickedIgnore = box->clickedButton() == ignore, clickedLater = box->clickedButton() == later, clickedView = box->clickedButton() == view;
        delete box;
        if (closing_ || !window_ || generation != generation_ || r.expiresAt <= QDateTime::currentMSecsSinceEpoch()) return;
        try {
            auto current = [&r](const UpdateState &s) {
                require(s.checkpoint.present && s.checkpoint.revision == r.checkpoint.revision && s.checkpoint.digest == r.checkpoint.digest, "NO_CURRENT_OFFER");
            };
            if (clickedIgnore) store_.mutate([&](UpdateState &s) { current(s); s.ignoredVersionCode = version; });
            else if (clickedLater) store_.mutate([&](UpdateState &s) { current(s); s.snoozedVersionCode = version; s.snoozedUntil = QDateTime::currentMSecsSinceEpoch() + 86400000; });
            else if (clickedView && foreground()) {
                store_.mutate([&](UpdateState &s) { current(s); });
                QDesktopServices::openUrl(QUrl(r.release.value("pageUrl").toString()));
            }
        } catch (...) { failed(true); }
    }
    void completed(FetchThread *job, quint64 generation) {
        if (job != flight_ || generation != generation_) return;
        timeout_.stop(); flight_.clear(); const bool manual = manual_; manual_ = false;
        if (!safe(manual)) return;
        if (!job->error.isEmpty()) { failed(manual); return; }
        try {
            Receipt receipt = verifyEnvelope(job->data, config_);
            store_.mutate([&](UpdateState &s) { s.checkpoint = acceptCheckpoint(receipt, s.checkpoint); });
            if (!safe(manual) || generation != generation_) return;
            if (receipt.availability == "available") present(receipt, manual);
            else if (manual) status(receipt.availability == "incompatible" ? text("暂时没有适合此系统和处理器的新版本。", "No newer version is compatible with this system and processor.") : text("当前已是最新浩科版，或暂无可用更新。", "This is the current Haoke version, or no update is available."));
        } catch (...) { failed(manual); }
    }
    void check(bool manual) {
        if (!valid_) { if (manual) status(text("本版本的更新配置不可用。", "Update configuration is unavailable in this build.")); return; }
        if (!safe(manual)) return;
        if (flight_) { manual_ = manual_ || manual; return; }
        // A cancelled synchronous WinHTTP call may still be unwinding. Wait
        // for its finished signal instead of stacking replacement threads.
        for (int i = workers_.size() - 1; i >= 0; --i) if (!workers_.at(i)) workers_.removeAt(i);
        for (const QPointer<FetchThread> &owned : workers_) if (owned && owned->isRunning()) {
            pendingCheck_ = true; pendingManual_ = pendingManual_ || manual; return;
        }
        retry_.stop(); settle_.stop();
        try {
            const qint64 now = QDateTime::currentMSecsSinceEpoch(); qint64 defer = 0;
            store_.mutate([&](UpdateState &s) {
                const qint64 elapsed = now - s.lastCheckAt;
                if (!manual && elapsed >= 0 && elapsed < 60000) defer = 60000 - elapsed;
                else s.lastCheckAt = now;
            });
            if (defer > 0) { scheduleRetry(static_cast<int>(defer)); return; }
            manual_ = manual; const quint64 generation = generation_;
            FetchThread *job = new FetchThread(config_); flight_ = job;
            for (int i = workers_.size() - 1; i >= 0; --i) if (!workers_.at(i)) workers_.removeAt(i);
            workers_.append(job);
            QObject::connect(job, &QThread::finished, this, [this, job, generation]() {
                QPointer<Controller> alive(this);
                completed(job, generation);
                if (!alive) return;
                if (pendingCheck_ && !closing_) {
                    const bool manual = pendingManual_; const quint64 currentGeneration = generation_;
                    pendingCheck_ = false; pendingManual_ = false;
                    QTimer::singleShot(0, this, [this, manual, currentGeneration]() {
                        if (currentGeneration == generation_ && safe(manual)) check(manual);
                    });
                }
            }, Qt::QueuedConnection);
            timeout_.start(12000); job->start();
        } catch (...) { failed(manual); }
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == window_) {
            if (event->type() == QEvent::Close) { eligible_ = false; cancel(); QTimer::singleShot(0, this, [this]() { refresh(); }); }
            else if (!presenting_ && (event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Show || event->type() == QEvent::Hide || event->type() == QEvent::WindowStateChange)) QTimer::singleShot(0, this, [this]() { refresh(); });
        } else if (watched == qApp && event->type() == QEvent::ApplicationStateChange) QTimer::singleShot(0, this, [this]() { refresh(); });
        return QObject::eventFilter(watched, event);
    }
public:
    static QString defaultStateDirectory() {
        const QString portable = QDir(QCoreApplication::applicationDirPath()).filePath("portable");
        if (QFileInfo(portable).isDir()) return QDir(portable).filePath("vlc");
        return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("haoke-updates");
    }
    explicit Controller(QWidget *window, const Config &config, const std::function<bool()> &idle, const QString &stateDirectory = QString())
        : QObject(window), window_(window), config_(config), idle_(idle), store_(stateDirectory.isEmpty() ? defaultStateDirectory() : stateDirectory, config) {
        try { validateConfig(config_); require(static_cast<bool>(idle_), "INVALID_CONFIGURATION"); } catch (...) { valid_ = false; }
        settle_.setSingleShot(true); timeout_.setSingleShot(true); retry_.setSingleShot(true);
        QObject::connect(&settle_, &QTimer::timeout, this, [this]() { check(false); });
        QObject::connect(&retry_, &QTimer::timeout, this, [this]() { if (safe()) check(false); });
        QObject::connect(&timeout_, &QTimer::timeout, this, [this]() {
            const bool manual = manual_; cancel(); if (safe(manual)) failed(manual);
        });
        if (window_) window_->installEventFilter(this);
        if (qApp) qApp->installEventFilter(this);
        QTimer::singleShot(1500, this, [this]() { refresh(); });
    }
    ~Controller() override {
        closing_ = true; cancel(); if (qApp) qApp->removeEventFilter(this);
        // VLC unloads its Qt plugin after destroying MainInterface. No worker
        // may retain an instruction pointer in that module at this boundary.
        // Cancel every owned request first; a cancelled prior flight is still
        // owned until QThread::finished, not forgotten when flight_ is cleared.
        for (const QPointer<FetchThread> &job : workers_) if (job) job->cancel();
        for (const QPointer<FetchThread> &job : workers_) if (job) {
            job->wait(); // Never terminate a thread or unload while it executes.
            delete job;
        }
    }
    void manualCheck() {
        // QAction::triggered may run before the native QMenu closes. A single
        // queued dispatch preserves the click without treating that menu as a
        // permanent unsafe route or starting a background retry.
        QTimer::singleShot(0, this, [this]() {
            if (foreground() && !QApplication::activeModalWidget()) check(true);
        });
    }
    void refresh() {
        if (closing_) return;
        if (presenting_) {
            if ((!dialogManual_ && !idle_()) || QApplication::applicationState() != Qt::ApplicationActive) {
                cancel(); if (dialog_) dialog_->reject();
            }
            return;
        }
        const bool front = foreground(), next = safe();
        if (!front && foreground_) { retries_ = 0; }
        foreground_ = front;
        if (!next) {
            eligible_ = false;
            // Explicit manual requests may complete while media is loaded;
            // automatic requests are always invalidated by the playback path.
            if (flight_ && manual_ && safe(true)) return;
            cancel(); return;
        }
        if (!eligible_) { eligible_ = true; retries_ = 0; settle_.start(750); }
    }
};
} // namespace HaokeUpdates
#endif
