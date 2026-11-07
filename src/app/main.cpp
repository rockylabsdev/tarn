// tarn daemon           run for the session; owns all state and every surface
// tarn doctor           check the setup, with no daemon needed (doctor.cpp)
// tarn capture fullscreen   (and every other command): one request to the daemon, then exit
#include "countdown_surface.h"
#include "daemon/control_server.h"
#include "doctor.h"
#include "daemon/daemon.h"
#include "editor/editor.h"
#include "editor_windows.h"
#include "history_window/history_window.h"
#include "history_windows.h"
#include "overlay/overlay.h"
#include "overlay_surface.h"
#include "pin/pins.h"
#include "pin_windows.h"
#include "palette/palette.h"
#include "recording/click_ripples.h"
#include "recording/recording_session.h"
#include "recording_surface.h"
#include "ripple_surface.h"
#include "thumbnails/thumbnail_stack.h"
#include "timer/countdown.h"
#include "thumbnail_surface.h"
#include "trim/trim.h"
#include "trim_windows.h"
#include "version.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <cstdio>

static int runClient(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    QLocalSocket socket;
    socket.connectToServer(tarn::Daemon::controlSocketPath());
    if (!socket.waitForConnected(1000)) {
        std::fprintf(stderr, "tarn: no daemon is running. Start it with `tarn daemon` (tarn.lua does this at login). Run `tarn doctor` for a full check.\n");
        return 2;
    }
    socket.write(QJsonDocument(QJsonObject{{"args", QJsonArray::fromStringList(args)}}).toJson(QJsonDocument::Compact) + '\n');
    socket.waitForBytesWritten(1000);
    while (!socket.canReadLine() && socket.waitForReadyRead(15000)) {
    }
    if (!socket.canReadLine()) {
        std::fprintf(stderr, "tarn: the daemon did not answer.\n");
        return 2;
    }
    const auto reply = QJsonDocument::fromJson(socket.readLine()).object();
    if (reply.value("ok").toBool())
        return 0;
    std::fprintf(stderr, "tarn: %s\n", qPrintable(reply.value("error").toString()));
    return 1;
}

static int runDaemon(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    // A live daemon already answers on the socket: don't steal it.
    {
        QLocalSocket probe;
        probe.connectToServer(tarn::Daemon::controlSocketPath());
        if (probe.waitForConnected(200)) {
            std::fprintf(stderr, "tarn: a daemon is already running.\n");
            return 1;
        }
    }

    tarn::Daemon daemon;
    tarn::ControlServer server(daemon);
    const QString problem = server.listen(tarn::Daemon::controlSocketPath());
    if (!problem.isEmpty()) {
        std::fprintf(stderr, "tarn: cannot listen on %s: %s\n", qPrintable(tarn::Daemon::controlSocketPath()), qPrintable(problem));
        return 1;
    }
    tarn::ThumbnailSurface thumbnails(daemon.service<tarn::ThumbnailStack>(), daemon.service<tarn::Palette>());
    tarn::OverlaySurface overlay(daemon.service<tarn::Overlay>(), daemon.service<tarn::Palette>());
    tarn::CountdownSurface countdown(daemon.service<tarn::Countdown>(), daemon.service<tarn::Palette>());
    tarn::RecordingSurface recording(daemon.service<tarn::RecordingSession>(), daemon.service<tarn::Palette>());
    tarn::RippleSurface ripples(daemon.service<tarn::ClickRipples>(), daemon.service<tarn::RecordingSession>(), daemon.service<tarn::Palette>());
    overlay.warmUp(); // the first real open is then fast
    tarn::EditorWindows editorWindows(daemon.service<tarn::Editor>(), daemon.service<tarn::Palette>());
    tarn::PinWindows pinWindows(daemon.service<tarn::Pins>(), daemon.service<tarn::Palette>());
    tarn::HistoryWindows historyWindows(daemon.service<tarn::HistoryWindow>(), daemon.service<tarn::Palette>());
    tarn::TrimWindows trimWindows(daemon.service<tarn::Trim>(), daemon.service<tarn::Palette>());
    std::fprintf(stderr, "tarn: daemon listening on %s\n", qPrintable(tarn::Daemon::controlSocketPath()));
    return app.exec();
}

int main(int argc, char **argv) {
    if (argc > 1 && QByteArrayView(argv[1]) == "daemon")
        return runDaemon(argc, argv);
    // Checks the setup in the client, so it works with no daemon. It blocks only this process.
    if (argc > 1 && QByteArrayView(argv[1]) == "doctor")
        return tarn::runDoctor(argc, argv);
    if (argc > 1 && QByteArrayView(argv[1]) == "--version") {
        std::printf("tarn " TARN_VERSION "\n");
        return 0;
    }
    if (argc < 2) {
        std::fprintf(stderr, "usage: tarn daemon | tarn capture area|fullscreen|repeat|timer|record|text | tarn settings | tarn history | tarn restore | tarn quit | tarn doctor | tarn --version\n");
        return 2;
    }
    return runClient(argc, argv);
}
