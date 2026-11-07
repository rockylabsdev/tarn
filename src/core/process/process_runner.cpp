#include "process_runner.h"

#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <memory>

namespace tarn {

QString ProcessResult::describe(const QString &program) const {
    if (!started)
        return QStringLiteral("%1 could not be started (is it installed?). Run `tarn doctor` for a full check.").arg(program);
    if (!finished)
        return QStringLiteral("%1 did not finish in time").arg(program);
    const QString detail = QString::fromUtf8(err).trimmed();
    return QStringLiteral("%1 exited with %2%3").arg(program).arg(exitCode).arg(detail.isEmpty() ? QString() : ": " + detail);
}

bool ProcessRunner::openWithDefaultApp(const QString &path, const QString &what, QString *error) {
    if (QProcess::startDetached(QStringLiteral("xdg-open"), {path}))
        return true;
    *error = QStringLiteral("could not run xdg-open (is it installed?). The %1 is %2").arg(what, path);
    return false;
}

// Background jobs still running when the daemon goes away are dropped: their
// callbacks would find nothing left to call.
ProcessRunner::~ProcessRunner() {
    for (QProcess *process : findChildren<QProcess *>()) {
        process->disconnect();
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
}

ProcessResult ProcessRunner::run(const QString &program, const QStringList &args, const QByteArray &input, int timeoutMs) {
    ProcessResult result;
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(timeoutMs))
        return result;
    result.started = true;
    if (!input.isEmpty())
        process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        result.out = process.readAllStandardOutput();
        result.err = process.readAllStandardError();
        return result;
    }
    result.finished = true;
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    result.out = process.readAllStandardOutput();
    result.err = process.readAllStandardError();
    return result;
}

void ProcessRunner::runAsync(const QString &program, const QStringList &args, const QByteArray &input, int timeoutMs,
                             QObject *context, std::function<void(const ProcessResult &)> done) {
    auto *process = new QProcess(this);
    auto state = std::make_shared<ProcessResult>(); // filled as the process goes
    QPointer<QObject> where(context);
    auto finish = [process, state, where, done = std::move(done)] {
        if (process->state() != QProcess::NotRunning) { // the timeout
            process->kill();
            process->waitForFinished(1000);
        } else {
            state->finished = process->error() != QProcess::FailedToStart;
            state->exitCode = process->exitStatus() == QProcess::NormalExit ? process->exitCode() : -1;
        }
        state->out = process->readAllStandardOutput();
        state->err = process->readAllStandardError();
        const ProcessResult result = *state;
        process->deleteLater();
        if (where)
            done(result);
    };
    // One of: finished, could not start, or timed out. The guard makes it once.
    auto once = std::make_shared<bool>(false);
    auto run = [once, finish = std::move(finish)] {
        if (*once)
            return;
        *once = true;
        finish();
    };
    connect(process, &QProcess::started, process, [state] { state->started = true; });
    connect(process, &QProcess::finished, process, [run](int, QProcess::ExitStatus) { run(); });
    connect(process, &QProcess::errorOccurred, process, [process, run](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            run();
    });
    QTimer::singleShot(timeoutMs, process, [run, process] {
        if (process->state() != QProcess::NotRunning)
            run();
    });
    process->start(program, args);
    if (!input.isEmpty())
        process->write(input);
    process->closeWriteChannel();
}

} // namespace tarn
