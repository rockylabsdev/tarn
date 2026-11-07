// The all-in-one Overlay's Modes. Print opens on Screenshot; 1-3 (or the
// Mode bar) switch Mode and keep a kept Selection; the v1 keys open it preset;
// T is the Timer toggle (Screenshot only, no window highlight, a clock in the
// size pill); F takes the whole monitor in every Mode; Text Mode copies a
// clicked window's text; the Mode bar jumps to the bottom edge while a
// Selection is within 24 px of its spot.
#include "test_env.h"

#include <algorithm>

#include <cmath>

#include "overlay/overlay.h"
#include "text/text_layout.h"
#include "overlay_surface.h"
#include "palette/palette.h"
#include "text/text_capture.h"
#include "timer/countdown.h"

#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>

using tarn::Overlay;
using tarn::OverlayModel;
using tarn::WindowInfo;
using tarn::test::TestEnv;

namespace {
QImage gradient(int w = 128, int h = 80) {
    QImage image(w, h, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb(x * 2, y * 3, (x ^ y) * 4));
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
const QByteArray kMonitor =
    R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0,"activeWorkspace":{"id":1},"specialWorkspace":{"id":0}}])";
// A roomy 1000 x 600 monitor at scale 1, for the tests that click the Mode bar.
const QByteArray kBigMonitor =
    R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":1000,"height":600,"scale":1.0,"transform":0,"activeWorkspace":{"id":1},"specialWorkspace":{"id":0}}])";
const QSize kScreen(1000, 600);
WindowInfo win(int x, int y, int w, int h, const QString &app) { return {QRect(x, y, w, h), app, app + " title", 0}; }

QQuickItem *findItem(QQuickItem *from, const QString &objectName) {
    if (from->objectName() == objectName)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = findItem(child, objectName))
            return hit;
    return nullptr;
}

void drag(OverlayModel &m, int x1, int y1, int x2, int y2, int mods = 0) {
    m.pointerPressed(x1, y1, Qt::LeftButton, mods);
    m.pointerMoved(x2, y2, mods);
    m.pointerReleased(x2, y2, Qt::LeftButton, mods);
}
} // namespace

class OverlayModesTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    tarn::Countdown &countdown() { return env->daemon().service<tarn::Countdown>(); }
    void useBigMonitor() {
        env->setFixture("hyprctl", "monitors.json", kBigMonitor);
        env->setFixture("grim", "output.png", pngOf(gradient(1000, 600)));
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setFixture("grim", "output.png", pngOf(gradient()));
        env->setPointer(10, 10);
    }
    void cleanup() { env.reset(); }

    // ---- opening and presets ----

    void printOpensOnScreenshotWithEveryToggleOff() {
        QVERIFY(env->request({"capture", "area"}).ok);
        QCOMPARE(model().mode(), OverlayModel::Screenshot);
        QVERIFY(!model().timer());
    }

    void theV1KeysOpenItPreset() {
        QVERIFY(env->request({"capture", "record"}).ok);
        QCOMPARE(model().mode(), OverlayModel::Record);
        QVERIFY(!model().timer());
        model().cancel();

        QVERIFY(env->request({"capture", "text"}).ok);
        QCOMPARE(model().mode(), OverlayModel::Text);
        model().cancel();

        QVERIFY(env->request({"capture", "timer"}).ok);
        QCOMPARE(model().mode(), OverlayModel::Screenshot);
        QVERIFY(model().timer());
    }

    void superShiftPrintAgainCancelsTheCountdown() {
        QVERIFY(env->request({"capture", "timer"}).ok);
        drag(model(), 10, 5, 30, 25);
        QVERIFY(countdown().active());
        QVERIFY(env->request({"capture", "timer"}).ok);
        QVERIFY(!countdown().active());
        QVERIFY(!model().active());
    }

    void togglesStartOffEveryTimeTheOverlayOpens() {
        QVERIFY(env->request({"capture", "timer"}).ok);
        QVERIFY(model().timer());
        model().cancel();
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(!model().timer());
        model().keyPressed(Qt::Key_T);
        QVERIFY(model().timer());
        model().cancel();
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(!model().timer());
    }

    // ---- switching Mode ----

    void keysOneToFourSwitchMode() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, kScreen);
        m.keyPressed(Qt::Key_2);
        QCOMPARE(m.mode(), OverlayModel::Record);
        QVERIFY(m.recordMode());
        m.keyPressed(Qt::Key_3);
        QCOMPARE(m.mode(), OverlayModel::Text);
        QVERIFY(!m.recordMode());
        m.keyPressed(Qt::Key_1);
        QCOMPARE(m.mode(), OverlayModel::Screenshot);
        m.keyPressed(Qt::Key_4); // Color Mode
        QCOMPARE(m.mode(), OverlayModel::Color);
        m.setMode(OverlayModel::Text); // a Mode bar click
        QCOMPARE(m.mode(), OverlayModel::Text);
        QVERIFY(m.active());
    }

    void aKeptSelectionSurvivesASwitchAndRecordPutsItsPanelOnIt() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, kScreen);
        drag(m, 100, 100, 300, 250, Qt::ControlModifier);
        QVERIFY(m.kept());
        const QRect kept = m.selection();
        m.keyPressed(Qt::Key_2);
        QVERIFY(m.kept());
        QCOMPARE(m.selection(), kept);
        QVERIFY(m.recordMode()); // the Record panel binds to kept && recordMode: the same area
        m.keyPressed(Qt::Key_3);
        QCOMPARE(m.selection(), kept);
        QVERIFY(m.kept());
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        m.confirm();
        QCOMPARE(confirmed.count(), 1);
        QCOMPARE(confirmed.first().first().toRect(), kept);
        QCOMPARE(confirmed.first().at(1).value<OverlayModel::Mode>(), OverlayModel::Text);
    }

    void switchingToRecordWhileDraggingStillKeepsTheFinishedDrag() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, kScreen);
        m.pointerPressed(10, 10, Qt::LeftButton);
        m.pointerMoved(100, 100);
        m.keyPressed(Qt::Key_2);
        m.pointerReleased(100, 100, Qt::LeftButton);
        QVERIFY(m.kept());
    }

    // ---- the Timer ----

    void tTogglesTheTimerOnScreenshotOnly() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, kScreen);
        m.setTimerDelay(3);
        m.keyPressed(Qt::Key_T);
        QVERIFY(m.timer());
        QCOMPARE(m.timerLabel(), QString("3 s"));
        m.keyPressed(Qt::Key_T);
        QVERIFY(!m.timer());
        for (auto mode : {OverlayModel::Record, OverlayModel::Text}) {
            m.setMode(mode);
            QVERIFY(!m.timerAvailable());
            m.keyPressed(Qt::Key_T);
            QVERIFY(!m.timer());
        }
        m.setMode(OverlayModel::Screenshot);
        QVERIFY(m.timerAvailable());
    }

    void switchingModeTurnsTheTimerOff() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, kScreen, 1.0, true);
        QVERIFY(m.timer());
        m.keyPressed(Qt::Key_3);
        QVERIFY(!m.timer());
        m.keyPressed(Qt::Key_1);
        QVERIFY(!m.timer());
    }

    void theTimerRemovesTheWindowHighlightAndTurningItOffBringsItBack() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, kScreen);
        m.setWindows({win(10, 10, 200, 200, "a")});
        m.pointerMoved(50, 50);
        QVERIFY(m.hasHighlight());
        m.keyPressed(Qt::Key_T);
        QVERIFY(!m.hasHighlight());
        m.pointerMoved(60, 60);
        QVERIFY(!m.hasHighlight());
        m.keyPressed(Qt::Key_T);
        QVERIFY(m.hasHighlight());
    }

    void aTimerShotConfirmsAsScreenshotWithTheTimerStillOn() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, kScreen, 1.0, true);
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        drag(m, 10, 10, 50, 50);
        QCOMPARE(confirmed.count(), 1);
        QVERIFY(m.timer()); // readable while the Overlay service handles the outcome
    }

    // ---- F ----

    void fTakesTheWholeMonitorInScreenshotAndText() {
        for (auto mode : {OverlayModel::Screenshot, OverlayModel::Text}) {
            OverlayModel m;
            m.open(mode, kScreen);
            QSignalSpy confirmed(&m, &OverlayModel::confirmed);
            m.keyPressed(Qt::Key_F);
            QCOMPARE(confirmed.count(), 1);
            QCOMPARE(confirmed.first().first().toRect(), QRect(QPoint(0, 0), kScreen));
            QCOMPARE(confirmed.first().at(1).value<OverlayModel::Mode>(), mode);
        }
    }

    void fInRecordSelectsTheWholeMonitorForTheRecordPanel() {
        OverlayModel m;
        m.open(OverlayModel::Record, kScreen);
        m.keyPressed(Qt::Key_F);
        QVERIFY(m.kept());
        QCOMPARE(m.selection(), QRect(QPoint(0, 0), kScreen));
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        m.keyPressed(Qt::Key_Return); // the panel's Record Video
        QCOMPARE(confirmed.count(), 1);
        QCOMPARE(confirmed.first().first().toRect(), QRect(QPoint(0, 0), kScreen));
    }

    void fStartsAFullScreenCountdownWithTheTimerOn() {
        QVERIFY(env->request({"capture", "timer"}).ok);
        model().keyPressed(Qt::Key_F);
        QVERIFY(countdown().active());
        QCOMPARE(countdown().area(), QRect(0, 0, 64, 40));
    }

    // ---- Text Mode and windows ----

    void textModeHighlightsWindowsButRecordDoesNot() {
        OverlayModel m;
        m.open(OverlayModel::Text, kScreen);
        m.setWindows({win(10, 10, 200, 200, "a")});
        m.pointerMoved(50, 50);
        QVERIFY(m.hasHighlight());
        m.keyPressed(Qt::Key_2);
        QVERIFY(!m.hasHighlight());
        m.keyPressed(Qt::Key_3);
        QVERIFY(m.hasHighlight());
    }

    void aClickOnAWindowInTextModeCopiesThatWindowsText() {
        const QJsonObject client{{"mapped", true}, {"hidden", false}, {"workspace", QJsonObject{{"id", 1}}},
                                 {"at", QJsonArray{4, 4}}, {"size", QJsonArray{30, 20}}, {"fullscreen", 0},
                                 {"floating", false}, {"class", "term"}, {"title", "t"}, {"focusHistoryID", 0}};
        env->setFixture("hyprctl", "clients.json", QJsonDocument(QJsonArray{client}).toJson());
        QVERIFY(env->request({"capture", "text"}).ok);
        model().pointerMoved(10, 10);
        QVERIFY(model().hasHighlight());
        model().pointerPressed(10, 10, Qt::LeftButton);
        model().pointerReleased(10, 10, Qt::LeftButton);
        QTRY_VERIFY(!env->daemon().service<tarn::TextCapture>().busy());
        QVERIFY(!model().active());
        QCOMPARE(env->calls("tesseract").size(), 1);
        // the window: 30 x 20 logical at scale 2, as ZXingReader reads it (Tesseract
        // gets it enlarged 2x)
        const QImage seen = QImage::fromData(env->stdinOfCall("ZXingReader", 1), "PNG");
        QCOMPARE(seen.size(), QSize(60, 40));
        QCOMPARE(seen.pixel(0, 0), gradient().pixel(8, 8));
        QCOMPARE(QImage::fromData(env->stdinOfCall("tesseract", 1), "PNG").size(), QSize(120, 80));
        QCOMPARE(env->calls("tarn-clip").size(), 1); // the text was copied
    }

    // ---- Mode bar placement ----

    void theBarSitsAtTheTopUnlessASelectionIsWithin24pxOfItsSpot() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, kScreen);
        QVERIFY(!m.modeBarAtBottom());
        const QRect spot = m.modeBarTopSpot();
        QCOMPARE(spot.top(), 24);
        QCOMPARE(spot.x() * 2 + spot.width(), kScreen.width());

        drag(m, 100, 300, 400, 500, Qt::ControlModifier); // far below the spot
        QVERIFY(!m.modeBarAtBottom());
        m.clearKept();

        // Clear of the spot's bottom edge by 24 px counts; by 25 does not.
        const int bottom = spot.bottom() + 1;
        drag(m, spot.left(), bottom + 24, spot.right(), bottom + 100, Qt::ControlModifier);
        QVERIFY(m.modeBarAtBottom());
        m.clearKept();
        QVERIFY(!m.modeBarAtBottom());
        drag(m, spot.left(), bottom + 25, spot.right(), bottom + 100, Qt::ControlModifier);
        QVERIFY(!m.modeBarAtBottom());
        // Beside the spot by more than 24 px does not count either.
        m.clearKept();
        drag(m, spot.left() - 80, 30, spot.left() - 25, 60, Qt::ControlModifier);
        QVERIFY(!m.modeBarAtBottom());
    }

    void aDraggedSelectionMovesTheBarAndDraggingAwayBringsItBack() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, kScreen);
        m.pointerPressed(480, 400, Qt::LeftButton);
        m.pointerMoved(520, 300);
        QVERIFY(!m.modeBarAtBottom());
        m.pointerMoved(520, 40);
        QVERIFY(m.modeBarAtBottom());
        m.pointerMoved(520, 300);
        QVERIFY(!m.modeBarAtBottom());
    }

    void hoveringNeverMovesTheBar() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, kScreen);
        const QPoint c = m.modeBarTopSpot().center();
        m.pointerMoved(c.x(), c.y());
        QVERIFY(!m.modeBarAtBottom());
        m.pointerMoved(c.x(), 2);
        QVERIFY(!m.modeBarAtBottom());
    }

    // ---- QML ----

    void theModeBarLoadsAndBindsToTheModel() {
        useBigMonitor();
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QCOMPARE(surface.view().status(), QQuickView::Ready);
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        auto *bar = findItem(root, "modeBar");
        QVERIFY(bar);
        QVERIFY(bar->isVisible());
        QCOMPARE(bar->y(), 24.0);
        for (const char *name : {"modeBtnScreenshot", "modeBtnRecord", "modeBtnText", "modeBtnColor", "modeBtnTimer",
                                 "modeBtnSequence", "modeBtnFull", "modeBtnLast"})
            QVERIFY2(findItem(root, name), name);
        QCOMPARE(findItem(root, "modeBtnScreenshot")->property("active").toBool(), true);

        model().keyPressed(Qt::Key_3);
        QTRY_COMPARE(findItem(root, "modeBtnText")->property("active").toBool(), true);
        QCOMPARE(findItem(root, "modeBtnScreenshot")->property("active").toBool(), false);
        model().keyPressed(Qt::Key_1);
        model().keyPressed(Qt::Key_T);
        QTRY_COMPARE(findItem(root, "modeBtnTimer")->property("active").toBool(), true);
    }

    // The key letter sits inside the pill's rounded corner with room to spare, clear of
    // the icon; the Text layout's name fits its button in every layout; and the bar
    // the model places is wide enough for its buttons in every Mode.
    void everyLabelHasRoomInTheModeBar() {
        useBigMonitor();
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        auto *bar = findItem(root, "modeBar");
        auto *row = findItem(root, "modeBarRow");
        QVERIFY(bar && row);
        const char *buttons[] = {"modeBtnScreenshot", "modeBtnRecord", "modeBtnText", "modeBtnColor",
                                 "modeBtnTimer", "modeBtnSequence", "modeBtnFull", "modeBtnLast"};
        auto checkKeys = [&] {
            for (const char *name : buttons) {
                QQuickItem *btn = findItem(root, name);
                QQuickItem *key = findItem(btn, "keyLabel");
                QVERIFY2(key, name);
                const qreal r = btn->height() / 2;
                // The key's top-right corner, measured from the center of the pill's right end.
                const qreal dx = key->x() + key->width() - (btn->width() - r), dy = r - key->y();
                QVERIFY2(std::hypot(std::max<qreal>(dx, 0), dy) <= r - 2, name);
                QVERIFY2(key->x() >= btn->property("iconSlot").toReal() - 12 + 3, name); // clear of the 16 px icon
            }
        };
        for (int mode : {0, 1, 2, 3}) {
            model().setMode(mode);
            QCoreApplication::processEvents();
            QTRY_COMPARE(bar->width(), qreal(model().modeBarTopSpot().width()));
            QVERIFY2(row->width() + 2 * 6 <= bar->width(), qPrintable(QString("mode %1").arg(mode)));
            checkKeys();
        }
        model().setMode(2);
        QQuickItem *text = findItem(root, "modeBtnText");
        QQuickItem *name = findItem(root, "textLayoutLabel");
        QQuickItem *key = findItem(text, "keyLabel");
        for (const QString layout : {"Paragraphs", "Lines", "Table"}) {
            QTRY_COMPARE(name->property("text").toString(), layout);
            // The button hugs the name: 6 px to the key letter (up to 1 px of rounding),
            // once its width morph settles.

            // The bar the daemon places (it decides the jump to the bottom) is exactly
            // the bar drawn: the buttons plus 6 px each side.
            QTRY_COMPARE(qRound(row->width()) + 2 * 6, model().modeBarTopSpot().width());
            QTRY_COMPARE(bar->width(), qreal(model().modeBarTopSpot().width()));
            QTRY_COMPARE(text->width(), text->property("boxW").toReal()); // the Spring at rest
            const qreal gap = key->x() - (findItem(root, "textLayoutClip")->x() + name->width());
            QVERIFY2(gap >= 6 && gap < 7, qPrintable(QString("%1: gap %2").arg(layout).arg(gap)));
            model().keyPressed(Qt::Key_K);
        }
    }

    // K swaps the name at once while the Text button morphs to its new width; the
    // name is clipped to the pill, so a longer one never shows outside it mid-morph.
    void aLongerLayoutNameNeverShowsOutsideThePillWhileItGrows() {
        useBigMonitor();
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        model().setMode(2);
        model().keyPressed(Qt::Key_K); // Paragraphs -> Lines
        QQuickItem *btn = findItem(root, "modeBtnText");
        QTRY_COMPARE(btn->width(), qreal(46 + OverlayModel::textButtonExtra(tarn::TextLayout::Lines)));
        model().keyPressed(Qt::Key_K); // Lines -> Table
        model().keyPressed(Qt::Key_K); // Table -> Paragraphs: the pill grows by 34 px
        QQuickItem *clip = findItem(root, "textLayoutClip");
        QVERIFY(clip && clip->clip());
        QCOMPARE(findItem(root, "textLayoutLabel")->property("text").toString(), QString("Paragraphs"));
        for (int i = 0; i < 12; ++i) { // through the 160 ms morph
            QVERIFY2(clip->x() + clip->width() <= btn->width(), qPrintable(QString("at step %1").arg(i)));
            QTest::qWait(16);
        }
    }

    // Leaving Text Mode while the Text button is still growing bends the motion (a Spring
    // keeps its speed) instead of turning on the spot: CODING_STANDARDS.md, Motion.
    void leavingTextModeMidGrowthBendsInsteadOfRestarting() {
        useBigMonitor();
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        QQuickItem *btn = findItem(surface.view().rootObject(), "modeBtnText");
        QTRY_COMPARE(btn->width(), 46.0);
        QList<qreal> widths;
        QObject::connect(btn, &QQuickItem::widthChanged, btn, [&] { widths << btn->width(); });
        model().setMode(2); // grows to name the Text layout
        const qreal full = 46 + OverlayModel::textButtonExtra(model().textLayout());
        QTRY_VERIFY(btn->width() > 50);
        const qreal atReversal = btn->width();
        if (atReversal >= full - 1)
            QSKIP("the button grew fully before the test could reverse it (a slow machine)");
        const int from = widths.size();
        model().setMode(0);
        QTRY_COMPARE(btn->width(), 46.0);
        const qreal peak = *std::max_element(widths.cbegin() + from, widths.cend());
        QVERIFY2(peak > atReversal, qPrintable(QString("turned on the spot at %1").arg(atReversal)));
    }

    // While the Text button glides, the bar's pill follows its buttons frame by frame:
    // it never jumps to its final width ahead of them (the buttons spilled past its ends).
    void theBarHugsItsButtonsThroughoutTheGlide() {
        useBigMonitor();
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        QQuickItem *bar = findItem(root, "modeBar");
        QQuickItem *row = findItem(root, "modeBarRow");
        QQuickItem *text = findItem(root, "modeBtnText");
        model().setMode(2); // Paragraphs, the widest name
        QTRY_COMPARE(text->width(), text->property("boxW").toReal());
        model().keyPressed(Qt::Key_K); // Lines: the button shrinks
        int frames = 0;
        for (int i = 0; i < 40 && text->width() != text->property("boxW").toReal(); ++i, ++frames) {
            QCoreApplication::processEvents();
            QVERIFY2(qAbs(bar->width() - (row->width() + 2 * 6)) < 0.5,
                     qPrintable(QString("frame %1: bar %2, buttons %3").arg(i).arg(bar->width()).arg(row->width())));
            QTest::qWait(8);
        }
        QVERIFY(frames > 0); // it was caught mid-glide
    }

    void theBarJumpsToTheBottomWithTheSelection() {
        useBigMonitor();
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *bar = findItem(surface.view().rootObject(), "modeBar");
        model().pointerPressed(480, 400, Qt::LeftButton);
        model().pointerMoved(520, 40);
        QTRY_COMPARE(bar->y(), qreal(600 - 24 - bar->height()));
        model().pointerMoved(520, 300);
        QTRY_COMPARE(bar->y(), 24.0);
    }

    void theSizePillShowsTheDelayWithAClockWhileTheTimerIsOn() {
        useBigMonitor();
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(env->request({"capture", "timer"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        model().pointerPressed(10, 100, Qt::LeftButton);
        model().pointerMoved(40, 130);
        QTRY_VERIFY(findItem(root, "sizePill")->isVisible());
        QTRY_COMPARE(findItem(root, "sizeLabel")->property("text").toString(), QString::fromUtf8("3 s   30 × 30"));
        QVERIFY(findItem(root, "timerClock")->isVisible());
    }

    void theRecordPanelStaysClearOfTheModeBarNearTheBottomEdge() {
        useBigMonitor(); // 1000 x 600 logical
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(env->request({"capture", "record"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        model().keepArea(QRect(40, 10, 600, 590)); // under the bar's top spot, down to the bottom edge, off-center
        auto *bar = findItem(root, "modeBar");
        auto *panel = findItem(root, "recordPanel");
        QVERIFY(bar && panel);
        QTRY_VERIFY(panel->isVisible());
        QTRY_VERIFY(model().modeBarAtBottom());
        QTRY_COMPARE(bar->y(), qreal(600 - 24 - bar->height()));
        const QRectF barRect(bar->x(), bar->y(), bar->width(), bar->height());
        QTRY_VERIFY(!barRect.intersects(QRectF(panel->x(), panel->y(), panel->width(), panel->height())));
        QVERIFY(panel->y() >= 0);
        // Stepped clear, the panel stacks on the bar: same center line, a 12 px gap.
        QCOMPARE(panel->x() + panel->width() / 2, bar->x() + bar->width() / 2);
        QCOMPARE(panel->y() + panel->height(), bar->y() - 12);
    }

    void aPressOnTheSequencePillDoesNotStartADrag() {
        useBigMonitor();
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        model().keyPressed(Qt::Key_S);
        auto *pill = findItem(surface.view().rootObject(), "sequencePill");
        QVERIFY(pill);
        QTRY_VERIFY(pill->isVisible());
        const QPoint p = pill->mapToScene(QPointF(pill->width() / 2, pill->height() / 2)).toPoint();
        QTest::mousePress(&surface.view(), Qt::LeftButton, {}, p);
        QTest::mouseMove(&surface.view(), p + QPoint(60, 80));
        QTest::mouseRelease(&surface.view(), Qt::LeftButton, {}, p + QPoint(60, 80));
        QVERIFY(model().selection().isEmpty()); // no Selection was dragged
        QCOMPARE(model().taken(), 0);
    }

    void clickingTheBarSwitchesMode() {
        useBigMonitor();
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *btn = findItem(surface.view().rootObject(), "modeBtnRecord");
        QVERIFY(btn);
        const QPointF p = btn->mapToScene(QPointF(btn->width() / 2, btn->height() / 2));
        QTest::mouseClick(&surface.view(), Qt::LeftButton, {}, p.toPoint());
        QCOMPARE(model().mode(), OverlayModel::Record);
    }
};

QTEST_MAIN(OverlayModesTest)
#include "overlay_modes_test.moc"
