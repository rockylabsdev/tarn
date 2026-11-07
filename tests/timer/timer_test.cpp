// The Timer. Super+Shift+Print opens the Overlay in Timer mode; an area
// (or F for the full screen) starts a Countdown of the `timer_delay` Setting;
// when it ends the LIVE screen is grabbed and lands as a Thumbnail with a
// History entry. Pressing the key again during the Countdown cancels. Time
// comes from the test clock: the tests move it and call tick(), no sleeping.
#include "test_env.h"
#include "overlay/overlay_model.h"

#include "countdown_surface.h"
#include "history/history.h"
#include "overlay/overlay.h"
#include "palette/palette.h"
#include "settings/settings.h"
#include "thumbnails/thumbnail_stack.h"
#include "timer/countdown.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>

using tarn::Countdown;
using tarn::CountdownSurface;
using tarn::Overlay;
using tarn::OverlayModel;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

namespace {
QImage gradient(int shift) {
    QImage image(128, 80, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb((x * 2 + shift) & 255, y * 3, (x ^ y) * 4));
    return image;
}

QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// One 128 x 80 monitor at scale 2: 64 x 40 logical pixels.
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0}])";

QQuickItem *findItem(QQuickItem *from, const QString &objectName) {
    if (from->objectName() == objectName)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = findItem(child, objectName))
            return hit;
    return nullptr;
}
} // namespace

class TimerTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    QDateTime m_now;

    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    Countdown &countdown() { return env->daemon().service<Countdown>(); }
    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    tarn::History &history() { return env->daemon().service<tarn::History>(); }
    QString configFile() const { return env->scratch() + "/config/tarn/config.toml"; }
    bool press() { return env->request({"capture", "timer"}).ok; }
    void writeConfig(const QByteArray &text) {
        QFile f(configFile());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
    }
    void dragArea() { // logical (10,5) to (30,25)
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerMoved(30, 25);
        model().pointerReleased(30, 25, Qt::LeftButton);
    }
    // Moves the test clock and lets the Countdown notice, with no real waiting.
    void advance(int ms) {
        m_now = m_now.addMSecs(ms);
        env->setNow(m_now);
        countdown().tick();
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setFixture("grim", "output.png", pngOf(gradient(0)));
        env->setPointer(10, 10);
        m_now = QDateTime(QDate(2026, 11, 3), QTime(10, 0, 0));
        env->setNow(m_now);
    }
    void cleanup() { env.reset(); }

    // ---- the Setting ----

    void theDelayIsASettingThatDefaultsToThreeSecondsAndChangesLive() {
        auto &settings = env->daemon().service<tarn::Settings>();
        QCOMPARE(settings.timerDelaySeconds(), 3.0);
        const QString text = QString::fromUtf8(TestEnv::readFile(configFile()));
        QVERIFY(text.contains("timer_delay = 3"));
        QVERIFY(text.left(text.indexOf("\ntimer_delay = ")).trimmed().split('\n').last().startsWith('#'));
        writeConfig("timer_delay = 5\n");
        QTRY_COMPARE(settings.timerDelaySeconds(), 5.0);
        QVERIFY(env->calls("notify-send").isEmpty());
    }

    void aBadDelayIsReportedAndTheLastGoodOneStays() {
        auto &settings = env->daemon().service<tarn::Settings>();
        writeConfig("timer_delay = \"soon\"\n");
        QTRY_COMPARE(env->calls("notify-send").size(), 1);
        QCOMPARE(settings.timerDelaySeconds(), 3.0);
    }

    // ---- starting ----

    void theKeyOpensTheOverlayInTimerModeWithNoWindowHighlight() {
        QVERIFY(press());
        QVERIFY(model().active());
        QCOMPARE(model().mode(), OverlayModel::Screenshot);
        QVERIFY(model().timer());
        model().pointerMoved(10, 10);
        QVERIFY(!model().hasHighlight());
        QVERIFY(!countdown().active());
    }

    void anAreaStartsACountdownOfTheSettingsDelayAndTakesNoShotYet() {
        QVERIFY(press());
        dragArea();
        QVERIFY(!model().active());
        QVERIFY(countdown().active());
        QCOMPARE(countdown().area(), QRect(10, 5, 20, 20));
        QCOMPARE(countdown().seconds(), 3);
        QCOMPARE(env->calls("grim").size(), 1); // only the frozen image the Overlay opened on
        QCOMPARE(stack().count(), 0);
    }

    void fStartsACountdownForTheFullScreen() {
        QVERIFY(press());
        model().keyPressed(Qt::Key_F);
        QVERIFY(countdown().active());
        QCOMPARE(countdown().area(), QRect(0, 0, 64, 40));
    }

    void fInThePlainOverlayTakesTheScreenWithNoCountdown() {
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_F);
        QVERIFY(!model().active());
        QVERIFY(!countdown().active());
        QCOMPARE(stack().count(), 1);
    }

    // ---- counting down, on the test clock ----

    void theLabelCountsDownThreeTwoOneThenShoots() {
        QVERIFY(press());
        dragArea();
        QSignalSpy changed(&countdown(), &Countdown::changed);
        advance(500);
        QCOMPARE(countdown().seconds(), 3);
        advance(600);
        QCOMPARE(countdown().seconds(), 2);
        advance(1000);
        QCOMPARE(countdown().seconds(), 1);
        QVERIFY(changed.count() >= 2);
        QCOMPARE(env->calls("grim").size(), 1);
        QCOMPARE(stack().count(), 0);
        advance(899);
        QCOMPARE(stack().count(), 0); // 2999 ms: still counting
        QVERIFY(countdown().active());
        advance(1);
        QVERIFY(!countdown().active());
        QCOMPARE(env->calls("grim").size(), 2);
        QCOMPARE(stack().count(), 1);
    }

    void theSettingsDelayIsUsedAndAZeroDelayShootsAtOnce() {
        writeConfig("timer_delay = 10\n");
        QTRY_COMPARE(env->daemon().service<tarn::Settings>().timerDelaySeconds(), 10.0);
        QVERIFY(press());
        dragArea();
        QCOMPARE(countdown().seconds(), 10);
        advance(9999);
        QVERIFY(countdown().active());
        advance(1);
        QVERIFY(!countdown().active());
        QCOMPARE(stack().count(), 1);

        writeConfig("timer_delay = 0\n");
        QTRY_COMPARE(env->daemon().service<tarn::Settings>().timerDelaySeconds(), 0.0);
        QVERIFY(press());
        dragArea();
        QVERIFY(!countdown().active());
        QCOMPARE(stack().count(), 2);
    }

    // ---- the shot ----

    void theShotIsTheLiveScreenAtTheEndNotTheFrozenOneAndLandsInHistory() {
        QVERIFY(press());
        dragArea();
        // The screen changes while the Countdown runs (a menu opens).
        env->setFixture("grim", "output.png", pngOf(gradient(100)));
        advance(3000);
        QCOMPARE(stack().count(), 1);
        const QString path = stack().save(stack().ids().first());
        QVERIFY(!path.isEmpty());
        const QImage saved(path);
        QCOMPARE(saved.size(), QSize(40, 40)); // 20 x 20 logical at scale 2
        QCOMPARE(saved.pixel(0, 0), gradient(100).pixel(20, 10));
        QCOMPARE(saved.pixel(39, 39), gradient(100).pixel(59, 49));
        QVERIFY(saved.pixel(0, 0) != gradient(0).pixel(20, 10));

        history().flush();
        QCOMPARE(history().ids().size(), 1);
        const QString hid = history().ids().first();
        const QJsonObject d = QJsonDocument::fromJson(TestEnv::readFile(history().descriptionPath(hid))).object();
        QCOMPARE(d.value("kind").toString(), QString("area"));
        QCOMPARE(d.value("savedPaths").toArray().first().toString(), path);
        QCOMPARE(QImage(history().pngPath(hid)).pixel(0, 0), gradient(100).pixel(20, 10));
    }

    void aFullScreenTimerShotIsTheWholeMonitor() {
        QVERIFY(press());
        model().keyPressed(Qt::Key_F);
        advance(3000);
        QCOMPARE(stack().count(), 1);
        QCOMPARE(QImage(stack().save(stack().ids().first())).size(), QSize(128, 80));
        history().flush();
        const QJsonObject d = QJsonDocument::fromJson(TestEnv::readFile(history().descriptionPath(history().ids().first()))).object();
        QCOMPARE(d.value("kind").toString(), QString("fullscreen")); // the History Kind filter's Full screen
    }

    void aFailedGrabAtTheEndNotifiesAndLeavesNoThumbnail() {
        QVERIFY(press());
        model().keyPressed(Qt::Key_F);
        env->setMode("grim", "fail");
        advance(3000);
        QCOMPARE(stack().count(), 0);
        QCOMPARE(env->calls("notify-send").size(), 1);
        QVERIFY(!countdown().active());
    }

    // ---- cancel ----

    void theKeyAgainDuringTheCountdownCancelsWithNoCapture() {
        QVERIFY(press());
        dragArea();
        advance(1000);
        QVERIFY(press());
        QVERIFY(!countdown().active());
        advance(5000);
        QCOMPARE(env->calls("grim").size(), 1);
        QCOMPARE(stack().count(), 0);
        history().flush();
        QVERIFY(history().ids().isEmpty());
        QVERIFY(env->calls("notify-send").isEmpty());
    }

    void afterACancelTheKeyStartsANewTimer() {
        QVERIFY(press());
        dragArea();
        QVERIFY(press()); // cancel
        QVERIFY(press()); // new Timer
        QVERIFY(model().active());
        QCOMPARE(model().mode(), OverlayModel::Screenshot);
        QVERIFY(model().timer());
    }

    void escInTheOverlayCancelsWithoutACountdown() {
        QVERIFY(press());
        model().keyPressed(Qt::Key_Escape);
        QVERIFY(!model().active());
        QVERIFY(!countdown().active());
        QCOMPARE(stack().count(), 0);
    }

    // ---- the label ----

    // A Countdown on a 400 x 300 monitor, on the surface, for the area (100,120)-(300,220); an empty
    // area means the full screen. The surface is up and its pill has dropped into place.
    void startOn(CountdownSurface &surface, const QRect &area) {
        const QRect monitor(0, 0, 400, 300);
        countdown().start("DP-1", monitor, area.isEmpty() ? monitor : area);
        QVERIFY(surface.visible());
        QCOMPARE(surface.view().size(), monitor.size());
        QQuickItem *label = findItem(surface.view().rootObject(), "countdownLabel");
        QVERIFY(label);
        QTRY_COMPARE_WITH_TIMEOUT(label->opacity(), 1.0, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(label->scale(), 1.0, 3000);
    }
    QImage frame(CountdownSurface &surface) { return surface.view().grabWindow().convertToFormat(QImage::Format_ARGB32); }

    // ---- the pill ----

    void thePillIsAtTheTopCenterAtTheGivenTop() {
        QCOMPARE(CountdownSurface::pillRect(QSize(2560, 1440), 68), QRect(1228, 68, 104, 44));
        QCOMPARE(CountdownSurface::pillRect(QSize(1000, 600), 12), QRect((1000 - 104) / 2, 12, 104, 44));
    }

    // ---- under the top bar ----

    // Starts a Countdown on DP-1 with the monitors fixture `json` and returns its top.
    int topWith(const QByteArray &json, const QString &name = "DP-1") {
        env->setFixture("hyprctl", "monitors.json", json);
        countdown().start(name, QRect(0, 0, 400, 300), QRect(0, 0, 400, 300));
        const int top = countdown().top();
        countdown().cancel();
        return top;
    }
    static QByteArray monitorJson(const QString &name, const QString &reservedField) {
        return QString(R"({"id":0,"name":"%1","x":0,"y":0,"width":400,"height":300,%2"scale":1.0,"transform":0})")
            .arg(name, reservedField).toUtf8();
    }

    void theCountdownSitsTwelveBelowTheReservedTop() {
        QCOMPARE(topWith("[" + monitorJson("DP-1", R"("reserved":[0,56,0,0],)") + "]"), 68);
    }

    void whateverTheBarsHeightTheGapIsTwelve() {
        QCOMPARE(topWith("[" + monitorJson("DP-1", R"("reserved":[0,90,0,0],)") + "]"), 102);
        QCOMPARE(topWith("[" + monitorJson("DP-1", R"("reserved":[0,30,0,0],)") + "]"), 42);
    }

    void withNoReservedTopItSitsTwelveFromTheTopEdge() {
        QCOMPARE(topWith("[" + monitorJson("DP-1", R"("reserved":[0,0,0,0],)") + "]"), 12);
    }

    void aMonitorThatReportsNothingCountsAsZero() {
        QCOMPARE(topWith("[" + monitorJson("DP-1", "") + "]"), 12);
    }

    void onlyTheTopCounts() {
        QCOMPARE(topWith("[" + monitorJson("DP-1", R"("reserved":[7,0,9,40],)") + "]"), 12);
    }

    void itReadsTheMonitorItAppearsOn() {
        const QByteArray two = "[" + monitorJson("DP-1", R"("reserved":[0,56,0,0],)") + "," +
                               monitorJson("DP-2", R"("reserved":[0,20,0,0],)") + "]";
        QCOMPARE(topWith(two, "DP-2"), 32);
        QCOMPARE(topWith(two, "DP-1"), 68);
    }

    void theTopIsReadWhenTheCountdownStartsNotAfter() {
        env->setFixture("hyprctl", "monitors.json", "[" + monitorJson("DP-1", R"("reserved":[0,56,0,0],)") + "]");
        countdown().start("DP-1", QRect(0, 0, 400, 300), QRect(0, 0, 400, 300));
        env->setFixture("hyprctl", "monitors.json", "[" + monitorJson("DP-1", R"("reserved":[0,10,0,0],)") + "]");
        QCOMPARE(countdown().top(), 68);
        countdown().cancel();
    }

    void theQmlPillSitsWhereTheSurfaceSaysAndIsRedWithWhiteInk() {
        CountdownSurface surface(countdown(), env->daemon().service<tarn::Palette>());
        QQuickItem *root = surface.view().rootObject();
        QVERIFY(root);
        QQuickItem *label = findItem(root, "countdownLabel");
        QVERIFY(label);
        root->setSize(QSize(2560, 1440)); // the view sizes it when shown
        env->setFixture("hyprctl", "monitors.json", "[" + monitorJson("DP-1", R"("reserved":[0,56,0,0],)") + "]");
        countdown().start("DP-1", QRect(0, 0, 2560, 1440), QRect(0, 0, 2560, 1440));
        QCOMPARE(countdown().top(), 68);
        QCOMPARE(QRect(int(label->x()), int(label->y()), int(label->width()), int(label->height())),
                 CountdownSurface::pillRect(QSize(2560, 1440), 68));
        countdown().cancel();
        QQuickItem *pill = findItem(label, "osdPill");
        QVERIFY(pill);
        QCOMPARE(pill->property("radius").toReal(), pill->height() / 2); // a pill
        QCOMPARE(pill->property("color").value<QColor>(), QColor("#FF453A")); // the recording red
        QCOMPARE(pill->property("border").value<QObject *>()->property("width").toInt(), 0);
        QVERIFY(findItem(root, "countdownClock"));
        QCOMPARE(findItem(root, "countdownText")->property("color").value<QColor>(), QColor("#FFFFFF"));
    }

    void thePillIsRedOnScreen() {
        CountdownSurface surface(countdown(), env->daemon().service<tarn::Palette>());
        startOn(surface, QRect(100, 120, 200, 100));
        const QImage img = frame(surface);
        const QRect pill = CountdownSurface::pillRect(QSize(400, 300), countdown().top());
        QCOMPARE(img.pixel(pill.left() + 8, pill.center().y()), qRgba(0xFF, 0x45, 0x3A, 255));
        countdown().cancel();
    }

    // ---- the region ----

    void anAreaCountdownDimsOutsideAndOutlinesTheAreaWithTheOverlaysLook() {
        CountdownSurface surface(countdown(), env->daemon().service<tarn::Palette>());
        startOn(surface, QRect(100, 120, 200, 100));
        const QImage img = frame(surface);
        const int dim = img.pixel(20, 250); // outside the area
        QCOMPARE(qAlpha(dim), 128);         // the Overlay's 50% black
        QCOMPARE(qRed(dim) + qGreen(dim) + qBlue(dim), 0);
        QCOMPARE(qAlpha(img.pixel(200, 170)), 0); // inside the area: clear, the live screen shows
        QCOMPARE(qAlpha(img.pixel(100, 120)), 0); // the area's own corner pixel
        // The outline: 3 px just outside the edge, the accent at 50% over the dim.
        for (const QPoint p : {QPoint(98, 170), QPoint(301, 170), QPoint(200, 118), QPoint(200, 221)}) {
            const int px = img.pixel(p);
            QVERIFY2(qAlpha(px) > 128 && qAlpha(px) < 255, qPrintable(QString("%1,%2").arg(p.x()).arg(p.y())));
        }
        QCOMPARE(qAlpha(img.pixel(96, 170)), 128); // just beyond the 3 px outline: dim again
        countdown().cancel();
    }

    void aFullScreenCountdownHasOnlyThePill() {
        CountdownSurface surface(countdown(), env->daemon().service<tarn::Palette>());
        startOn(surface, QRect());
        QVERIFY(countdown().fullScreen());
        const QImage img = frame(surface);
        QCOMPARE(qAlpha(img.pixel(20, 250)), 0); // no dim
        QCOMPARE(qAlpha(img.pixel(1, 1)), 0);
        const QRect pill = CountdownSurface::pillRect(QSize(400, 300), countdown().top());
        QCOMPARE(img.pixel(pill.left() + 8, pill.center().y()), qRgba(0xFF, 0x45, 0x3A, 255));
        countdown().cancel();
    }

    void anAreaCountdownIsNotFullScreen() {
        countdown().start("DP-1", QRect(0, 0, 400, 300), QRect(0, 0, 400, 299));
        QVERIFY(!countdown().fullScreen());
        countdown().cancel();
    }

    void thePillDropsInOverTimeAndLeavesAtOnce() {
        CountdownSurface surface(countdown(), env->daemon().service<tarn::Palette>());
        QQuickItem *root = surface.view().rootObject();
        QQuickItem *label = findItem(root, "countdownLabel");
        QVERIFY(label);
        QVERIFY(!label->isVisible()); // nothing before the Countdown
        QList<qreal> opacities;
        connect(label, &QQuickItem::opacityChanged, this, [&] { opacities << label->opacity(); });
        QVERIFY(press());
        dragArea();
        QVERIFY(surface.visible());
        QTRY_COMPARE_WITH_TIMEOUT(label->opacity(), 1.0, 3000);
        QVERIFY2(opacities.size() > 3, "it must drop in over several frames, not appear at once");
        QVERIFY(opacities.first() < 0.5);
        QVERIFY(std::is_sorted(opacities.begin(), opacities.end()));
        // Hidden: gone in the same breath (no exit motion), before the grab.
        advance(3100);
        QVERIFY(!surface.visible());
        QVERIFY(!label->isVisible());
        QCOMPARE(label->opacity(), 0.0);
    }

    void theLabelShowsTheSecondsAndIsOffScreenBeforeTheShot() {
        CountdownSurface surface(countdown(), env->daemon().service<tarn::Palette>());
        QQuickItem *root = surface.view().rootObject();
        QVERIFY(root);
        QVERIFY(!surface.visible());
        QVERIFY(press());
        dragArea();
        QVERIFY(surface.visible());
        QTRY_COMPARE(findItem(root, "countdownText")->property("text").toString(), QString("3"));
        advance(1100);
        QTRY_COMPARE(findItem(root, "countdownText")->property("text").toString(), QString("2"));
        // Hidden is signaled before the grab, and the label is gone when the shot lands.
        QSignalSpy hidden(&countdown(), &Countdown::hidden);
        advance(1900);
        QCOMPARE(hidden.count(), 1);
        QVERIFY(!surface.visible());
        QCOMPARE(env->calls("grim").size(), 2);
        QCOMPARE(stack().count(), 1);
    }

    void theLabelGoesOnACancelToo() {
        CountdownSurface surface(countdown(), env->daemon().service<tarn::Palette>());
        QVERIFY(press());
        dragArea();
        QVERIFY(surface.visible());
        QVERIFY(press());
        QVERIFY(!surface.visible());
    }

    void theWholeMonitorSurfaceNeverAcceptsInputOrFocus() {
        CountdownSurface surface(countdown(), env->daemon().service<tarn::Palette>());
        startOn(surface, QRect(100, 120, 200, 100));
        QCOMPARE(surface.view().size(), QSize(400, 300)); // covers the monitor, so the flags cover all of it
        const Qt::WindowFlags flags = surface.view().flags();
        QVERIFY(flags & Qt::WindowTransparentForInput);
        QVERIFY(flags & Qt::WindowDoesNotAcceptFocus);
        countdown().cancel();
    }

    void everythingIsGoneWhenGrimRuns() {
        CountdownSurface surface(countdown(), env->daemon().service<tarn::Palette>());
        QQuickItem *root = surface.view().rootObject();
        QVERIFY(press());
        dragArea();
        QVERIFY(surface.visible());
        bool visibleAtHidden = true;
        QObject::connect(&countdown(), &Countdown::hidden, &surface, [&] { visibleAtHidden = surface.visible(); });
        advance(3100);
        QVERIFY(!visibleAtHidden); // the surface hides on `hidden`, before grim
        QVERIFY(!surface.visible());
        QVERIFY(!findItem(root, "countdownLabel")->isVisible());
        QCOMPARE(env->calls("grim").size(), 2);
    }
};

QTEST_MAIN(TimerTest)
#include "timer_test.moc"
