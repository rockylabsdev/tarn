#pragma once
// Runs outside programs (grim, hyprctl, notify-send, ...), found on PATH.
// Every outside command in Tarn goes through here, which is also why the
// tests can fake them all with stub executables.
#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <functional>

namespace tarn {

class Daemon;

struct ProcessResult {
    bool started = false;   // false: the program was not found or would not start
    bool finished = false;  // false: it timed out and was killed
    int exitCode = -1;
    QByteArray out;
    QByteArray err;
    bool succeeded() const { return started && finished && exitCode == 0; }
    // "grim exited with 1: <stderr>" style text for notifications.
    QString describe(const QString &program) const;
};

class ProcessRunner : public QObject {
    Q_OBJECT
public:
    explicit ProcessRunner(Daemon &) {}
    ~ProcessRunner() override;
    // Blocking. Fine for the tens-of-milliseconds commands of a capture; a
    // long job (recording, encoding) must use its own async QProcess.
    ProcessResult run(const QString &program, const QStringList &args, const QByteArray &input = {},
                      int timeoutMs = 5000);

    // Hands a file or folder to the desktop's default app (`xdg-open`), detached: the
    // daemon does not wait for the app. False sets *error to a sentence that names the
    // problem and the path (`what` is "file" or "folder"), ready for a notification.
    bool openWithDefaultApp(const QString &path, const QString &what, QString *error);

    // The same job without blocking the daemon: for programs that can take long
    // (Tesseract, ffprobe and ffmpeg on a big file). `done` runs later on the
    // event loop with the same ProcessResult run() would give, unless `context`
    // is destroyed first (then it never runs). The process is killed at timeoutMs.
    void runAsync(const QString &program, const QStringList &args, const QByteArray &input, int timeoutMs,
                  QObject *context, std::function<void(const ProcessResult &)> done);
};

} // namespace tarn
