// The Frame round a shot. Layout and rendering are tested on the Capture
// document and through the Editor session (what the Background panel calls).
//
// Every control has a reference PNG on TWO shot sizes (96x64 and 288x192: the
// large one is the small one times three), plus checks that the same Frame is
// proportionally the same on both. References live in tests/frame/reference/ and
// are compared exactly; regenerate with TARN_UPDATE_REFERENCES=1 (through ctest's
// environment, see docs/agents/verify.md) and look at the images first.
#include "test_env.h"

#include "annotations/image.h"
#include "document/capture_document.h"
#include "editor/editor.h"
#include "frame/frame.h"
#include "pin/pins.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QTest>
#include <cmath>

using tarn::CaptureDocument;
using tarn::EditorSession;
using tarn::Frame;
using tarn::FrameLayout;
using tarn::test::TestEnv;

Q_DECLARE_METATYPE(tarn::Frame)

#ifndef TARN_FRAME_REFERENCE_DIR
#error "TARN_FRAME_REFERENCE_DIR is set by tests/frame/CMakeLists.txt"
#endif

namespace {

const QSize kSmall(96, 64);
const QSize kLarge(288, 192);

// A shot whose picture is the same at any size: a gradient with a block at fixed fractions.
QImage shotOf(QSize size) {
    QImage image(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x) {
            const qreal fx = qreal(x) / size.width(), fy = qreal(y) / size.height();
            image.setPixel(x, y, qRgb(int(40 + 160 * fx), int(60 + 120 * fy), int(200 - 100 * fx * fy)));
        }
    for (int y = size.height() * 3 / 8; y < size.height() * 5 / 8; ++y)
        for (int x = size.width() / 4; x < size.width() / 2; ++x)
            image.setPixel(x, y, qRgb(250, 240, 90));
    return image;
}

QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

Frame plain(const QString &color = QStringLiteral("#ff2d6cdf")) {
    Frame f;
    f.background.kind = QStringLiteral("plain");
    f.background.params = {{QStringLiteral("color"), color}};
    return f;
}

// The controls under test: a name and a Frame that sets that control away from the defaults.
struct Case {
    const char *name;
    Frame frame;
};
QList<Case> cases() {
    QList<Case> list;
    list.append({"plain", plain()});
    {
        Frame f = plain();
        f.padding = 0.22;
        list.append({"padding", f});
    }
    {
        Frame f = plain();
        f.inset = 0.07;
        list.append({"inset", f});
    }
    {
        Frame f = plain();
        f.inset = 0.07;
        f.autoBalance = true;
        list.append({"auto-balance", f});
    }
    {
        Frame f = plain();
        f.shadow = 0.08;
        f.padding = 0.15;
        list.append({"shadow", f});
    }
    {
        Frame f = plain();
        f.corners = 0.12;
        list.append({"corners", f});
    }
    {
        Frame f = plain();
        f.ratio = QStringLiteral("1:1");
        f.alignH = -1;
        f.alignV = 1;
        list.append({"alignment", f});
    }
    {
        Frame f = plain();
        f.ratio = QStringLiteral("16:9");
        list.append({"ratio", f});
    }
    {
        Frame f = plain(QStringLiteral("#00000000")); // the transparent swatch
        f.shadow = 0.06;
        list.append({"transparent", f});
    }
    {
        Frame f = plain();
        f.padding = Frame::kMaxPadding; // about a shot of background on each side
        list.append({"padding-max", f});
    }
    {
        const char *v[] = {"top", "middle", "bottom"};
        const char *h[] = {"left", "center", "right"};
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) { // padding, no Ratio: the shot moves inside the padded image
                Frame f = plain();
                f.padding = 0.2;
                f.alignH = x - 1;
                f.alignV = y - 1;
                static QList<QByteArray> names; // the row names must outlive the list
                names.append(QByteArray("align-") + v[y] + "-" + h[x]);
                list.append({names.last().constData(), f});
            }
    }
    return list;
}

void compareWithReference(const QImage &actual, const QString &name) {
    const QString path = QStringLiteral(TARN_FRAME_REFERENCE_DIR "/") + name;
    if (qEnvironmentVariableIsSet("TARN_UPDATE_REFERENCES")) {
        QVERIFY(actual.save(path, "PNG"));
        return;
    }
    QImage expected(path);
    QVERIFY2(!expected.isNull(), qPrintable("missing reference " + path));
    QCOMPARE(actual.size(), expected.size());
    QCOMPARE(actual.convertToFormat(QImage::Format_ARGB32), expected.convertToFormat(QImage::Format_ARGB32));
}

QString nameOf(const QString &control, const char *size) { return QStringLiteral("%1-%2.png").arg(control, QLatin1String(size)); }

void fillCases() {
    QTest::addColumn<QString>("control");
    QTest::addColumn<Frame>("frame");
    for (const auto &c : cases())
        QTest::newRow(c.name) << QString::fromLatin1(c.name) << c.frame;
}

} // namespace

class FrameTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    QDateTime when{QDate(2026, 11, 3), QTime(9, 30, 0)};
    int shots = 0;

    tarn::ThumbnailStack &stack() { return env->daemon().service<tarn::ThumbnailStack>(); }
    tarn::Editor &editor() { return env->daemon().service<tarn::Editor>(); }
    int addShot() { return stack().add(pngOf(shotOf(kSmall)), when.addSecs(shots++)); }
    QString shared() const { return env->scratch() + "/cache/tarn/shared"; }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        shots = 0;
    }
    void cleanup() { env.reset(); }

    // ---- every control, on two shot sizes, against reference images ----

    void everyControlMatchesItsReferenceOnASmallShot_data() { fillCases(); }
    void everyControlMatchesItsReferenceOnASmallShot() {
        QFETCH(QString, control);
        QFETCH(Frame, frame);
        CaptureDocument doc(shotOf(kSmall));
        doc.setFrame(frame);
        compareWithReference(doc.flatten(), nameOf(control, "small"));
    }

    void everyControlMatchesItsReferenceOnALargeShot_data() { fillCases(); }
    void everyControlMatchesItsReferenceOnALargeShot() {
        QFETCH(QString, control);
        QFETCH(Frame, frame);
        CaptureDocument doc(shotOf(kLarge));
        doc.setFrame(frame);
        compareWithReference(doc.flatten(), nameOf(control, "large"));
    }

    // ---- the same Frame is proportionally the same on any shot ----

    void theLayoutScalesWithTheShot_data() { fillCases(); }
    void theLayoutScalesWithTheShot() {
        QFETCH(Frame, frame);
        const FrameLayout a = tarn::layoutFrame(kSmall, frame);
        const FrameLayout b = tarn::layoutFrame(kLarge, frame);
        auto near3x = [](int small, int large) { return std::abs(large - 3 * small) <= 3; }; // rounding of the pixel grid
        QVERIFY(near3x(a.size.width(), b.size.width()));
        QVERIFY(near3x(a.size.height(), b.size.height()));
        QVERIFY(near3x(a.card.x(), b.card.x()));
        QVERIFY(near3x(a.card.y(), b.card.y()));
        QVERIFY(near3x(a.content.x(), b.content.x()));
        QVERIFY(near3x(a.content.y(), b.content.y()));
        QVERIFY(std::abs(b.cardRadius - 3 * a.cardRadius) < 0.5);
        QVERIFY(std::abs(b.shadowBlur - 3 * a.shadowBlur) < 0.5);
    }

    void theLargeFrameDownscaledLooksLikeTheSmallOne_data() { fillCases(); }
    void theLargeFrameDownscaledLooksLikeTheSmallOne() {
        QFETCH(Frame, frame);
        CaptureDocument small(shotOf(kSmall)), large(shotOf(kLarge));
        small.setFrame(frame);
        large.setFrame(frame);
        const QImage s = small.flatten().convertToFormat(QImage::Format_ARGB32);
        const QImage l = large.flatten().scaled(small.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
        QCOMPARE(l.size(), s.size());
        qint64 total = 0;
        for (int y = 0; y < s.height(); ++y)
            for (int x = 0; x < s.width(); ++x)
                for (int shift : {0, 8, 16, 24})
                    total += std::abs(int((s.pixel(x, y) >> shift) & 0xff) - int((l.pixel(x, y) >> shift) & 0xff));
        const double mean = double(total) / (s.width() * s.height() * 4);
        QVERIFY2(mean < 6.0, qPrintable(QString::number(mean)));
    }

    void anyShotShapeKeepsTheFractionsOfItsShorterSide() {
        Frame f = plain();
        f.padding = 0.1;
        const FrameLayout wide = tarn::layoutFrame(QSize(400, 100), f);
        QCOMPARE(wide.content.x(), 10); // 0.1 of the shorter side (100), on every side
        QCOMPARE(wide.content.y(), 10);
        QCOMPARE(wide.size, QSize(420, 120));
        const FrameLayout tall = tarn::layoutFrame(QSize(100, 400), f);
        QCOMPARE(tall.content.topLeft(), QPoint(10, 10));
    }

    // ---- what each control does to the geometry ----

    void noBackgroundMeansNoFrameAtAll() {
        CaptureDocument doc(shotOf(kSmall));
        QCOMPARE(doc.size(), kSmall);
        QCOMPARE(doc.flatten(), shotOf(kSmall));
        Frame f = plain();
        doc.setFrame(f);
        QVERIFY(doc.size() != kSmall);
        f.background.kind = QStringLiteral("none");
        doc.setFrame(f);
        QCOMPARE(doc.flatten(), shotOf(kSmall));
    }

    void paddingAddsAMarginOnEverySide() {
        Frame f = plain();
        f.padding = 0.25;
        const FrameLayout L = tarn::layoutFrame(QSize(100, 80), f);
        QCOMPARE(L.card, QRect(20, 20, 100, 80));
        QCOMPARE(L.size, QSize(140, 120));
    }

    void insetGrowsTheCardAroundTheShot() {
        Frame f = plain();
        f.padding = 0;
        f.inset = 0.1;
        const FrameLayout L = tarn::layoutFrame(QSize(100, 80), f);
        QCOMPARE(L.content, QRect(8, 8, 100, 80));
        QCOMPARE(L.card, QRect(0, 0, 116, 96));
    }

    void ratioGrowsTheImageNeverCropsIt() {
        Frame f = plain();
        f.padding = 0;
        f.ratio = QStringLiteral("1:1");
        QCOMPARE(tarn::layoutFrame(QSize(100, 60), f).size, QSize(100, 100));
        f.ratio = QStringLiteral("16:9");
        QCOMPARE(tarn::layoutFrame(QSize(100, 100), f).size, QSize(178, 100));
        f.ratio = QStringLiteral("9:16");
        QCOMPARE(tarn::layoutFrame(QSize(100, 100), f).size, QSize(100, 178));
        f.ratio = QStringLiteral("5:2"); // a custom ratio
        QCOMPARE(tarn::layoutFrame(QSize(100, 100), f).size, QSize(250, 100));
        f.ratio = QStringLiteral("nonsense");
        QCOMPARE(tarn::layoutFrame(QSize(100, 100), f).size, QSize(100, 100));
    }

    void paddingGoesUpToAboutAShotOnEverySide() {
        Frame f = plain();
        f.padding = Frame::kMaxPadding;
        const FrameLayout L = tarn::layoutFrame(QSize(100, 80), f);
        QCOMPARE(L.card, QRect(80, 80, 100, 80)); // 1 x the shorter side all round
        QCOMPARE(L.size, QSize(260, 240));
    }

    void alignmentMovesTheShotWithPaddingAndNoRatio() {
        Frame f = plain();
        f.padding = 0.25; // 20 px on the 80 px side
        const QSize shot(100, 80);
        const QSize size = tarn::layoutFrame(shot, f).size;
        QCOMPARE(size, QSize(140, 120));
        for (int ah = -1; ah <= 1; ++ah)
            for (int av = -1; av <= 1; ++av) {
                f.alignH = ah;
                f.alignV = av;
                const FrameLayout L = tarn::layoutFrame(shot, f);
                QCOMPARE(L.size, size); // the image size never changes
                QCOMPARE(L.card.topLeft(), QPoint(20 * (1 + ah), 20 * (1 + av))); // aligned side 0, opposite 40
            }
    }

    void presetsSavedWithTheOldSmallerRangeStillLoad() {
        const Frame f = Frame::fromMap({{"background", QVariantMap{{"kind", "plain"}}}, {"padding", 0.22}});
        QCOMPARE(f.padding, 0.22);
        QCOMPARE(Frame::fromMap({{"background", QVariantMap{{"kind", "plain"}}}, {"padding", 7}}).padding, Frame::kMaxPadding);
    }

    void alignmentPlacesTheCardInTheSpareRoom() {
        Frame f = plain();
        f.padding = 0;
        f.ratio = QStringLiteral("1:1");
        const QSize shot(100, 60);
        QCOMPARE(tarn::layoutFrame(shot, f).card.topLeft(), QPoint(0, 20));
        f.alignV = -1;
        QCOMPARE(tarn::layoutFrame(shot, f).card.topLeft(), QPoint(0, 0));
        f.alignV = 1;
        QCOMPARE(tarn::layoutFrame(shot, f).card.topLeft(), QPoint(0, 40));
        f.ratio = QStringLiteral("16:9");
        f.alignH = 1;
        const FrameLayout wide = tarn::layoutFrame(QSize(60, 100), f);
        QCOMPARE(wide.card.topLeft(), QPoint(wide.size.width() - 60, 0));
    }

    void shadowCornersAndPaddingChangeThePixels() {
        auto frameWith = [](qreal shadow, qreal corners, qreal padding) {
            Frame f = plain();
            f.shadow = shadow;
            f.corners = corners;
            f.padding = padding;
            return f;
        };
        auto render = [&](const Frame &f) {
            CaptureDocument doc(shotOf(kLarge));
            doc.setFrame(f);
            return doc.flatten();
        };
        const QImage base = render(frameWith(0, 0, 0.1));
        QVERIFY(render(frameWith(0.06, 0, 0.1)) != base);
        QVERIFY(render(frameWith(0, 0.1, 0.1)) != base);
        QVERIFY(render(frameWith(0, 0, 0.2)).size() != base.size());
        // A rounded corner shows the background where the shot's corner was.
        const Frame round = frameWith(0, 0.12, 0.1);
        const QPoint corner = tarn::layoutFrame(kLarge, round).content.topLeft();
        QCOMPARE(render(round).pixel(corner), render(round).pixel(0, 0));
        QVERIFY(base.pixel(corner) != base.pixel(0, 0));
    }

    void aTransparentBackgroundExportsWithAnAlphaChannel() {
        CaptureDocument doc(shotOf(kSmall));
        doc.setFrame(plain(QStringLiteral("#00000000")));
        const QImage out = doc.flatten();
        QVERIFY(out.hasAlphaChannel());
        QCOMPARE(qAlpha(out.pixel(0, 0)), 0);
        CaptureDocument solid(shotOf(kSmall));
        solid.setFrame(plain());
        QVERIFY(!solid.flatten().hasAlphaChannel());
    }

    // ---- the Frame wraps the content; the content stays in its own coordinates ----

    void annotationsAreNotClippedToTheShotTheyPaintOverTheFrame() { // (was: clipped to the shot)
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        s->setFrameValue("shadow", 0);
        s->setFrameValue("corners", 0);
        // A rectangle whose stroke straddles the shot's edge (6 px wide, 3 px of it outside).
        s->pointerPressed(0, 0);
        s->pointerMoved(96, 64);
        s->pointerReleased(96, 64);
        QCOMPARE(s->annotationCount(), 1);
        const QImage out = s->document().flatten();
        const QPoint at = s->document().contentOffset();
        QVERIFY(at.x() > 3);
        QVERIFY(out.pixel(at.x() - 1, at.y() + 30) != qRgb(0x2d, 0x6c, 0xdf)); // the stroke reaches the Frame
        QCOMPARE(out.pixel(at.x() - 5, at.y() + 30), qRgb(0x2d, 0x6c, 0xdf));  // but no further than it is wide
        QVERIFY(out.pixel(at.x() + 1, at.y() + 30) != shotOf(kSmall).pixel(1, 30)); // and the stroke inside
    }

    void pointerCoordinatesStayInTheContentsOwnPixels() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->setViewportSize(1000, 800);
        const QPointF before = s->docToView(10, 10);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        s->setFrameValue("ratio", "16:9"); // spare room, card pushed to the left
        s->setFrameValue("ah", -1);
        const QPointF after = s->docToView(10, 10);
        QVERIFY(after != before);
        const QPointF back = s->viewToDoc(after.x(), after.y());
        QVERIFY(std::abs(back.x() - 10) < 1e-6 && std::abs(back.y() - 10) < 1e-6);
        // The content's top left in the view is the framed image's origin plus the Frame's offset.
        QCOMPARE(s->docToView(0, 0).x(), s->origin().x() + s->document().contentOffset().x() * s->zoom());
    }

    void oneViewToDocMappingThroughGrowthCropTurnAndFrame() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->setViewportSize(1000, 800);
        QImage pic(20, 20, QImage::Format_ARGB32);
        pic.fill(Qt::white);
        QVERIFY(s->addImage(pic, QPointF(0, 30))); // centered on the left edge: the canvas grows to a negative x
        QVERIFY(s->document().canvas().x() < 0);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        s->enterCrop();
        s->rotateCrop(1);
        s->leaveCrop();
        QCOMPARE(s->document().crop().quarterTurns, 1);
        for (const QPointF p : {QPointF(-5, 30), QPointF(10, 10), QPointF(40, 20)}) {
            const QPointF v = s->docToView(p.x(), p.y());
            const QPointF back = s->viewToDoc(v.x(), v.y());
            QVERIFY2(std::abs(back.x() - p.x()) < 1e-6 && std::abs(back.y() - p.y()) < 1e-6, "view <-> doc must round-trip");
        }
        // The canvas's top-left corner lands at the content's own corner after the turn, offset by the Frame.
        const QPointF corner = s->docToView(s->document().canvas().x(), s->document().canvas().bottom() + 1);
        QCOMPARE(corner.x(), s->origin().x() + s->document().contentOffset().x() * s->zoom());
        QCOMPARE(corner.y(), s->origin().y() + s->document().contentOffset().y() * s->zoom());
    }

    // ---- undo and redo ----

    void aBackgroundChangeUndoesAndRedoes() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        QVERIFY(!s->document().frame().active());
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        QVERIFY(s->frameMap().value("on").toBool());
        QVERIFY(s->documentSize() != kSmall);
        s->undo();
        QVERIFY(!s->frameMap().value("on").toBool());
        QCOMPARE(s->documentSize(), kSmall);
        s->redo();
        QVERIFY(s->frameMap().value("on").toBool());
    }

    void everyControlUndoesAndRedoes() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        const QList<std::pair<QString, QVariant>> changes = {{"padding", 0.2}, {"inset", 0.05}, {"autoBalance", true}, {"shadow", 0.07},
                                                              {"corners", 0.1}, {"ah", -1}, {"av", 1}, {"ratio", "1:1"}};
        for (const auto &[key, value] : changes) {
            const QVariant before = s->frameMap().value(key);
            s->setFrameValue(key, value);
            QCOMPARE(s->frameMap().value(key), value);
            s->undo();
            QCOMPARE(s->frameMap().value(key), before);
            s->redo();
            QCOMPARE(s->frameMap().value(key), value);
        }
    }

    void aSliderDragIsOneUndoStep() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        const qreal start = s->frameMap().value("padding").toDouble();
        for (qreal v : {0.05, 0.1, 0.15, 0.2})
            s->dragFrameValue("padding", v);
        s->endFrameDrag();
        QCOMPARE(s->frameMap().value("padding").toDouble(), 0.2);
        QCOMPARE(s->document().history().undoCount(), 2); // the background and the whole drag
        s->undo();
        QCOMPARE(s->frameMap().value("padding").toDouble(), start);
    }

    void frameAndAnnotationStepsUndoInOrder() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->pointerPressed(10, 10);
        s->pointerMoved(40, 30);
        s->pointerReleased(40, 30);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        s->undo();
        QCOMPARE(s->annotationCount(), 1);
        QVERIFY(!s->frameMap().value("on").toBool());
        s->undo();
        QCOMPARE(s->annotationCount(), 0);
    }

    void undoAfterSettingNoneBringsTheFrameBack() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        s->setFrameValue("padding", 0.2);
        s->setFrameBackground("none");
        QCOMPARE(s->documentSize(), kSmall);
        s->undo();
        QCOMPARE(s->frameMap().value("padding").toDouble(), 0.2);
        QVERIFY(s->frameMap().value("on").toBool());
    }

    // ---- the Frame is in Copy, Save and Pin ----

    void copyIncludesTheFrame() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        const QSize framed = s->documentSize();
        QVERIFY(s->copyAndClose());
        const QImage copied(shared() + "/2026-11-03_09-30-00.png");
        QCOMPARE(copied.size(), framed);
        QVERIFY(framed != kSmall);
    }

    void saveIncludesTheFrame() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        s->setFrameValue("ratio", "1:1");
        QVERIFY(s->saveAndClose());
        const QImage saved(env->screenshots() + "/2026-11-03_09-30-00.png");
        QVERIFY(!saved.isNull());
        QCOMPARE(saved.width(), saved.height());
    }

    void pinIncludesTheFrame() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        const QSize framed = s->documentSize();
        QVERIFY(s->pin());
        auto &pins = env->daemon().service<tarn::Pins>();
        QCOMPARE(pins.ids().size(), 1);
        QCOMPARE(QImage::fromData(pins.png(pins.ids().first())).size(), framed);
    }

    void doneKeepsTheFrameOnTheThumbnailAndReopeningKeepsItEditable() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        const QSize framed = s->documentSize();
        s->done();
        QCOMPARE(QImage::fromData(stack().edited(id)).size(), framed);
        EditorSession *again = editor().open(id);
        QVERIFY(again->frameMap().value("on").toBool()); // the Frame is part of the document, still adjustable
    }

    void aFrameAloneCountsAsAnEditAndRemovingItClearsTheEdit() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        s->setFrameBackground("none");
        s->done();
        QVERIFY(stack().edited(id).isEmpty());
    }

    // ---- one geometry: grown canvas + Spotlight dim + Frame ----

    void anImagePastTheEdgeASpotlightAndAFrameShareOneGeometry() {
        CaptureDocument doc(shotOf(kSmall));
        QImage pic(20, 20, QImage::Format_ARGB32);
        pic.fill(QColor(255, 255, 255));
        doc.add(tarn::makeImageAnnotation(pic, QRectF(-10, 30, 20, 20))); // 10 px past the left edge
        doc.draw(QStringLiteral("spotlight"), tarn::DragGesture{{50, 10}, {70, 25}, {}, tarn::ToolStyle{}});
        Frame f = plain();
        f.padding = 0.15; // of the shorter side of the content (64): 10 px
        f.shadow = 0;
        f.corners = 0;
        doc.setFrame(f);

        QCOMPARE(doc.canvas(), QRect(-10, 0, 106, 64));
        QCOMPARE(doc.contentSize(), QSize(106, 64));
        QCOMPARE(doc.dimArea(), QRectF(-10, 0, 106, 64)); // the content, never the Frame
        const QImage out = doc.flatten();
        QCOMPARE(out.size(), QSize(126, 84));
        // Document (x, y) lands at (x + 10 + 10, y + 10): canvas origin -10, Frame padding 10.
        const QRgb frameColor = qRgb(0x2d, 0x6c, 0xdf);
        QCOMPARE(out.pixel(2, 2) | 0xFF000000u, frameColor);          // Frame untouched by the dim
        QCOMPARE(out.pixel(124, 82) | 0xFF000000u, frameColor);
        QCOMPARE(out.pixel(10 + 5, 10 + 40) | 0xFF000000u, qRgb(255, 255, 255)); // the image, in the grown area
        const QRgb inside = shotOf(kSmall).pixel(60, 18);              // inside the Spotlight: bright
        QCOMPARE(out.pixel(60 + 20, 18 + 10) | 0xFF000000u, inside | 0xFF000000u);
        const QRgb outside = out.pixel(20 + 20, 10 + 55);              // shot outside the Spotlight: dimmed
        const int keep = 255 - CaptureDocument::kDimAlpha;
        const QRgb s = shotOf(kSmall).pixel(20, 55);
        QVERIFY(qAbs(qRed(outside) - qRed(s) * keep / 255) <= 2);
        // The view mapping is the inverse of the paint mapping, through both offsets.
        compareWithReference(out, QStringLiteral("grown-spotlight-frame.png"));
    }

    void theSameWithACropARotationAndAnOutputScaleIsOneChain() {
        // canvas (grown image) -> Crop (cut + turn) -> content -> Frame wraps it -> output scale on the whole.
        CaptureDocument doc(shotOf(kSmall));
        QImage pic(20, 20, QImage::Format_ARGB32);
        pic.fill(QColor(255, 255, 255));
        doc.add(tarn::makeImageAnnotation(pic, QRectF(-10, 30, 20, 20)));
        doc.draw(QStringLiteral("spotlight"), tarn::DragGesture{{50, 10}, {70, 25}, {}, tarn::ToolStyle{}});
        Frame f = plain();
        f.padding = 0.15;
        f.shadow = 0;
        f.corners = 0;
        doc.setFrame(f);
        tarn::Crop crop;
        crop.rect = QRect(-10, 0, 80, 64); // the grown strip plus the shot up to x = 70 (canvas px)
        crop.quarterTurns = 1;             // clockwise: the content is 64 x 80
        crop.scale = 2;
        QVERIFY(doc.setCrop(crop));

        QCOMPARE(doc.canvasRect(), QRect(-10, 0, 106, 64));
        QCOMPARE(doc.contentSize(), QSize(64, 80));
        QCOMPARE(doc.frameLayout().size, QSize(84, 100)); // padding 10 on the content's shorter side (64)
        QCOMPARE(doc.size(), QSize(168, 200));            // the scale covers the Frame too
        const QImage out = doc.flatten();
        QCOMPARE(out.size(), QSize(168, 200));
        // canvas (x, y) -> content (64 - y, x + 10) -> view (+10, +10) -> output (x2)
        auto at = [&](qreal x, qreal y) { return out.pixel(int((64 - y + 10) * 2) + 1, int((x + 10 + 10) * 2) + 1) | 0xFF000000u; };
        const QRgb frameColor = qRgb(0x2d, 0x6c, 0xdf);
        QCOMPARE(out.pixel(3, 3) | 0xFF000000u, frameColor);
        QCOMPARE(out.pixel(165, 197) | 0xFF000000u, frameColor);
        QCOMPARE(at(-5, 40), qRgb(255, 255, 255));                              // the image, in the grown strip
        QVERIFY(qAbs(qRed(at(60, 18)) - qRed(shotOf(kSmall).pixel(60, 18))) <= 3); // inside the Spotlight: bright (scaled, so not exact)
        const int keep = 255 - CaptureDocument::kDimAlpha;
        QVERIFY(qAbs(qRed(at(20, 55)) - qRed(shotOf(kSmall).pixel(20, 55)) * keep / 255) <= 3); // outside: dimmed
        compareWithReference(out, QStringLiteral("grown-spotlight-frame-crop-turn-x2.png"));
    }

    // ---- the value ----

    void aFrameRoundTripsThroughItsMap() {
        Frame f = plain();
        f.padding = 0.2;
        f.inset = 0.05;
        f.autoBalance = true;
        f.alignH = 1;
        f.ratio = QStringLiteral("5:2");
        QVERIFY(Frame::fromMap(f.toMap()) == f);
    }

    void aBackgroundKindIsPluggable() {
        QVERIFY(tarn::BackgroundKinds::find("plain"));
        // Gradient, wallpaper and blur each add one file.
        for (const char *kind : {"gradient", "wallpaper", "blur"})
            QVERIFY(tarn::BackgroundKinds::find(kind));
        QVERIFY(!tarn::BackgroundKinds::find("no-such-kind"));
    }
};

QTEST_MAIN(FrameTest)
#include "frame_test.moc"
