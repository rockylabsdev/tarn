// The Watermark in the Frame. Drawing is compared with stored reference PNGs
// (regenerate with TARN_UPDATE_REFERENCES=1 through CTest, then look at the images);
// the Preset rules, the missing image and the Project file round trip are driven
// through the same seams the Editor and the capture use.
#include "test_env.h"

#include "document/capture_document.h"
#include "document/project_file.h"
#include "editor/editor.h"
#include "frame/frame.h"
#include "frame/watermark.h"
#include "preset/preset_store.h"
#include "state/remembered_state.h"
#include "theme.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QTest>

using tarn::CaptureDocument;
using tarn::Frame;
using tarn::Watermark;
using tarn::test::TestEnv;

#ifndef TARN_WATERMARK_REFERENCE_DIR
#error "set by tests/watermark/CMakeLists.txt"
#endif

namespace {

const QRgb kBlue = qRgb(40, 90, 160);

QImage shotOf(QSize size = QSize(200, 120)) {
    QImage image(size, QImage::Format_RGB32);
    image.fill(kBlue);
    return image;
}
QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}
// A 20x10 logo: left half solid red, right half solid green.
QImage logo() {
    QImage image(20, 10, QImage::Format_ARGB32);
    image.fill(qRgb(255, 0, 0));
    for (int y = 0; y < 10; ++y)
        for (int x = 10; x < 20; ++x)
            image.setPixel(x, y, qRgb(0, 255, 0));
    return image;
}
void compareWithReference(const QImage &actual, const QString &name) {
    const QString path = QStringLiteral(TARN_WATERMARK_REFERENCE_DIR "/") + name;
    if (qEnvironmentVariableIsSet("TARN_UPDATE_REFERENCES")) {
        QVERIFY(actual.save(path, "PNG"));
        return;
    }
    QImage expected(path);
    QVERIFY2(!expected.isNull(), qPrintable("missing reference " + path));
    QCOMPARE(actual.size(), expected.size());
    QCOMPARE(actual.convertToFormat(QImage::Format_ARGB32), expected.convertToFormat(QImage::Format_ARGB32));
}
Watermark imageMark(int corner = Watermark::BottomRight) {
    Watermark w;
    w.mode = Watermark::Image;
    w.path = "/logos/me.png";
    w.image = logo();
    w.corner = corner;
    w.opacity = 1.0;
    return w;
}
Watermark textMark(int corner = Watermark::BottomRight) {
    Watermark w;
    w.mode = Watermark::Text;
    w.text = "@jamey";
    w.corner = corner;
    return w;
}
const char *cornerName[] = {"tl", "tr", "bl", "br"};

void cornerRows() {
    QTest::addColumn<int>("corner");
    for (int c = 0; c < 4; ++c)
        QTest::newRow(cornerName[c]) << c;
}

tarn::ToolStyle styleWith(const QString &pixelateStyle) {
    tarn::ToolStyle s;
    s.choices.insert("pixelate_style", pixelateStyle);
    s.choices.insert("pixelate_strength", "3");
    s.choices.insert("spotlight_shape", "rectangle");
    return s;
}

} // namespace

class WatermarkTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    tarn::ThumbnailStack &stack() { return env->daemon().service<tarn::ThumbnailStack>(); }
    tarn::PresetStore &presets() { return env->daemon().service<tarn::PresetStore>(); }
    int notifications() const { return env->calls("notify-send").size(); }
    QString logoFile() const { return env->scratch() + "/logo.png"; }
    static Frame plainFrame() {
        Frame f;
        f.background.kind = "plain";
        f.background.params = {{"color", "#ff112233"}};
        return f;
    }

private slots:
    void initTestCase() { tarn::loadFonts(); }
    void init() {
        env = std::make_unique<TestEnv>();
        env->setNow(QDateTime(QDate(2026, 11, 3), QTime(9, 30, 0)));
    }
    void cleanup() { env.reset(); }

    // ---- drawing ----

    void imageInEachCornerMatchesItsReference_data() { cornerRows(); }
    void imageInEachCornerMatchesItsReference() {
        QFETCH(int, corner);
        CaptureDocument doc(shotOf());
        Frame f;
        f.watermark = imageMark(corner);
        doc.setFrame(f);
        compareWithReference(doc.flatten(), QStringLiteral("image-%1.png").arg(cornerName[corner]));
    }

    void imageSitsAtTheSizeAndMarginTheSpecGives() {
        // 200x120: shorter side 120. Size 8% = 9.6 -> the logo's long side is 10 px (10x5 here).
        // Margin 3% = 3.6 -> 4 px from the corner: the logo covers x 186..195, y 111..115.
        CaptureDocument doc(shotOf());
        Frame f;
        f.watermark = imageMark();
        doc.setFrame(f);
        const QImage out = doc.flatten();
        QCOMPARE(out.size(), QSize(200, 120));
        QCOMPARE(out.pixel(188, 113), qRgb(255, 0, 0));
        QCOMPARE(out.pixel(193, 113), qRgb(0, 255, 0));
        QCOMPARE(out.pixel(184, 113), kBlue);
        QCOMPARE(out.pixel(197, 113), kBlue);
        QCOMPARE(out.pixel(188, 109), kBlue);
        QCOMPARE(out.pixel(188, 117), kBlue);
    }

    void opacityBlendsTheImageWithTheShot() {
        CaptureDocument doc(shotOf());
        Frame f;
        f.watermark = imageMark();
        f.watermark.opacity = 0.5;
        doc.setFrame(f);
        const QRgb p = doc.flatten().pixel(188, 113);
        QVERIFY(qAbs(qRed(p) - (255 + 40) / 2) <= 2);
        QVERIFY(qAbs(qBlue(p) - 80) <= 2);
    }

    // ---- auto contrast: text takes near-black on a light picture ----

    // Darkest and brightest gray among the pixels of `out` inside `area`.
    static QPair<int, int> grayRange(const QImage &out, const QRect &area) {
        int lo = 255, hi = 0;
        for (int y = area.top(); y <= area.bottom(); ++y)
            for (int x = area.left(); x <= area.right(); ++x) {
                const int g = qGray(out.pixel(x, y));
                lo = std::min(lo, g);
                hi = std::max(hi, g);
            }
        return {lo, hi};
    }
    static QImage plainShot(QRgb color, QSize size = QSize(400, 240)) {
        QImage image(size, QImage::Format_RGB32);
        image.fill(color);
        return image;
    }
    static Frame textFrame() {
        Frame f;
        f.watermark = textMark();
        f.watermark.size = 0.2;
        f.watermark.opacity = 1.0;
        return f;
    }
    void textOnAWhiteShotComesOutDarkerThanTheWhiteAroundIt() {
        CaptureDocument doc(plainShot(qRgb(255, 255, 255)));
        doc.setFrame(textFrame());
        const auto [lo, hi] = grayRange(doc.flatten(), QRect(200, 120, 200, 120));
        QVERIFY2(lo < 60, qPrintable(QString::number(lo)));
        QCOMPARE(hi, 255);
    }

    void textOnABlackShotComesOutLighterThanTheBlackAroundIt() {
        CaptureDocument doc(plainShot(qRgb(0, 0, 0)));
        doc.setFrame(textFrame());
        const auto [lo, hi] = grayRange(doc.flatten(), QRect(200, 120, 200, 120));
        QVERIFY2(hi > 240, qPrintable(QString::number(hi)));
        QCOMPARE(lo, 0);
    }

    void textStaysWhiteWhenTheLightAreaIsOnlyInTheOppositeCorner() {
        QImage shot = plainShot(qRgb(0, 0, 0));
        QPainter(&shot).fillRect(0, 0, 200, 120, Qt::white);
        CaptureDocument doc(shot);
        doc.setFrame(textFrame());
        const auto [lo, hi] = grayRange(doc.flatten(), QRect(200, 120, 200, 120));
        QVERIFY(hi > 240);
        QCOMPARE(lo, 0);
    }

    void aCropThatMovesTheCornerOntoALightAreaTurnsTheTextDark() {
        QImage shot = plainShot(qRgb(0, 0, 0));
        QPainter(&shot).fillRect(0, 0, 200, 120, Qt::white);
        CaptureDocument doc(shot);
        doc.setFrame(textFrame());
        tarn::Crop crop;
        crop.rect = QRect(0, 0, 200, 120);
        QVERIFY(doc.setCrop(crop));
        const auto [lo, hi] = grayRange(doc.flatten(), QRect(0, 0, 200, 120));
        QVERIFY2(lo < 60, qPrintable(QString::number(lo)));
        QCOMPARE(hi, 255);
    }

    void aWatermarkOpacityStillAppliesToTheDarkText() {
        CaptureDocument doc(plainShot(qRgb(255, 255, 255)));
        Frame f = textFrame();
        f.watermark.opacity = 0.5;
        doc.setFrame(f);
        const auto [lo, hi] = grayRange(doc.flatten(), QRect(200, 120, 200, 120));
        QVERIFY2(lo > 100 && lo < 170, qPrintable(QString::number(lo)));
    }

    void anImageWatermarkIgnoresTheShotsBrightness() {
        CaptureDocument doc(plainShot(qRgb(255, 255, 255), QSize(200, 120)));
        Frame f;
        f.watermark = imageMark();
        doc.setFrame(f);
        QCOMPARE(doc.flatten().pixel(188, 113), qRgb(255, 0, 0));
    }

    void textIsWhiteWithADarkShadowInTheChosenCorner_data() { cornerRows(); }
    void textIsWhiteWithADarkShadowInTheChosenCorner() {
        QFETCH(int, corner);
        CaptureDocument doc(shotOf(QSize(400, 240)));
        Frame f;
        f.watermark = textMark(corner);
        f.watermark.size = 0.2;
        f.watermark.opacity = 1.0;
        doc.setFrame(f);
        const QImage out = doc.flatten();
        const QRect quadrant((corner % 2) * 200, (corner / 2) * 120, 200, 120);
        bool white = false, dark = false, outside = false;
        for (int y = 0; y < out.height(); ++y)
            for (int x = 0; x < out.width(); ++x) {
                const QRgb p = out.pixel(x, y);
                if (p == kBlue)
                    continue;
                if (!quadrant.contains(x, y))
                    outside = true;
                if (qRed(p) > 240 && qGreen(p) > 240 && qBlue(p) > 240)
                    white = true;
                if (qBlue(p) < 150 && qRed(p) < 40)
                    dark = true;
            }
        QVERIFY(white);
        QVERIFY(dark);
        QVERIFY(!outside);
        compareWithReference(out, QStringLiteral("text-%1.png").arg(cornerName[corner]));
    }

    void defaultsAndSliderRanges() {
        Frame f = Frame::fromMap({{"watermark", QVariantMap{{"mode", "text"}, {"text", "x"}, {"size", 0.9}, {"opacity", 0.0}}}});
        QCOMPARE(f.watermark.size, 0.25);
        QCOMPARE(f.watermark.opacity, 0.10);
        f = Frame::fromMap({{"watermark", QVariantMap{{"mode", "text"}, {"text", "x"}, {"size", 0.0}}}});
        QCOMPARE(f.watermark.size, 0.03);
        f = Frame::fromMap({});
        QCOMPARE(f.watermark.mode, Watermark::Off);
        QCOMPARE(f.watermark.corner, int(Watermark::BottomRight));
        QCOMPARE(f.watermark.size, 0.08);
        QCOMPARE(f.watermark.opacity, 0.6);
    }

    // ---- the last Watermark in Remembered state ----

    void theLastWatermarkRoundTripsThroughTheStateFile() {
        Watermark w;
        w.mode = Watermark::Text;
        w.text = "@me";
        w.path = "/somewhere/logo.png";
        w.corner = Watermark::TopRight;
        w.size = 0.12;
        w.opacity = 0.4;
        const QString file = env->scratch() + "/state/tarn/state.toml";
        tarn::RememberedState(file).setMap("watermark", w.rememberedMap());

        const tarn::RememberedState reopened(file); // a restart reads the file
        QCOMPARE(Watermark::fromMap(reopened.map("watermark")), w);
        QCOMPARE(reopened.value("watermark.mode").toString(), QString("text"));
        QCOMPARE(reopened.value("watermark.corner").toInt(), 1);
        QVERIFY(!reopened.contains("watermark.hasImage"));
        QVERIFY(!reopened.contains("watermark.png"));
    }

    void anOutOfRangeLastWatermarkIsClampedWhenReadBack() {
        const QString file = env->scratch() + "/state/tarn/state.toml";
        {
            tarn::RememberedState state(file);
            state.setValue("watermark.mode", "text");
            state.setValue("watermark.text", "@me");
            state.setValue("watermark.corner", 7);
            state.setValue("watermark.size", 0.9);
            state.setValue("watermark.opacity", 0.01);
        }
        const Watermark w = Watermark::fromMap(tarn::RememberedState(file).map("watermark"));
        QCOMPARE(w.mode, Watermark::Text);
        QCOMPARE(w.corner, int(Watermark::BottomRight));
        QCOMPARE(w.size, 0.25);
        QCOMPARE(w.opacity, 0.10);

        tarn::RememberedState(file).setValue("watermark.size", 0.0);
        QCOMPARE(Watermark::fromMap(tarn::RememberedState(file).map("watermark")).size, 0.03);
    }

    void afterACropItSitsInTheCornerOfTheCroppedShot() {
        CaptureDocument doc(shotOf());
        Frame f;
        f.watermark = imageMark(Watermark::TopLeft);
        doc.setFrame(f);
        tarn::Crop crop;
        crop.rect = QRect(100, 60, 100, 60); // shorter side 60: logo 5 px long side (5x3), margin 2 px
        QVERIFY(doc.setCrop(crop));
        const QImage out = doc.flatten();
        QCOMPARE(out.size(), QSize(100, 60));
        QCOMPARE(out.pixel(3, 3), qRgb(255, 0, 0));
        QCOMPARE(out.pixel(5, 3), qRgb(0, 255, 0));
        QCOMPARE(out.pixel(10, 10), kBlue);
        QCOMPARE(out.pixel(0, 0), kBlue);
    }

    void withABackgroundItSitsInsideTheCardInsideTheInset() {
        CaptureDocument doc(shotOf());
        Frame f = plainFrame();
        f.background.params = {{"color", "#ffffffff"}};
        f.padding = 0.2;
        f.inset = 0.05;
        f.shadow = 0;
        f.corners = 0;
        f.watermark = imageMark(Watermark::TopLeft);
        doc.setFrame(f);
        const QPoint c = doc.frameLayout().content.topLeft();
        const QImage out = doc.flatten();
        QCOMPARE(out.pixel(c.x() + 6, c.y() + 6), qRgb(255, 0, 0)); // margin 4 from the shot edge, not the card
        QCOMPARE(out.pixel(c.x() + 1, c.y() + 1), kBlue);
    }

    void itIsAboveTheSpotlightDimAndNeverUnderPixelate() {
        CaptureDocument doc(shotOf());
        Frame f;
        f.watermark = imageMark();
        doc.setFrame(f);
        QVERIFY(doc.draw("spotlight", tarn::DragGesture{{5, 5}, {40, 40}, {}, styleWith("black")}) > 0);
        QVERIFY(doc.draw("pixelate", tarn::DragGesture{{120, 60}, {200, 120}, {}, styleWith("black")}) > 0);
        const QImage out = doc.flatten();
        QVERIFY(out.pixel(100, 30) != kBlue);                // the dim is on
        QCOMPARE(out.pixel(130, 90), qRgb(0, 0, 0));          // Pixelate blacks out its area
        QCOMPARE(out.pixel(188, 113), qRgb(255, 0, 0));       // the Watermark is full color above both
        QCOMPARE(out.pixel(193, 113), qRgb(0, 255, 0));
    }

    // ---- Presets, B, a missing image ----

    void aPresetKeepsTheImageByPathSoANewFileReachesNewShots() {
        QVERIFY(logo().save(logoFile()));
        Frame f = plainFrame();
        f.watermark.mode = Watermark::Image;
        f.watermark.path = logoFile();
        f.watermark.corner = Watermark::TopRight;
        f.watermark.size = 0.12;
        f.watermark.opacity = 0.8;
        QVERIFY(presets().save("Mine", f));
        const QString text = QString::fromUtf8(TestEnv::readFile(env->scratch() + "/config/tarn/presets.toml"));
        QVERIFY(text.contains(logoFile()));
        QVERIFY(presets().setAuto("Mine"));

        env->restartDaemon();
        const Frame first = *presets().autoFrame();
        QCOMPARE(first.watermark.mode, Watermark::Image);
        QCOMPARE(first.watermark.corner, int(Watermark::TopRight));
        QCOMPARE(first.watermark.size, 0.12);
        QCOMPARE(first.watermark.opacity, 0.8);
        QCOMPARE(first.watermark.image.size(), QSize(20, 10));
        QImage other(40, 40, QImage::Format_ARGB32);
        other.fill(Qt::blue);
        QVERIFY(other.save(logoFile()));
        QCOMPARE(presets().autoFrame()->watermark.image.size(), QSize(40, 40));
        QCOMPARE(notifications(), 0);
    }

    void aWatermarkAloneCanBeAPresetOnBackgroundNone() {
        Frame f;
        f.watermark = textMark();
        QVERIFY(!f.active());
        QVERIFY(presets().save("Sign", f));
        env->restartDaemon();
        QCOMPARE(presets().preset("Sign")->watermark.text, QString("@jamey"));
        QVERIFY(!presets().preset("Sign")->active());
        QVERIFY(!presets().save("Empty", Frame())); // nothing at all still cannot be a Preset
    }

    void aMissingImageIsSkippedWithOneNotificationAndTheCaptureCompletes() {
        Frame f = plainFrame();
        f.watermark.mode = Watermark::Image;
        f.watermark.path = env->scratch() + "/gone/logo.png";
        QVERIFY(presets().save("Lost", f));
        QVERIFY(presets().setAuto("Lost"));
        const int id = stack().add(pngOf(shotOf(QSize(80, 60))), env->daemon().now(), {}, true);
        QVERIFY(id >= 0);
        QCOMPARE(notifications(), 1);
        QVERIFY(env->calls("notify-send").first().join(' ').contains("logo.png"));
        const Frame frame = *stack().autoFrame(id);
        QCOMPARE(frame.watermark.mode, Watermark::Off); // skipped; the rest of the Frame still applies
        QVERIFY(frame.active());
        QVERIFY(!stack().edited(id).isEmpty());
    }

    void applyingAPresetReplacesTheWholeFrameWatermarkIncluded() {
        QVERIFY(logo().save(logoFile()));
        Frame withMark = plainFrame();
        withMark.watermark.mode = Watermark::Image;
        withMark.watermark.path = logoFile();
        Frame without = plainFrame();
        QVERIFY(presets().save("With", withMark));
        QVERIFY(presets().save("Without", without));

        const int id = stack().add(pngOf(shotOf()), env->daemon().now(), {}, false);
        auto *session = env->daemon().service<tarn::Editor>().open(id);
        QVERIFY(session);
        auto mode = [&] { return session->frameMap().value("watermark").toMap().value("mode").toString(); };
        QVERIFY(session->applyPreset("With"));
        QCOMPARE(mode(), QString("image"));
        QVERIFY(session->applyPreset("Without"));
        QCOMPARE(mode(), QString("off"));
        session->undo();
        QCOMPARE(mode(), QString("image"));
    }

    void bSkipsTheAutoPresetWatermarkIncluded() {
        Frame f;
        f.watermark = textMark();
        QVERIFY(presets().save("Sign", f));
        QVERIFY(presets().setAuto("Sign"));
        const QByteArray png = pngOf(shotOf(QSize(80, 60)));
        const int framed = stack().add(png, env->daemon().now(), {}, true);
        const int skipped = stack().add(png, env->daemon().now(), {}, false);
        QVERIFY(!stack().edited(framed).isEmpty());
        QCOMPARE(stack().autoFrame(framed)->watermark.mode, Watermark::Text);
        QVERIFY(stack().edited(skipped).isEmpty());
        QVERIFY(!stack().autoFrame(skipped));
    }

    // ---- the Editor: each change is one undo step ----

    void eachPanelChangeIsOneUndoStep() {
        QVERIFY(logo().save(logoFile()));
        const int id = stack().add(pngOf(shotOf()), env->daemon().now(), {}, false);
        auto *s = env->daemon().service<tarn::Editor>().open(id);
        QVERIFY(s);
        auto mark = [&] { return s->frameMap().value("watermark").toMap(); };
        QVERIFY(s->setWatermarkImage(logoFile()));
        QCOMPARE(mark().value("mode").toString(), QString("image"));
        s->setFrameValue("wmCorner", 0);
        QCOMPARE(mark().value("corner").toInt(), 0);
        s->dragFrameValue("wmSize", 0.2);
        s->dragFrameValue("wmSize", 0.15);
        s->endFrameDrag();
        QCOMPARE(mark().value("size").toDouble(), 0.15);
        s->setFrameValue("wmMode", "text");
        s->setFrameValue("wmText", "me");
        QCOMPARE(mark().value("text").toString(), QString("me"));
        s->undo();
        QCOMPARE(mark().value("text").toString(), QString());
        s->undo();
        QCOMPARE(mark().value("mode").toString(), QString("image"));
        s->undo();
        QCOMPARE(mark().value("size").toDouble(), 0.08);
        s->undo();
        QCOMPARE(mark().value("corner").toInt(), 3);
        s->undo();
        QCOMPARE(mark().value("mode").toString(), QString("off"));
        QVERIFY(!s->setWatermarkImage(env->scratch() + "/nope.png"));
        QCOMPARE(notifications(), 1);
    }

    // ---- the Project file ----

    void theProjectFileKeepsTheImageAfterTheFileIsDeleted() {
        QVERIFY(logo().save(logoFile()));
        CaptureDocument doc(shotOf());
        Frame f;
        f.watermark = imageMark();
        f.watermark.path = logoFile();
        f.watermark.opacity = 0.7;
        doc.setFrame(f);
        const QImage before = doc.flatten();
        const QByteArray json = tarn::projectToJson(doc);
        QVERIFY(QFile::remove(logoFile()));

        CaptureDocument again(shotOf());
        QVERIFY(tarn::loadProject(again, json).ok);
        QCOMPARE(again.frame().watermark.mode, Watermark::Image);
        QCOMPARE(again.frame().watermark.path, logoFile());
        QCOMPARE(again.frame().watermark.opacity, 0.7);
        QCOMPARE(again.flatten(), before);
        QVERIFY(again.isEdited());
    }
};

QTEST_MAIN(WatermarkTest)
#include "watermark_test.moc"
