#pragma once
// The test harness. One TestEnv per test (create it in init(), destroy it in
// cleanup()) gives a test:
//   - a scratch directory with HOME and every XDG_* folder inside it
//     (XDG_RUNTIME_DIR included, mode 0700), so nothing touches the real home;
//   - PATH set to the stub executables in tests/support/stubs plus a short list
//     of real tools (safe-tools.txt): a program with no stub is "not found";
//   - a Daemon whose clock the test controls, driven through request()
//     exactly as a `tarn` command would.
// Qt itself runs offscreen: tarn_add_test compiles tests/support/force_offscreen.cpp
// into every test, so even a test binary run by hand never reaches the desktop.
#include "daemon/daemon.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <memory>

namespace tarn::test {

class TestEnv {
public:
    TestEnv();
    ~TestEnv();
    TestEnv(const TestEnv &) = delete;
    TestEnv &operator=(const TestEnv &) = delete;

    Daemon &daemon() { return *m_daemon; }
    // Sends a request the way the `tarn` client does: request({"capture","fullscreen"}).
    Reply request(const QStringList &args) { return m_daemon->handle(args); }
    // Replaces the daemon with a fresh one (same scratch dir), e.g. to test start-up.
    void restartDaemon();

    // Where things are.
    QString scratch() const { return m_dir.path(); }
    QString home() const { return scratch() + "/home"; }
    QString pictures() const { return home() + "/Pictures"; }       // XDG_PICTURES_DIR
    QString screenshots() const { return pictures() + "/Screenshots"; }
    QString videos() const { return home() + "/Videos"; }           // XDG_VIDEOS_DIR
    QString recordings() const { return videos() + "/Tarn"; }       // the Recordings folder
    QString keptRecordings() const { return scratch() + "/cache/tarn/recordings"; }
    QString runtimeDir() const { return scratch() + "/run"; }       // XDG_RUNTIME_DIR

    // The daemon's clock: it names captures and ages files by this, never by the
    // real one. A test that dates something the daemon compares uses now().
    QDateTime now() const { return m_now; }
    void setNow(const QDateTime &t) { m_now = t; }

    // Stubs. `name` is the program the stub stands in for ("grim", "wl-copy", ...).
    // Each call's arguments, in order.
    QList<QStringList> calls(const QString &stub) const;
    // What was piped into call number n (1-based) of a stub that saves stdin.
    QByteArray stdinOfCall(const QString &stub, int n) const;
    // "ok" by default; "fail" makes the stub log the call and exit 1.
    void setMode(const QString &stub, const QString &mode);
    // Overrides a canned file, e.g. setFixture("grim", "output.png", bytes).
    void setFixture(const QString &stub, const QString &file, const QByteArray &content);
    // Convenience for the pointer: where hyprctl says it is.
    void setPointer(int x, int y);

    // The bytes of the canned grim image shipped with the harness.
    static QByteArray defaultGrimPng();

    // Files directly in a folder, sorted by name (empty if it does not exist).
    static QStringList filesIn(const QString &dir);
    static QByteArray readFile(const QString &path);

private:
    QString stateDir() const { return scratch() + "/stubs"; }
    void setEnv(const char *name, const QString &value);

    QTemporaryDir m_dir;
    QHash<QByteArray, QByteArray> m_savedEnv; // value before the harness changed it; null = was unset
    QDateTime m_now;
    std::unique_ptr<Daemon> m_daemon;
};

} // namespace tarn::test
