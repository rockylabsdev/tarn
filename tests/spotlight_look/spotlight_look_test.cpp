// The Spotlight's rectangle has rounded corners (a share of the shot's size,
// like the Frame's corners); Rectangle and Filled rectangle keep hard corners.
// Reference PNGs in tests/spotlight_look/reference (regenerate with
// TARN_UPDATE_REFERENCES=1 through ctest, then look).
#include "document/annotation.h"
#include "document/capture_document.h"

#include <QDir>
#include <QImage>
#include <QTest>

using tarn::CaptureDocument;
using tarn::DragGesture;
using tarn::ToolStyle;

#ifndef TARN_SPOTLIGHT_REFERENCE_DIR
#error "set by tests/spotlight_look/CMakeLists.txt"
#endif

namespace {

QImage testShot(int w, int h) {
    QImage image(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            image.setPixel(x, y, qRgb(30 + x % 200, 60 + y % 150, 120 + (x + y) % 100));
    return image;
}

void compareWithReference(const QImage &actual, const QString &name) {
    const QString path = QStringLiteral(TARN_SPOTLIGHT_REFERENCE_DIR "/") + name;
    if (qEnvironmentVariableIsSet("TARN_UPDATE_REFERENCES")) {
        QVERIFY(actual.save(path, "PNG"));
        return;
    }
    QImage expected(path);
    QVERIFY2(!expected.isNull(), qPrintable("missing reference " + path));
    QCOMPARE(actual.size(), expected.size());
    QCOMPARE(actual.convertToFormat(QImage::Format_ARGB32), expected.convertToFormat(QImage::Format_ARGB32));
}

bool isDimmed(const QImage &out, const QImage &shot, int x, int y) { return qRed(out.pixel(x, y)) < qRed(shot.pixel(x, y)) - 10; }

} // namespace

class SpotlightLookTest : public QObject {
    Q_OBJECT
private slots:
    void theRectangleSpotlightHasRoundedCorners() {
        const QImage shot = testShot(200, 120);
        CaptureDocument doc(shot);
        doc.draw("spotlight", DragGesture{{40, 30}, {160, 90}, {}, ToolStyle{}});
        const QImage out = doc.flatten();
        compareWithReference(out, "rounded.png");
        QVERIFY(isDimmed(out, shot, 40, 30));   // the very corner is dimmed
        QVERIFY(!isDimmed(out, shot, 100, 60)); // the middle is bright
        QVERIFY(!isDimmed(out, shot, 100, 31)); // straight edges reach the rectangle
        QVERIFY(!isDimmed(out, shot, 41, 60));
    }

    void theCornerRadiusScalesWithTheShot() {
        const QImage small = testShot(200, 120), big = testShot(800, 480);
        CaptureDocument a(small), b(big);
        a.draw("spotlight", DragGesture{{40, 30}, {160, 90}, {}, ToolStyle{}});
        b.draw("spotlight", DragGesture{{160, 120}, {640, 360}, {}, ToolStyle{}});
        // Four times the shot, four times the rounding: 3 px in from the corner is still
        // outside the arc of the big one (radius 14 px) but already inside that of the small (3.6 px).
        QVERIFY(!isDimmed(a.flatten(), small, 43, 33));
        QVERIFY(isDimmed(b.flatten(), big, 163, 123));
    }

    void rectangleAndFilledRectangleKeepHardCorners() {
        const QImage shot = testShot(200, 120);
        for (const char *tool : {"rect", "filled-rect"}) {
            CaptureDocument doc(shot);
            doc.draw(tool, DragGesture{{40, 30}, {160, 90}, {}, ToolStyle{}});
            const QImage out = doc.flatten();
            QVERIFY(out.pixel(41, 31) != shot.pixel(41, 31)); // painted right in the corner: not rounded away
        }
    }
};

QTEST_MAIN(SpotlightLookTest)
#include "spotlight_look_test.moc"
