#include "doctor.h"

#include "daemon/daemon.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <cstdio>

namespace tarn {
namespace {

// Every outside call is bounded, and a child that outlives its time is killed (SIGKILL:
// the recorder's `--info` ignores SIGTERM while it waits for a portal). Each call has its
// own limit, and the whole run has one budget on top: calls that hang one after another
// must not add up, so a call gets what is left of the budget and a check that finds none
// left says it couldn't tell.
constexpr int kCallMs = 3000;
constexpr int kTotalMs = 5000;   // the whole run, the encoder probe included
constexpr int kDaemonMs = 2000;  // connect, send and read the daemon's reply, together
constexpr int kKillWaitMs = 1000; // after SIGKILL, for the child to be reaped

// Set when the run starts; `run` and `checkDaemon` draw on it.
QDeadlineTimer g_budget;

int budgetLeftMs() { return int(qMax<qint64>(0, g_budget.remainingTime())); }

// Tarn's own file describes each bind it adds with this prefix. A Lua bind shows in
// `hyprctl binds -j` with an opaque `arg`, not its command line, but its description
// is reported as written, and it stays when the user changes the key.
const QLatin1String kBindDescriptionPrefix("Tarn:");

struct Result {
    bool started = false;
    bool finished = false;
    int exitCode = -1;
    QByteArray out;
    bool ok() const { return started && finished && exitCode == 0; }
};

Result run(const QString &program, const QStringList &args, int timeoutMs = kCallMs) {
    Result r;
    const QDeadlineTimer call(qMin(timeoutMs, budgetLeftMs()));
    if (call.hasExpired())
        return r;
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(qMax<qint64>(1, call.remainingTime())))
        return r;
    r.started = true;
    p.closeWriteChannel();
    if (!p.waitForFinished(qMax<qint64>(1, call.remainingTime()))) {
        p.kill();
        p.waitForFinished(kKillWaitMs);
        return r;
    }
    r.finished = true;
    r.exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    r.out = p.readAllStandardOutput();
    return r;
}

bool onPath(const QString &program) { return !QStandardPaths::findExecutable(program).isEmpty(); }

class Report {
public:
    void good(const QString &text) { line(QStringLiteral("✓"), text); }
    void note(const QString &text) { line(QStringLiteral("!"), text); }
    void fail(const QString &text) {
        line(QStringLiteral("✗"), text);
        ++m_failures;
    }
    void detail(const QString &text) { std::printf("  %s\n", qPrintable(text)); }
    int failures() const { return m_failures; }

private:
    void line(const QString &mark, const QString &text) { std::printf("%s %s\n", qPrintable(mark), qPrintable(text)); }
    int m_failures = 0;
};

// A tool Tarn cannot work without: ✓ or ✗ with the pacman line. `package` is the
// program's name unless the package is called something else.
void requiredTool(Report &r, const QString &program, const QString &needed, QString package = {}) {
    if (package.isEmpty())
        package = program;
    if (onPath(program))
        r.good(QStringLiteral("%1: found").arg(program));
    else
        r.fail(QStringLiteral("%1: not found. %2 need it. Install: pacman -S %3").arg(program, needed, package));
}

// A tool that unlocks a feature: ✓ naming what it unlocks, or ! naming what is off.
bool optionalTool(Report &r, const QString &program, const QString &package, const QString &unlocks) {
    if (onPath(program)) {
        r.good(QStringLiteral("%1: found (%2)").arg(program, unlocks));
        return true;
    }
    r.note(QStringLiteral("%1: not found, so %2 is off. Install: pacman -S %3").arg(program, unlocks, package));
    return false;
}

// True when Hyprland answers; the checks that ask it things run only then.
bool checkHyprland(Report &r) {
    if (!onPath(QStringLiteral("hyprctl"))) {
        r.fail(QStringLiteral("Hyprland: hyprctl not found. Install: pacman -S hyprland"));
    } else {
        const Result monitors = run(QStringLiteral("hyprctl"), {QStringLiteral("monitors"), QStringLiteral("-j")});
        if (monitors.ok()) {
            r.good(QStringLiteral("Hyprland: running"));
            return true;
        }
        if (monitors.started && !monitors.finished)
            r.fail(QStringLiteral("Hyprland: couldn't tell, hyprctl did not answer in time."));
        else
            r.fail(QStringLiteral("Hyprland: not running. Tarn needs it; start a Hyprland session and run `tarn doctor` there."));
    }
    r.detail(QStringLiteral("Skipped, they need hyprctl: Tarn's file, cursor:no_hardware_cursors."));
    return false;
}

void checkTarnFile(Report &r) {
    const Result binds = run(QStringLiteral("hyprctl"), {QStringLiteral("binds"), QStringLiteral("-j")});
    const QJsonArray all = QJsonDocument::fromJson(binds.out).array();
    int tarn = 0;
    for (const QJsonValue &bind : all)
        if (bind.toObject().value(QStringLiteral("description")).toString().startsWith(kBindDescriptionPrefix))
            ++tarn;
    if (!binds.ok() || all.isEmpty()) {
        r.fail(QStringLiteral("Tarn's file: couldn't read Hyprland's binds, so can't tell whether it is loaded."));
    } else if (tarn > 0) {
        r.good(QStringLiteral("Tarn's file: loaded (%1 binds described \"Tarn: …\"; a changed key still counts)").arg(tarn));
        r.detail(QStringLiteral("Its window rules and autostart can't be read from Hyprland, so they are not checked."));
    } else {
        r.fail(QStringLiteral("Tarn's file: not loaded. No bind in `hyprctl binds` is described \"Tarn: …\". "
                              "Copy tarn.lua into ~/.config/hypr/config/ and add require(\"config.tarn\"); "
                              "if it is copied and you removed its bind descriptions, this check can't see it."));
    }
}

void checkDaemon(Report &r) {
    // One deadline for the whole exchange: a daemon that sends bytes but never a line
    // must not keep the loop going.
    const QDeadlineTimer deadline(qMin(kDaemonMs, budgetLeftMs()));
    const auto left = [&] { return int(qMax<qint64>(1, deadline.remainingTime())); };
    QLocalSocket socket;
    socket.connectToServer(Daemon::controlSocketPath());
    bool answered = false;
    if (socket.waitForConnected(left())) {
        // Any reply shows the daemon is serving; `ping` is not a command, so it does nothing.
        socket.write("{\"args\":[\"ping\"]}\n");
        socket.waitForBytesWritten(left());
        while (!socket.canReadLine() && !deadline.hasExpired() && socket.waitForReadyRead(left())) {
        }
        answered = socket.canReadLine();
    }
    if (answered)
        r.good(QStringLiteral("daemon: answering"));
    else
        r.fail(QStringLiteral("daemon: not answering. Start it with `tarn daemon` (tarn.lua does this when Hyprland starts)."));
}

void checkTesseract(Report &r) {
    if (!onPath(QStringLiteral("tesseract"))) {
        r.note(QStringLiteral("tesseract: not found, so Text capture is off. Install: pacman -S tesseract tesseract-data-eng"));
        return;
    }
    const Result listed = run(QStringLiteral("tesseract"), {QStringLiteral("--list-langs")});
    QStringList languages;
    const QStringList lines = QString::fromUtf8(listed.out).split('\n', Qt::SkipEmptyParts);
    for (int i = 1; i < lines.size(); ++i) // the first line is the header
        if (lines.at(i).trimmed() != QLatin1String("osd"))
            languages << lines.at(i).trimmed();
    if (!listed.ok())
        r.note(QStringLiteral("tesseract: found, but couldn't list its languages (Text capture)"));
    else if (languages.isEmpty())
        r.note(QStringLiteral("tesseract: found, but no language data is installed, so Text capture reads nothing. Install: pacman -S tesseract-data-eng"));
    else
        r.good(QStringLiteral("tesseract: found (Text capture; languages: %1)").arg(languages.join(QStringLiteral(", "))));
}

void checkPointer(Report &r) {
    const Result option = run(QStringLiteral("hyprctl"), {QStringLiteral("getoption"), QStringLiteral("cursor:no_hardware_cursors"), QStringLiteral("-j")});
    const QJsonValue value = QJsonDocument::fromJson(option.out).object().value(QStringLiteral("int"));
    if (!option.ok() || !value.isDouble()) {
        r.note(QStringLiteral("cursor:no_hardware_cursors: couldn't read it (information only)"));
    } else if (value.toInt() == 0) {
        r.good(QStringLiteral("cursor:no_hardware_cursors = 0: hardware pointer, so captures leave the pointer out (information only)"));
    } else {
        r.note(QStringLiteral("cursor:no_hardware_cursors = %1: software pointer, so the pointer shows in every capture (information only)").arg(value.toInt()));
    }
}

// Whether the encoder list has ended: the section after it has begun.
bool codecListEnded(const QByteArray &output) {
    const qsizetype list = output.indexOf("section=video_codecs\n");
    return list >= 0 && output.indexOf("section=", list + 1) >= 0;
}

// Reads the probe up to the end of the encoder list, then stops it: what follows the list
// is the slow portal step, which doctor has no use for. Takes what the budget has left.
QByteArray readCodecList(QProcess &probe) {
    QByteArray output;
    while (true) {
        output += probe.readAllStandardOutput();
        if (codecListEnded(output) || probe.state() == QProcess::NotRunning || budgetLeftMs() == 0)
            break;
        probe.waitForReadyRead(budgetLeftMs());
    }
    output += probe.readAllStandardOutput();
    if (probe.state() != QProcess::NotRunning) {
        probe.kill();
        probe.waitForFinished(kKillWaitMs);
    }
    return output;
}

// `gpu-screen-recorder --info` lists the encoders under `section=video_codecs`: `h264`
// when the GPU encodes it, `h264_software` when the CPU can.
void reportEncoder(Report &r, QProcess &probe) {
    const QByteArray output = readCodecList(probe);
    QStringList codecs;
    bool inCodecs = false;
    if (codecListEnded(output)) {
        for (const QString &line : QString::fromUtf8(output).split('\n')) {
            const QString text = line.trimmed();
            if (text.startsWith(QLatin1String("section=")))
                inCodecs = text == QLatin1String("section=video_codecs");
            else if (inCodecs && !text.isEmpty())
                codecs << text;
        }
    }
    if (codecs.contains(QLatin1String("h264")))
        r.good(QStringLiteral("encoder: hardware h264"));
    else if (codecs.contains(QLatin1String("h264_software")))
        r.note(QStringLiteral("encoder: no hardware h264, so Recordings will use the CPU (expect a higher load)"));
    else
        r.note(QStringLiteral("encoder: couldn't tell (gpu-screen-recorder --info gave no usable answer within %1 s); Recordings use hardware if there is any, else the CPU").arg(kTotalMs / 1000));
}

} // namespace

int runDoctor(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    g_budget = QDeadlineTimer(kTotalMs);
    Report r;

    // This client runs its own blocking calls rather than ProcessRunner: that class is a
    // daemon service (it is built from a Daemon, which the client never creates) and
    // bounds each call alone, while doctor needs one budget across all of them.

    // Start the recorder's probe first, so the other checks run while it works. On a pipe
    // its list sits in stdio's buffer until it exits, which takes about 25 s when a portal
    // doesn't answer: `stdbuf` (coreutils) makes each line arrive as it is printed.
    QProcess probe;
    const bool recorder = onPath(QStringLiteral("gpu-screen-recorder"));
    if (recorder && onPath(QStringLiteral("stdbuf")))
        probe.start(QStringLiteral("stdbuf"), {QStringLiteral("-oL"), QStringLiteral("gpu-screen-recorder"), QStringLiteral("--info")});
    else if (recorder)
        probe.start(QStringLiteral("gpu-screen-recorder"), {QStringLiteral("--info")});

    std::printf("Required\n");
    const bool hyprland = checkHyprland(r);
    requiredTool(r, QStringLiteral("grim"), QStringLiteral("Screenshots"));
    requiredTool(r, QStringLiteral("gpu-screen-recorder"), QStringLiteral("Recordings"));
    if (hyprland)
        checkTarnFile(r);
    checkDaemon(r);

    std::printf("Optional\n");
    optionalTool(r, QStringLiteral("gifski"), QStringLiteral("gifski"), QStringLiteral("GIF Recordings"));
    optionalTool(r, QStringLiteral("ffmpeg"), QStringLiteral("ffmpeg"), QStringLiteral("GIF Recordings and Trim"));
    checkTesseract(r);
    optionalTool(r, QStringLiteral("ZXingReader"), QStringLiteral("zxing-cpp"), QStringLiteral("QR codes"));
    if (hyprland)
        checkPointer(r);

    std::printf("Encoder\n");
    if (recorder) {
        reportEncoder(r, probe);
    } else {
        r.note(QStringLiteral("encoder: couldn't tell (gpu-screen-recorder is not installed)"));
    }

    if (r.failures() == 0)
        std::printf("\nEverything required is in place.\n");
    else
        std::printf("\n%d required %s failed.\n", r.failures(), r.failures() == 1 ? "check" : "checks");
    return r.failures() == 0 ? 0 : 1;
}

} // namespace tarn
