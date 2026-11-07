// Every image capture knows which app and window it came from. Before the
// Overlay opens the Hyprland bridge takes one snapshot of the windows
// (`hyprctl clients`, `monitors`, `activewindow`); area, full-screen and Timer
// captures write `app` and `title` into their Description from it. The clients
// below are fixtures for the `hyprctl` stub.
#include "test_env.h"

#include "history/history.h"
#include "overlay/overlay.h"
#include "timer/countdown.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using tarn::Countdown;
using tarn::History;
using tarn::Overlay;
using tarn::OverlayModel;
using tarn::test::TestEnv;

namespace {
// One 128 x 80 monitor at scale 2 (64 x 40 logical pixels) showing workspace 1.
const QByteArray kMonitor =
    R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0,"activeWorkspace":{"id":1},"specialWorkspace":{"id":0}}])";

QJsonObject client(const QString &app, const QString &title, int x, int y, int w, int h, int workspace = 1, bool floating = false) {
    return {{"address", "0x" + app}, {"mapped", true}, {"hidden", false}, {"workspace", QJsonObject{{"id", workspace}}},
            {"at", QJsonArray{x, y}}, {"size", QJsonArray{w, h}}, {"fullscreen", 0}, {"floating", floating},
            {"class", app}, {"title", title}, {"focusHistoryID", 0}};
}
// The scratchpad (Hyprland's special workspace) as the user's real desk check showed
// it (2026-11-04): its windows sit on workspace -99 "special:special", and the
// monitor reports that id in `specialWorkspace` only while it is shown (0 when not).
// `visible` is true for windows on hidden workspaces too, so it is never read.
QByteArray monitorShowing(int special) {
    return QStringLiteral(R"([{"id":1,"name":"DP-2","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0,)"
                          R"("activeWorkspace":{"id":2,"name":"2"},"specialWorkspace":{"id":%1,"name":"%2"}}])")
        .arg(special)
        .arg(special ? "special:special" : "")
        .toUtf8();
}
// Workspace 2 holds Orca on the left and a terminal on the right; a full-size terminal
// sits in the scratchpad, listed last (as Hyprland listed it).
QJsonArray deskWithScratchpad() {
    return {client("orca", "Orca", 0, 2, 31, 38, 2), client("kitty", "~ - fish", 33, 2, 31, 38, 2),
            client("scratch", "~: hyprctl clients -j - hyprctl", 0, 2, 64, 38, -99)};
}
} // namespace

class SnapshotTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    History &history() { return env->daemon().service<History>(); }
    // `focused` is the class of the client `hyprctl activewindow` answers with.
    void setClients(const QJsonArray &clients, const QString &focused = {}) {
        env->setFixture("hyprctl", "clients.json", QJsonDocument(clients).toJson());
        QByteArray active = "{}";
        for (const auto &c : clients)
            if (c.toObject().value("class").toString() == focused)
                active = QJsonDocument(c.toObject()).toJson();
        env->setFixture("hyprctl", "activewindow.json", active);
    }
    void dragArea(const QRect &r) {
        model().pointerPressed(r.left(), r.top(), Qt::LeftButton);
        model().pointerMoved(r.right() + 1, r.bottom() + 1);
        model().pointerReleased(r.right() + 1, r.bottom() + 1, Qt::LeftButton);
    }
    // The newest Description in History.
    QJsonObject description() {
        history().flush();
        const QStringList ids = history().ids();
        return ids.isEmpty() ? QJsonObject{} : QJsonDocument::fromJson(TestEnv::readFile(history().descriptionPath(ids.last()))).object();
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setPointer(10, 10);
    }
    void cleanup() { env.reset(); }

    void anAreaIsAttributedToTheWindowItOverlapsMostNotTheFocusedOne() {
        setClients({client("left", "Left title", 0, 0, 30, 40), client("right", "Right title", 32, 0, 30, 40)}, "right");
        QVERIFY(env->request({"capture", "area"}).ok);
        dragArea(QRect(2, 5, 40, 20)); // 28 columns of it over "left", 10 over "right"
        const QJsonObject d = description();
        QCOMPARE(d.value("kind").toString(), QString("area"));
        QCOMPARE(d.value("app").toString(), QString("left"));
        QCOMPARE(d.value("title").toString(), QString("Left title"));
    }

    void aFloatingWindowCountsAndHidesWhatItCoversOfTheWindowBeneath() {
        // Tiled fills the monitor; the floating one is listed later, so it is on top.
        setClients({client("tiled", "T", 0, 0, 64, 40), client("float", "F", 10, 5, 20, 20, 1, true)});
        QVERIFY(env->request({"capture", "area"}).ok);
        dragArea(QRect(10, 5, 20, 20)); // exactly the floating window
        QCOMPARE(description().value("app").toString(), QString("float"));
    }

    void aMostlyTiledAreaStaysWithTheTiledWindowEvenWithAFloatingOneOnIt() {
        setClients({client("tiled", "T", 0, 0, 64, 40), client("float", "F", 10, 5, 20, 20, 1, true)});
        QVERIFY(env->request({"capture", "area"}).ok);
        dragArea(QRect(0, 0, 60, 38)); // the floating window covers 400 of 2280
        QCOMPARE(description().value("app").toString(), QString("tiled"));
    }

    void aFloatingWindowIsAboveTiledOnesWhateverTheListOrder() {
        setClients({client("float", "F", 10, 5, 20, 20, 1, true), client("tiled", "T", 0, 0, 64, 40)});
        QVERIFY(env->request({"capture", "area"}).ok);
        dragArea(QRect(10, 5, 20, 20));
        QCOMPARE(description().value("app").toString(), QString("float"));
    }

    void windowsOnOtherWorkspacesAreNeverPicked() {
        setClients({client("elsewhere", "E", 0, 0, 64, 40, 2), client("here", "H", 0, 0, 10, 40, 1)});
        QVERIFY(env->request({"capture", "area"}).ok);
        dragArea(QRect(0, 0, 40, 30)); // "elsewhere" fills it, but "here" is the only one on screen
        QCOMPARE(description().value("app").toString(), QString("here"));
    }

    void aWindowOnlyOnAnotherWorkspaceMeansBareDesktopEvenIfFocused() {
        setClients({client("elsewhere", "E", 0, 0, 64, 40, 2)}, "elsewhere");
        QVERIFY(env->request({"capture", "area"}).ok);
        dragArea(QRect(5, 5, 20, 20));
        QVERIFY(!description().contains("app"));
        QVERIFY(!description().contains("title"));
    }

    void fullScreenWithTheFocusedWindowOnAnotherWorkspaceLeavesThemOut() {
        setClients({client("elsewhere", "E", 0, 0, 64, 40, 2)}, "elsewhere");
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QVERIFY(!description().contains("app"));
    }

    void anAreaOverBareDesktopHasNeitherAppNorTitle() {
        setClients({client("far", "F", 40, 20, 20, 15)}, "far");
        QVERIFY(env->request({"capture", "area"}).ok);
        dragArea(QRect(2, 2, 20, 10));
        const QJsonObject d = description();
        QCOMPARE(d.value("kind").toString(), QString("area"));
        QVERIFY(!d.contains("app"));
        QVERIFY(!d.contains("title"));
    }

    void fInTheOverlayRecordsAFullScreenShot() {
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_F);
        QCOMPARE(description().value("kind").toString(), QString("fullscreen"));
    }

    void fullScreenUsesTheFocusedWindow() {
        setClients({client("big", "Big", 0, 0, 64, 40), client("focused", "Focus me", 40, 20, 20, 15)}, "focused");
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        const QJsonObject d = description();
        QCOMPARE(d.value("kind").toString(), QString("fullscreen"));
        QCOMPARE(d.value("app").toString(), QString("focused"));
        QCOMPARE(d.value("title").toString(), QString("Focus me"));
    }

    void fullScreenWithNothingFocusedLeavesThemOut() {
        setClients({client("a", "A", 0, 0, 30, 30)});
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QVERIFY(!description().contains("app"));
    }

    void aTimerAreaIsAttributedFromTheSnapshotTakenBeforeTheOverlayOpened() {
        const QDateTime now(QDate(2026, 11, 3), QTime(10, 0, 0));
        env->setNow(now);
        setClients({client("left", "Left title", 0, 0, 30, 40), client("right", "Right title", 32, 0, 30, 40)}, "right");
        QVERIFY(env->request({"capture", "timer"}).ok);
        dragArea(QRect(2, 5, 20, 20));
        setClients({client("other", "Opened during the countdown", 0, 0, 64, 40)}, "other"); // too late to count
        env->setNow(now.addSecs(10));
        env->daemon().service<Countdown>().tick();
        const QJsonObject d = description();
        QCOMPARE(d.value("kind").toString(), QString("area"));
        QCOMPARE(d.value("app").toString(), QString("left"));
        QCOMPARE(d.value("title").toString(), QString("Left title"));
    }

    void aTimerFullScreenUsesTheFocusedWindow() {
        const QDateTime now(QDate(2026, 11, 3), QTime(10, 0, 0));
        env->setNow(now);
        setClients({client("left", "L", 0, 0, 64, 40), client("right", "Focus me", 32, 0, 30, 40)}, "right");
        QVERIFY(env->request({"capture", "timer"}).ok);
        model().keyPressed(Qt::Key_F);
        env->setNow(now.addSecs(10));
        env->daemon().service<Countdown>().tick();
        QCOMPARE(description().value("kind").toString(), QString("fullscreen"));
        QCOMPARE(description().value("app").toString(), QString("right"));
    }

    void aWindowCaptureStillWritesItsWindowsAppAndTitle() {
        setClients({client("firefox", "Docs", 4, 4, 30, 30)});
        QVERIFY(env->request({"capture", "area"}).ok);
        model().pointerMoved(10, 10);
        model().pointerPressed(10, 10, Qt::LeftButton);
        model().pointerReleased(10, 10, Qt::LeftButton);
        const QJsonObject d = description();
        QCOMPARE(d.value("kind").toString(), QString("window"));
        QCOMPARE(d.value("app").toString(), QString("firefox"));
        QCOMPARE(d.value("title").toString(), QString("Docs"));
    }

    void aV1DescriptionLoadsAsItIs() {
        const QString dir = History::dir();
        QDir().mkpath(dir);
        const QByteArray v1 =
            R"({"captureTime":"2026-10-30T12:00:00","kind":"window","position":{"x":0,"y":0,"width":9,"height":9},"savedPaths":[]})";
        QFile png(dir + "/2026-10-30_12-00-00.png");
        QVERIFY(png.open(QIODevice::WriteOnly));
        png.write("x");
        png.close();
        QFile json(dir + "/2026-10-30_12-00-00.json");
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(v1);
        json.close();

        QCOMPARE(history().ids(), QStringList{"2026-10-30_12-00-00"});
        history().sweep();
        QCOMPARE(history().ids(), QStringList{"2026-10-30_12-00-00"}); // read and kept: inside the retention
        // A new capture beside it adds fields without touching the old file.
        setClients({client("a", "A", 0, 0, 64, 40)}, "a");
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        history().flush();
        QCOMPARE(TestEnv::readFile(dir + "/2026-10-30_12-00-00.json"), v1);
        QCOMPARE(history().ids().size(), 2);
    }
    void anOpenScratchpadWindowIsOnTopOfTheWorkspaceBeneath() {
        env->setFixture("hyprctl", "monitors.json", monitorShowing(-99));
        setClients(deskWithScratchpad());
        QVERIFY(env->request({"capture", "area"}).ok);
        dragArea(QRect(2, 5, 20, 20)); // over Orca, but the scratchpad covers it
        QCOMPARE(description().value("app").toString(), QString("scratch"));
    }

    void aClosedScratchpadsWindowsAreNeverPicked() {
        env->setFixture("hyprctl", "monitors.json", monitorShowing(0));
        setClients(deskWithScratchpad());
        QVERIFY(env->request({"capture", "area"}).ok);
        dragArea(QRect(2, 5, 20, 20));
        QCOMPARE(description().value("app").toString(), QString("orca"));
    }

    void aSwallowedWindowIsNeverPicked() {
        // A terminal that launched a window is hidden while that window stands in for it.
        QJsonObject swallowed = client("kitty", "qs - qs", 0, 0, 64, 40);
        swallowed["hidden"] = true;
        setClients({swallowed, client("orca", "Orca", 0, 0, 10, 40)});
        QVERIFY(env->request({"capture", "area"}).ok);
        dragArea(QRect(0, 0, 40, 30));
        QCOMPARE(description().value("app").toString(), QString("orca"));
    }

    void aWindowOffEveryMonitorIsNeverPicked() {
        // Seen at the desk: a floating window parked at x = -3047 on a one-monitor setup.
        setClients({client("offscreen", "O", -60, 5, 40, 30, 1, true), client("here", "H", 0, 0, 10, 40)});
        QVERIFY(env->request({"capture", "area"}).ok);
        dragArea(QRect(0, 0, 40, 30));
        QCOMPARE(description().value("app").toString(), QString("here"));
    }
};


QTEST_MAIN(SnapshotTest)
#include "snapshot_test.moc"
