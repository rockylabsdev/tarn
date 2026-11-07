// Color Mode and Color pick. Key 4 turns the Overlay into a picker on the
// frozen image with the Magnifier forced on (its pill shows swatch and hex); a
// click copies the hex, Shift+click copies rgb(r, g, b); arrows move the pointer
// 1 px and Enter picks. One notification "Copied #... · saved", the color goes
// to the front of Saved colors, and the Overlay closes.
#include "test_env.h"

#include "overlay/overlay.h"
#include "saved_colors/saved_colors.h"

#include <QBuffer>
#include <QImage>
#include <QSignalSpy>
#include <QTest>

using tarn::Overlay;
using tarn::OverlayModel;
using tarn::test::TestEnv;

namespace {
// Device pixel (x, y) is rgb(x * 2, y * 3, (x ^ y) * 4).
QImage gradient() {
    QImage image(128, 80, QImage::Format_RGB32);
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
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0}])";
} // namespace

class ColorPickTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    QString clipboardText() { return QString::fromUtf8(env->stdinOfCall("tarn-clip", 1)); }
    void openColor() {
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_4);
        QCOMPARE(model().mode(), OverlayModel::Color);
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setFixture("grim", "output.png", pngOf(gradient()));
        env->setPointer(10, 10);
    }
    void cleanup() { env.reset(); }

    void fourEntersColorModeWithTheMagnifierOnAndShowingTheColor() {
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(!model().magnifier());
        model().keyPressed(Qt::Key_4);
        QCOMPARE(model().mode(), OverlayModel::Color);
        QVERIFY(model().magnifier());
        model().pointerMoved(10, 5); // device pixel (20, 10) = rgb(40, 30, 120)
        QCOMPARE(model().magnifierLabel(), QString("#281e78"));
        QCOMPARE(model().pointerColor(), QColor(40, 30, 120));
        model().keyPressed(Qt::Key_M); // forced on: M cannot turn it off
        QVERIFY(model().magnifier());
        model().keyPressed(Qt::Key_1); // and a plain Mode gets the user's setting back
        QVERIFY(!model().magnifier());
    }

    // The pill shows what a click would copy: rgb while Shift is held, hex again on release.
    void holdingShiftShowsRgbInThePill() {
        openColor();
        model().pointerMoved(10, 5);
        QSignalSpy changed(&model(), &OverlayModel::pointerChanged);
        model().keyPressed(Qt::Key_Shift, Qt::ShiftModifier);
        QCOMPARE(model().magnifierLabel(), QString("rgb(40, 30, 120)"));
        QVERIFY(!changed.isEmpty()); // so QML re-reads the label
        model().keyReleased(Qt::Key_Shift, Qt::ShiftModifier); // Qt may still report Shift on its own release
        QCOMPARE(model().magnifierLabel(), QString("#281e78"));
        model().pointerMoved(11, 5, Qt::ShiftModifier); // device (22, 10); Shift already down when the pointer moves
        QCOMPARE(model().magnifierLabel(), QString("rgb(44, 30, 112)"));
    }

    void shiftOutsideColorModeKeepsThePosition() {
        QVERIFY(env->request({"capture", "area"}).ok);
        model().pointerMoved(10, 5);
        model().keyPressed(Qt::Key_Shift, Qt::ShiftModifier);
        QCOMPARE(model().magnifierLabel(), QString("20, 10"));
    }

    void clickCopiesTheHexFromTheFrozenImageAndNotifies() {
        openColor();
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerReleased(10, 5, Qt::LeftButton);
        QCOMPARE(clipboardText(), QString("#281e78"));
        QCOMPARE(env->calls("tarn-clip").first().first(), QString("--text"));
        const auto notes = env->calls("notify-send");
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.first().last(), QString("Copied #281e78 · saved"));
    }

    void shiftClickCopiesRgb() {
        openColor();
        model().pointerPressed(10, 5, Qt::LeftButton, Qt::ShiftModifier);
        model().pointerReleased(10, 5, Qt::LeftButton, Qt::ShiftModifier);
        QCOMPARE(clipboardText(), QString("rgb(40, 30, 120)"));
        QCOMPARE(env->calls("notify-send").first().last(), QString("Copied rgb(40, 30, 120) · saved"));
        QCOMPARE(env->daemon().service<tarn::SavedColors>().colors(), QList<QColor>{QColor(40, 30, 120)});
    }

    void arrowsMoveOnePixelAndEnterPicks() {
        openColor();
        model().pointerMoved(10, 5);
        model().keyPressed(Qt::Key_Right);
        model().keyPressed(Qt::Key_Down);
        QCOMPARE(model().pointer(), QPoint(11, 6));
        QVERIFY(!env->calls("hyprctl").isEmpty()); // the warp is asked of Hyprland, as in the other Modes
        model().keyPressed(Qt::Key_Return); // device pixel (22, 12) = rgb(44, 36, 104)
        QCOMPARE(clipboardText(), QString("#2c2468"));
        QVERIFY(!model().active());
    }

    void enterWithShiftPicksRgb() {
        openColor();
        model().pointerMoved(10, 5);
        model().keyPressed(Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(clipboardText(), QString("rgb(40, 30, 120)"));
    }

    void everyPickGoesToTheFrontOfSavedColorsAndTheOverlayCloses() {
        auto &saved = env->daemon().service<tarn::SavedColors>();
        openColor();
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerReleased(10, 5, Qt::LeftButton);
        QVERIFY(!env->daemon().service<Overlay>().isOpen());
        QCOMPARE(saved.colors(), QList<QColor>{QColor(40, 30, 120)});

        openColor();
        model().pointerPressed(20, 10, Qt::LeftButton); // device (40, 20) = rgb(80, 60, 240)
        model().pointerReleased(20, 10, Qt::LeftButton);
        QCOMPARE(saved.colors(), (QList<QColor>{QColor(80, 60, 240), QColor(40, 30, 120)}));
    }

    void aRightClickPicksNothing() {
        openColor();
        model().pointerPressed(10, 5, Qt::RightButton);
        QVERIFY(!model().active());
        QVERIFY(env->calls("tarn-clip").isEmpty());
        QVERIFY(env->calls("notify-send").isEmpty());
        QVERIFY(env->daemon().service<tarn::SavedColors>().colors().isEmpty());
    }

    void fullScreenDoesNothingInColorMode() {
        openColor();
        model().keyPressed(Qt::Key_F);
        QVERIFY(model().active());
        QVERIFY(!model().hasSelection());
        QVERIFY(env->calls("tarn-clip").isEmpty());
    }
};

QTEST_MAIN(ColorPickTest)
#include "color_pick_test.moc"
