// Shift draws a square, Alt draws from the center, held Space moves the
// Selection; the arrow keys warp the real pointer (through hyprctl) and
// Enter, arrows, Enter selects an area without the mouse.
#include "test_env.h"

#include "overlay/overlay.h"
#include "overlay/overlay_model.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QSignalSpy>
#include <QTest>

using tarn::Overlay;
using tarn::OverlayModel;
using tarn::test::TestEnv;

namespace {
constexpr int Shift = Qt::ShiftModifier;
constexpr int Alt = Qt::AltModifier;

QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// One 128 x 80 monitor at scale 2 (64 x 40 logical) placed at (100, 50) in the layout.
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":100,"y":50,"width":128,"height":80,"scale":2.0,"transform":0}])";

// An Overlay model of 100 x 80 logical px, with a drag from (20, 20) begun.
void begin(OverlayModel &m) {
    m.open(OverlayModel::Screenshot, QSize(100, 80));
    m.pointerPressed(20, 20, Qt::LeftButton);
}

void openOverlay(TestEnv &env) {
    env.setFixture("hyprctl", "monitors.json", kMonitor);
    env.setFixture("grim", "output.png", pngOf(QImage(128, 80, QImage::Format_RGB32)));
    env.setPointer(120, 60); // 20, 10 on the monitor
}
} // namespace

class OverlayKeysTest : public QObject {
    Q_OBJECT
private slots:
    void shiftDrawsASquareAtTheLongerSide() {
        OverlayModel m;
        begin(m);
        m.pointerMoved(50, 30, Shift);
        QCOMPARE(m.selection(), QRect(20, 20, 30, 30));
        m.pointerMoved(25, 60, Shift);
        QCOMPARE(m.selection(), QRect(20, 20, 40, 40));
        m.pointerMoved(5, 10, Shift); // up and left: grows towards the pointer
        QCOMPARE(m.selection(), QRect(5, 5, 15, 15));
    }

    void aSquareStopsAtTheMonitorEdge() {
        OverlayModel m;
        begin(m);
        m.pointerMoved(95, 30, Shift); // wants 75, only 60 fit below
        QCOMPARE(m.selection(), QRect(20, 20, 60, 60));
    }

    void altDrawsFromTheCenter() {
        OverlayModel m;
        begin(m);
        m.pointerMoved(30, 25, Alt);
        QCOMPARE(m.selection(), QRect(10, 15, 20, 10));
        m.pointerMoved(5, 5, Alt);
        QCOMPARE(m.selection(), QRect(5, 5, 30, 30));
    }

    void shiftAndAltTogetherMakeASquareAroundTheCenter() {
        OverlayModel m;
        begin(m);
        m.pointerMoved(30, 25, Shift | Alt);
        QCOMPARE(m.selection(), QRect(10, 10, 20, 20));
    }

    void pressingAndReleasingTheKeysReshapesWithoutMovingThePointer() {
        OverlayModel m;
        begin(m);
        m.pointerMoved(50, 30);
        QCOMPARE(m.selection(), QRect(20, 20, 30, 10));
        m.keyPressed(Qt::Key_Shift, Shift);
        QCOMPARE(m.selection(), QRect(20, 20, 30, 30));
        m.keyPressed(Qt::Key_Alt, Shift | Alt); // centered square, held in by the left and top edges
        QCOMPARE(m.selection(), QRect(0, 0, 40, 40));
        m.keyReleased(Qt::Key_Alt, Shift | Alt); // Qt may still list Alt on its own release
        QCOMPARE(m.selection(), QRect(20, 20, 30, 30));
        m.keyReleased(Qt::Key_Shift, Shift);
        QCOMPARE(m.selection(), QRect(20, 20, 30, 10));
    }

    void heldSpaceMovesTheSelectionInsteadOfResizingIt() {
        OverlayModel m;
        begin(m);
        m.pointerMoved(40, 30);
        QCOMPARE(m.selection(), QRect(20, 20, 20, 10));
        m.keyPressed(Qt::Key_Space);
        m.pointerMoved(50, 45);
        QCOMPARE(m.selection(), QRect(30, 35, 20, 10));
        m.keyReleased(Qt::Key_Space);
        m.pointerMoved(60, 50); // drawing again, from the moved anchor
        QCOMPARE(m.selection(), QRect(30, 35, 30, 15));
    }

    void spaceMoveStaysInsideTheMonitor() {
        OverlayModel m;
        begin(m);
        m.pointerMoved(40, 30);
        m.keyPressed(Qt::Key_Space);
        m.pointerMoved(99, 79);
        QCOMPARE(m.selection(), QRect(79, 69, 20, 10)); // the right edge stops it at 99, not 100+
        QVERIFY(QRect(0, 0, 100, 80).contains(m.selection()));
        m.pointerMoved(0, 0);
        QCOMPARE(m.selection().topLeft(), QPoint(0, 0));
    }

    void spaceMovesACenteredSelectionToo() {
        OverlayModel m;
        begin(m);
        m.pointerMoved(40, 30, Alt);
        QCOMPARE(m.selection(), QRect(0, 10, 40, 20));
        m.keyPressed(Qt::Key_Space, Alt);
        m.pointerMoved(50, 40, Alt);
        QCOMPARE(m.selection(), QRect(10, 20, 40, 20));
    }

    void arrowsAskForTheRealPointerToMoveOneOrTenPixels() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, QSize(100, 80));
        m.pointerMoved(50, 40);
        QSignalSpy warp(&m, &OverlayModel::pointerWarpRequested);
        m.keyPressed(Qt::Key_Right);
        m.keyPressed(Qt::Key_Down, Shift);
        m.keyPressed(Qt::Key_Left);
        m.keyPressed(Qt::Key_Up);
        QCOMPARE(warp.count(), 4);
        QCOMPARE(warp.at(0).at(0).toPoint(), QPoint(51, 40));
        QCOMPARE(warp.at(1).at(0).toPoint(), QPoint(51, 50));
        QCOMPARE(warp.at(2).at(0).toPoint(), QPoint(50, 50));
        QCOMPARE(warp.at(3).at(0).toPoint(), QPoint(50, 49));
    }

    void theOverlayServiceWarpsThroughHyprctl() {
        TestEnv env;
        openOverlay(env);
        QVERIFY(env.request({"capture", "area"}).ok);
        auto &model = env.daemon().service<Overlay>().model();
        model.pointerMoved(20, 10);
        model.keyPressed(Qt::Key_Right);
        model.keyPressed(Qt::Key_Down, Shift);
        QList<QStringList> warps;
        for (const auto &call : env.calls("hyprctl"))
            if (call.value(0) == "eval")
                warps << call;
        // The monitor sits at (100, 50) in the layout, so warps are in layout px.
        // Hyprland 0.56 reads dispatch arguments as Lua, so the warp is a Lua eval.
        QCOMPARE(warps, (QList<QStringList>{{"eval", "hl.dispatch(hl.dsp.cursor.move({x=121, y=60}))"}, {"eval", "hl.dispatch(hl.dsp.cursor.move({x=121, y=70}))"}}));
        for (const auto &call : env.calls("hyprctl"))
            QVERIFY(call.value(0) != "dispatch");
    }

    void enterArrowsEnterSelectsAndCapturesAnArea() {
        TestEnv env;
        openOverlay(env);
        QVERIFY(env.request({"capture", "area"}).ok);
        auto &model = env.daemon().service<Overlay>().model();
        model.pointerMoved(20, 10);
        QSignalSpy confirmed(&model, &OverlayModel::confirmed);
        model.keyPressed(Qt::Key_Return);
        QVERIFY(!model.hasSelection());
        for (int i = 0; i < 5; ++i)
            model.keyPressed(Qt::Key_Right);
        model.keyPressed(Qt::Key_Down, Shift);
        QCOMPARE(model.selection(), QRect(20, 10, 5, 10)); // Shift is a 10 px step here, not a square
        model.keyPressed(Qt::Key_Return);
        QCOMPARE(confirmed.count(), 1);
        QCOMPARE(confirmed.at(0).at(0).toRect(), QRect(20, 10, 5, 10));
        QCOMPARE(env.daemon().service<tarn::ThumbnailStack>().ids().size(), 1);
    }

    void enterWithNothingDrawnDoesNotCapture() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, QSize(100, 80));
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        m.keyPressed(Qt::Key_Return);
        m.keyPressed(Qt::Key_Return);
        QCOMPARE(confirmed.count(), 0);
        QVERIFY(m.active());
    }
};

QTEST_MAIN(OverlayKeysTest)
#include "overlay_keys_test.moc"
