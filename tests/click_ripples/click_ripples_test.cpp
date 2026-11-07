// Show clicks. The Hyprland binds are added at start and removed however a
// Recording ends (stop, delete, failure, a crash at the next start); a press
// draws a Ripple only inside the area. hyprctl is the stub, which only logs
// `eval`, so nothing real is bound.
#include "test_env.h"

#include "palette/palette.h"
#include "recording/click_ripples.h"
#include "recording/record_options.h"
#include "recording/recording_session.h"
#include "ripple_surface.h"

#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>

using tarn::RecordingSession;
using tarn::test::TestEnv;

namespace {
// 256 x 160 logical monitor at scale 2, placed at (100, 50) in the layout.
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":100,"y":50,"width":512,"height":320,"scale":2.0,"transform":0}])";

QQuickItem *findItem(QQuickItem *from, const QString &objectName) {
    if (from->objectName() == objectName)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = findItem(child, objectName))
            return hit;
    return nullptr;
}
} // namespace

class ClickRipplesTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    RecordingSession &session() { return env->daemon().service<RecordingSession>(); }
    tarn::ClickRipples &ripples() { return env->daemon().service<tarn::ClickRipples>(); }
    // Every `hyprctl eval <lua>` so far, as the Lua text.
    QStringList evals() const {
        QStringList out;
        for (const auto &c : env->calls("hyprctl"))
            if (c.value(0) == QLatin1String("eval"))
                out << c.value(1);
        return out;
    }
    void startRecording() {
        // Area (20,10) 80 x 50 logical on the monitor at (100,50).
        QVERIFY(session().start(QRect(20, 10, 80, 50), QRect(100, 50, 256, 160), 2.0, "DP-1").ok);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
    }
    void stopAndWait() {
        session().stop();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->daemon().service<tarn::RecordOptions>().setShowClicks(true);
    }
    void cleanup() { env.reset(); }

    void showClicksOffBindsNothing() {
        env->daemon().service<tarn::RecordOptions>().setShowClicks(false);
        startRecording();
        stopAndWait();
        QVERIFY(evals().isEmpty());
    }

    void bindsAreAddedAtStartAsNonConsumingMouseBinds() {
        startRecording();
        QCOMPARE(evals().size(), 1);
        const QString lua = evals().first();
        for (const char *b : {"mouse:272", "mouse:273", "mouse:274"})
            QVERIFY2(lua.contains(QStringLiteral("hl.bind(\"%1\"").arg(QLatin1String(b))), qPrintable(lua));
        QVERIFY(lua.contains("non_consuming=true"));
        QVERIFY(lua.contains("hl.dsp.exec_cmd(\"tarn record click left\")"));
        QVERIFY(lua.contains("hl.dsp.exec_cmd(\"tarn record click middle\")"));
        QVERIFY(lua.contains("hl.dsp.exec_cmd(\"tarn record click right\")"));
        QVERIFY(ripples().active());
    }

    void stopRemovesTheBindsAtOnce() {
        startRecording();
        session().stop(); // SIGINT sent; the file is not finished yet
        QCOMPARE(evals().size(), 2);
        QVERIFY(evals().last().contains("hl.unbind(\"mouse:272\")"));
        QVERIFY(evals().last().contains("hl.unbind(\"mouse:273\")"));
        QVERIFY(evals().last().contains("hl.unbind(\"mouse:274\")"));
        QVERIFY(!ripples().active());
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
        QCOMPARE(evals().size(), 2); // nothing more
    }

    void stopAskedWhileStartingRemovesThemToo() {
        QVERIFY(session().start(QRect(20, 10, 80, 50), QRect(100, 50, 256, 160), 2.0, "DP-1").ok);
        session().stop();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
        QCOMPARE(evals().size(), 2);
        QVERIFY(evals().last().contains("hl.unbind"));
    }

    void deleteRemovesTheBinds() {
        startRecording();
        session().deleteRecording();
        QCOMPARE(session().state(), RecordingSession::Deleted);
        QCOMPARE(evals().size(), 2);
        QVERIFY(evals().last().contains("hl.unbind"));
    }

    void restartKeepsOneSetOfBindsAndReadsTheToggleAgain() {
        startRecording();
        session().restart();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        QCOMPARE(evals().size(), 1); // still the first set, not a second
        QVERIFY(ripples().active());
        // Turned off meanwhile: the next restart takes them out.
        env->daemon().service<tarn::RecordOptions>().setShowClicks(false);
        session().restart();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        QCOMPARE(evals().size(), 2);
        QVERIFY(evals().last().contains("hl.unbind"));
        QVERIFY(!ripples().active());
    }

    void aRecorderThatDiesRemovesTheBinds() {
        env->setMode("gpu-screen-recorder", "die");
        QVERIFY(session().start(QRect(20, 10, 80, 50), QRect(100, 50, 256, 160), 2.0, "DP-1").ok);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Idle, 5000);
        QVERIFY(evals().last().contains("hl.unbind"));
        QVERIFY(!ripples().active());
    }

    void aCrashIsCleanedUpAtTheNextStart() {
        startRecording();
        QCOMPARE(evals().size(), 1);
        env->restartDaemon(); // the old daemon never got to unbind
        QCOMPARE(evals().size(), 2);
        QVERIFY(evals().last().contains("hl.unbind(\"mouse:272\")"));
        // And only once: a second start finds nothing to clean.
        env->restartDaemon();
        QCOMPARE(evals().size(), 2);
    }

    void aCleanStartRunsNoEval() {
        env->restartDaemon();
        QVERIFY(evals().isEmpty());
    }

    void aPressInsideTheAreaDrawsARippleOutsideDoesNot() {
        startRecording();
        QSignalSpy spy(&ripples(), &tarn::ClickRipples::rippled);
        // Layout pointer (100+30, 50+20) = (30, 20) on the monitor: inside (20,10,80,50).
        env->setPointer(130, 70);
        QVERIFY(env->request({"record", "click", "left"}).ok);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().first().toPointF(), QPointF(30, 20));
        // Global (30,20) would be inside if the monitor's origin were ignored: it must not draw.
        env->setPointer(30, 20);
        QVERIFY(env->request({"record", "click", "middle"}).ok);
        // Right of the area.
        env->setPointer(100 + 200, 50 + 20);
        QVERIFY(env->request({"record", "click", "right"}).ok);
        QCOMPARE(spy.size(), 1);
    }

    void noRippleWhilePausedOrAfterTheBindsAreGone() {
        startRecording();
        QSignalSpy spy(&ripples(), &tarn::ClickRipples::rippled);
        env->setPointer(130, 70);
        session().pause();
        env->request({"record", "click", "left"});
        QCOMPARE(spy.size(), 0);
        session().resume();
        stopAndWait();
        env->request({"record", "click", "left"});
        QCOMPARE(spy.size(), 0);
    }

    void theRippleSurfaceIsOnScreenOnlyDuringTheRecording() {
        tarn::RippleSurface surface(ripples(), session(), env->daemon().service<tarn::Palette>());
        QVERIFY(!surface.visible());
        startRecording();
        QVERIFY(surface.visible());
        auto *root = surface.view().rootObject();
        QVERIFY(root);
        QVERIFY(!findItem(root, "ripple"));
        env->setPointer(130, 70);
        QVERIFY(env->request({"record", "click", "left"}).ok);
        auto *ring = findItem(root, "ripple");
        QVERIFY(ring);
        QCOMPARE(ring->width(), 48.0);
        QCOMPARE(ring->x() + 24, 30.0); // centered on the press
        QCOMPARE(ring->y() + 24, 20.0);
        QTRY_VERIFY_WITH_TIMEOUT(!findItem(root, "ripple"), 2000); // gone after ~400 ms
        stopAndWait();
        QVERIFY(!surface.visible());
    }
};

QTEST_MAIN(ClickRipplesTest)
#include "click_ripples_test.moc"
