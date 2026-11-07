// Callouts. A drag with the Counter tool pulls a text box out of a Counter (or out of
// empty picture, placing the Counter first) once it passes 24 px beyond the Counter's edge;
// the box follows its Counter; the leader is derived; the move and delete rules; the Project
// file round trip. Drawing is compared with stored reference PNGs (regenerate with
// TARN_UPDATE_REFERENCES=1 and ctest, then look at the images).
#include "test_env.h"

#include "annotations/callout.h"
#include "annotations/text_annotation.h"
#include "document/capture_document.h"
#include "document/project_file.h"
#include "editor/editor.h"
#include "editor/editor_session.h"
#include "theme.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QImage>
#include <QTest>
#include <cmath>

using tarn::CalloutAnnotation;
using tarn::EditorSession;
using tarn::test::TestEnv;

#ifndef TARN_CALLOUT_REFERENCE_DIR
#error "set by tests/callout/CMakeLists.txt"
#endif

namespace {

QImage testShot() {
    QImage image(240, 140, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb(40 + x / 3, 70 + y / 2, 130));
    return image;
}

QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

void compareWithReference(const QImage &actual, const QString &name) {
    const QString path = QStringLiteral(TARN_CALLOUT_REFERENCE_DIR "/") + name;
    if (qEnvironmentVariableIsSet("TARN_UPDATE_REFERENCES")) {
        QVERIFY(actual.save(path, "PNG"));
        return;
    }
    QImage expected(path);
    QVERIFY2(!expected.isNull(), qPrintable("missing reference " + path));
    QCOMPARE(actual.size(), expected.size());
    QCOMPARE(actual.convertToFormat(QImage::Format_ARGB32), expected.convertToFormat(QImage::Format_ARGB32));
}

} // namespace

class CalloutTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    EditorSession *session = nullptr;

    tarn::ThumbnailStack &stack() { return env->daemon().service<tarn::ThumbnailStack>(); }
    tarn::Editor &editor() { return env->daemon().service<tarn::Editor>(); }
    // The Counter tool at its smallest size: a Counter is 17.6 px across (radius 8.8).
    void openCounter() {
        session = editor().open(stack().add(pngOf(testShot()), QDateTime(QDate(2026, 11, 3), QTime(9, 30, 0))));
        session->setTool("counter");
        session->setSizeIndex(0);
    }
    void click(qreal x, qreal y) { dragTo(x, y, x, y); }
    void dragTo(qreal x0, qreal y0, qreal x1, qreal y1) {
        session->pointerPressed(x0, y0);
        session->pointerMoved((x0 + x1) / 2, (y0 + y1) / 2);
        session->pointerReleased(x1, y1);
    }
    // A Counter at (x, y) with a Callout pulled to (tx, ty) and `text` typed.
    void counterWithCallout(qreal x, qreal y, qreal tx, qreal ty, const QString &text) {
        dragTo(x, y, tx, ty);
        QVERIFY(session->textEditing());
        session->setText(text);
        session->finishText();
    }
    const CalloutAnnotation *calloutAt(int index = 0) const {
        int n = 0;
        for (const auto &item : session->document().items())
            if (auto *c = dynamic_cast<const CalloutAnnotation *>(item.annotation.get()))
                if (n++ == index)
                    return c;
        return nullptr;
    }
    int callouts() const {
        int n = 0;
        for (const auto &item : session->document().items())
            n += dynamic_cast<const CalloutAnnotation *>(item.annotation.get()) != nullptr;
        return n;
    }
    int idOf(const CalloutAnnotation *c) const {
        for (const auto &item : session->document().items())
            if (item.annotation.get() == c)
                return item.id;
        return -1;
    }
    QPointF counterCenter(int id) const { return session->document().annotation(id)->bounds().center(); }
    QImage image() {
        session->setSelection({});
        session->pointerLeft();
        return session->document().flatten();
    }

private slots:
    // Callout text is drawn in Tarn's bundled Geist, as in the Editor. Without loading it the
    // references were drawn in the machine's default font, so they differed on CI.
    void initTestCase() { tarn::loadFonts(); }
    void init() { env = std::make_unique<TestEnv>(); }

    void calloutTextIsGeistNotTheMachinesDefaultFont() {
        QCOMPARE(QFontInfo(tarn::TextAnnotation::fontFor(30)).family(), QString("Geist"));
    }
    void cleanup() { env.reset(); }

    void aClickStillPlacesACounter() {
        openCounter();
        click(40, 60);
        QCOMPARE(session->annotationCount(), 1);
        QVERIFY(!session->textEditing());
        click(80, 60);
        QCOMPARE(session->annotationCount(), 2);
        QCOMPARE(callouts(), 0);
    }

    void aDragFromACounterOpensACalloutPast24PxBeyondItsEdge() {
        openCounter();
        click(40, 60);
        const int counter = session->document().order().first();
        const qreal radius = session->document().annotation(counter)->bounds().width() / 2;
        // Just short of 24 px beyond the edge: nothing.
        dragTo(40, 60, 40 + radius + 23, 60);
        QVERIFY(!session->textEditing());
        QCOMPARE(session->annotationCount(), 1);
        // Just past it: a Callout opens, ready for text.
        dragTo(40, 60, 40 + radius + 25, 60);
        QVERIFY(session->textEditing());
        session->setText("Open the menu");
        session->finishText();
        QCOMPARE(callouts(), 1);
        QCOMPARE(calloutAt()->text(), QString("Open the menu"));
        QCOMPARE(calloutAt()->counterId(), counter);
        QCOMPARE(session->selection(), QList<int>{idOf(calloutAt())});
    }

    // The box hugs its text at any length: a short note gets a short box (story 69).
    void theBoxHugsShortText() {
        openCounter();
        counterWithCallout(40, 60, 140, 60, "1");
        click(40, 120);
        counterWithCallout(40, 120, 140, 120, "11");
        const QFontMetricsF fm(tarn::TextAnnotation::fontFor(calloutAt(0)->size()));
        // No minimum width: one more character widens the box by exactly that character.
        QCOMPARE(calloutAt(1)->boxRect().width() - calloutAt(0)->boxRect().width(), fm.horizontalAdvance("11") - fm.horizontalAdvance("1"));
    }

    void theBoxOpensAtThePointerAndGrowsToTheRight() {
        openCounter();
        click(40, 60);
        dragTo(40, 60, 120, 60);
        session->setText("Hi");
        session->finishText();
        // The empty box opens centered on the pointer, then grows to the right as you type
        // (like Text): its middle stays on the pointer's line, its left edge just before it.
        const QRectF box = calloutAt()->boxRect();
        QVERIFY(qAbs(box.center().y() - 60) < 1.0);
        QVERIFY2(box.left() < 120 && box.left() > 120 - calloutAt()->size(), qPrintable(QString::number(box.left())));
    }

    void aDragFromEmptyPicturePlacesTheCounterAndPullsItsCallout() {
        openCounter();
        dragTo(60, 40, 150, 90);
        QVERIFY(session->textEditing());
        QCOMPARE(session->annotationCount(), 0); // nothing is in the document until the text is done
        session->setText("Step one");
        session->finishText();
        QCOMPARE(session->annotationCount(), 2);
        QCOMPARE(callouts(), 1);
        QCOMPARE(counterCenter(calloutAt()->counterId()), QPointF(60, 40)); // the Counter sits where the press was
        // The Counter and its Callout are one undo step.
        session->undo();
        QCOMPARE(session->annotationCount(), 0);
        session->redo();
        QCOMPARE(session->annotationCount(), 2);
    }

    void aShortDragFromEmptyPicturePlacesOnlyTheCounter() {
        openCounter();
        dragTo(60, 40, 60 + 8.8 + 20, 40);
        QVERIFY(!session->textEditing());
        QCOMPARE(session->annotationCount(), 1);
        QCOMPARE(callouts(), 0);
    }

    void aNewBoxLeftEmptyMakesNoCalloutButKeepsItsCounter() {
        openCounter();
        dragTo(60, 40, 150, 90);
        session->finishText(); // nothing typed
        QCOMPARE(session->annotationCount(), 1);
        QCOMPARE(callouts(), 0);

        dragTo(60, 40, 150, 40); // from that Counter: the box opens, left empty, and vanishes
        QVERIFY(session->textEditing());
        session->finishText();
        QCOMPARE(callouts(), 0);
        QCOMPARE(session->annotationCount(), 1);
    }

    void eachCounterHasAtMostOneCallout() {
        openCounter();
        counterWithCallout(40, 60, 140, 60, "One");
        const int counter = calloutAt()->counterId();
        QCOMPARE(callouts(), 1);
        // A drag on a Counter that already has one moves the Counter instead.
        dragTo(40, 60, 40, 100);
        QCOMPARE(callouts(), 1);
        QVERIFY(!session->textEditing());
        QCOMPARE(counterCenter(counter), QPointF(40, 100));
    }

    void theBoxFollowsItsCountersColor() {
        openCounter();
        counterWithCallout(40, 60, 140, 60, "Blue");
        const int counter = calloutAt()->counterId();
        QCOMPARE(calloutAt()->color(), session->document().annotation(counter)->anchorColor());
        session->setSelection({counter});
        session->setColor(QColor("#30D158"));
        QCOMPARE(calloutAt()->color(), QColor("#30D158"));
        // Recoloring the box recolors its Counter, as one undo step.
        session->setSelection({idOf(calloutAt())});
        session->setColor(QColor("#0A84FF"));
        QCOMPARE(session->document().annotation(counter)->anchorColor(), QColor("#0A84FF"));
        QCOMPARE(calloutAt()->color(), QColor("#0A84FF"));
        session->undo();
        QCOMPARE(calloutAt()->color(), QColor("#30D158"));
    }

    void renumberingKeepsTheCalloutOnItsCounter() {
        openCounter();
        click(30, 20);
        counterWithCallout(30, 60, 130, 60, "Second");
        const int counter = calloutAt()->counterId();
        const QLineF before = calloutAt()->leader();
        session->setSelection({session->document().order().first()}); // Counter 1
        session->deleteSelection();                                    // the second Counter becomes number 1
        QCOMPARE(callouts(), 1);
        QCOMPARE(calloutAt()->counterId(), counter);
        QCOMPARE(calloutAt()->leader(), before);
    }

    void deletingTheCounterDeletesAllThreeAndUndoBringsThemBack() {
        openCounter();
        counterWithCallout(40, 60, 140, 60, "Gone");
        const int counter = calloutAt()->counterId();
        session->setSelection({counter});
        session->deleteSelection();
        QCOMPARE(session->annotationCount(), 0);
        session->undo();
        QCOMPARE(session->annotationCount(), 2);
        QCOMPARE(callouts(), 1);
        QCOMPARE(calloutAt()->text(), QString("Gone"));
    }

    void deletingTheBoxRemovesBoxAndLeaderOnly() {
        openCounter();
        counterWithCallout(40, 60, 140, 60, "Box");
        session->setSelection({idOf(calloutAt())});
        session->deleteSelection();
        QCOMPARE(session->annotationCount(), 1);
        QCOMPARE(callouts(), 0);
        // And the Counter can pull a new one.
        dragTo(40, 60, 140, 60);
        QVERIFY(session->textEditing());
    }

    void anEmptyBoxRemovesItself() {
        openCounter();
        counterWithCallout(40, 60, 140, 60, "Text");
        QVERIFY(session->editText(idOf(calloutAt())));
        QVERIFY(session->textEditing());
        QCOMPARE(session->text(), QString("Text"));
        session->setText("   ");
        session->finishText();
        QCOMPARE(callouts(), 0);
        QCOMPARE(session->annotationCount(), 1);
        session->undo(); // one step brings the box back
        QCOMPARE(callouts(), 1);
        QCOMPARE(calloutAt()->text(), QString("Text"));
    }

    void doubleClickAndEnterEditTheTextAgain() {
        openCounter();
        counterWithCallout(40, 60, 140, 60, "First");
        const QPointF inBox = calloutAt()->boxRect().center();
        session->setSelection({});
        QVERIFY(session->editTextAt(inBox.x(), inBox.y()));
        session->setText("Second");
        session->finishText();
        QCOMPARE(calloutAt()->text(), QString("Second"));
        session->undo();
        QCOMPARE(calloutAt()->text(), QString("First"));

        session->setSelection({idOf(calloutAt())});
        QVERIFY(session->editSelectedCallout());
        QCOMPARE(session->text(), QString("First"));
        session->setText("Third");
        session->finishText();
        QCOMPARE(calloutAt()->text(), QString("Third"));
        // Enter on something else is not an edit.
        session->setSelection({calloutAt()->counterId()});
        QVERIFY(!session->editSelectedCallout());
    }

    void draggingTheCounterMovesItAlone() {
        openCounter();
        counterWithCallout(40, 60, 140, 60, "A");
        const int counter = calloutAt()->counterId();
        const QPointF boxBefore = calloutAt()->boxRect().topLeft();
        const QLineF leaderBefore = calloutAt()->leader();
        dragTo(40, 60, 40, 100); // the Counter already has a Callout, so the drag moves it
        QCOMPARE(counterCenter(counter), QPointF(40, 100));
        QCOMPARE(calloutAt()->boxRect().topLeft(), boxBefore);
        QVERIFY(calloutAt()->leader() != leaderBefore); // redrawn from the Counter's new place
        QVERIFY(std::abs(QLineF(calloutAt()->leader().p1(), QPointF(40, 100)).length() - calloutAt()->counterRadius()) < 0.01);
        session->undo(); // one step
        QCOMPARE(counterCenter(counter), QPointF(40, 60));
    }

    void draggingTheBoxMovesItAlone() {
        openCounter();
        counterWithCallout(40, 60, 140, 60, "A");
        const int counter = calloutAt()->counterId();
        const QPointF boxBefore = calloutAt()->boxRect().topLeft();
        dragTo(140, 60, 160, 100);
        QCOMPARE(counterCenter(counter), QPointF(40, 60));
        QCOMPARE(calloutAt()->boxRect().topLeft(), boxBefore + QPointF(20, 40));
    }

    void draggingTheLeaderMovesAllThree() {
        openCounter();
        counterWithCallout(40, 60, 140, 60, "A");
        const int counter = calloutAt()->counterId();
        const QPointF boxBefore = calloutAt()->boxRect().topLeft();
        const QPointF mid = calloutAt()->leader().center();
        QCOMPARE(calloutAt()->partAt(mid, 1), CalloutAnnotation::Leader);
        dragTo(mid.x(), mid.y(), mid.x() + 10, mid.y() + 30);
        QCOMPARE(counterCenter(counter), QPointF(50, 90));
        QCOMPARE(calloutAt()->boxRect().topLeft(), boxBefore + QPointF(10, 30));
        QCOMPARE(session->selection(), QList<int>{idOf(calloutAt())});
        // All three moved in ONE undo step.
        session->undo();
        QCOMPARE(counterCenter(counter), QPointF(40, 60));
        QCOMPARE(calloutAt()->boxRect().topLeft(), boxBefore);
    }

    void theLeaderRunsFromTheCountersEdgeToTheBoxEdge() {
        // One Counter, a box in each of eight directions.
        auto counter = tarn::AnnotationKinds::find("counter")->fromDrag({QPointF(100, 70), QPointF(100, 70), {}, tarn::ToolStyle(), {}});
        for (const QPointF &place : {QPointF(200, 70), QPointF(190, 130), QPointF(100, 135), QPointF(10, 130), QPointF(5, 70), QPointF(10, 10),
                                     QPointF(100, 5), QPointF(190, 10)}) {
            CalloutAnnotation c(1, place, "Note");
            c.anchorTo(counter.get());
            const QLineF leader = c.leader();
            QVERIFY2(!leader.isNull(), qPrintable(QString("%1,%2").arg(place.x()).arg(place.y())));
            QVERIFY(std::abs(QLineF(c.counterCenter(), leader.p1()).length() - c.counterRadius()) < 1e-6); // starts on the Counter's edge
            const QRectF box = c.boxRect();
            const bool onEdge = std::abs(leader.p2().x() - box.left()) < 1e-6 || std::abs(leader.p2().x() - box.right()) < 1e-6 ||
                                std::abs(leader.p2().y() - box.top()) < 1e-6 || std::abs(leader.p2().y() - box.bottom()) < 1e-6;
            QVERIFY(onEdge);
            QVERIFY(box.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(leader.p2()));
            // The end lies on the line from the box center towards the Counter, and so does the start.
            const QLineF axis(box.center(), c.counterCenter());
            for (const QPointF &p : {leader.p1(), leader.p2()}) {
                const QPointF d = p - box.center();
                const QPointF a = axis.p2() - axis.p1();
                QVERIFY(std::abs(d.x() * a.y() - d.y() * a.x()) < 1e-6 * axis.length() * axis.length());
            }
        }
    }

    void noLeaderWhenTheBoxCoversTheCounter() {
        openCounter();
        counterWithCallout(40, 60, 100, 60, "Wide text here");
        QVERIFY(!calloutAt()->leader().isNull());
        const QPointF delta = QPointF(40, 60) - calloutAt()->boxRect().center();
        session->setSelection({idOf(calloutAt())});
        session->nudge(int(std::round(delta.x())), int(std::round(delta.y())));
        QVERIFY(calloutAt()->leader().isNull());
    }

    void leadersAgainstBoxEdgesMatchTheReference() {
        openCounter();
        counterWithCallout(120, 40, 200, 40, "Right");
        counterWithCallout(120, 100, 40, 100, "Left");
        compareWithReference(image(), "two-boxes.png");
    }

    void everyDirectionMatchesTheReference() {
        openCounter();
        counterWithCallout(60, 40, 170, 25, "Above right");
        counterWithCallout(60, 100, 150, 120, "Below");
        counterWithCallout(190, 70, 120, 70, "Between");
        compareWithReference(image(), "directions.png");
    }

    void aCalloutFromEmptyPictureMatchesTheReference() {
        openCounter();
        dragTo(50, 50, 140, 90);
        session->setText("Line one\nLine two");
        session->finishText();
        compareWithReference(image(), "from-empty-picture.png");
    }

    void theCalloutSurvivesTheProjectFile() {
        openCounter();
        click(20, 20);
        counterWithCallout(40, 60, 140, 70, "Kept\nsafe");
        counterWithCallout(100, 100, 30, 120, "Second");
        session->setSelection({});
        const QImage before = image();
        const QList<int> order = session->document().order();
        const QByteArray bytes = tarn::projectToJson(session->document());
        QVERIFY(bytes.contains("\"callout\""));

        tarn::CaptureDocument loaded(testShot());
        const auto result = tarn::loadProject(loaded, bytes);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(loaded.order(), order);
        QCOMPARE(loaded.flatten().convertToFormat(QImage::Format_ARGB32), before.convertToFormat(QImage::Format_ARGB32));
        // Still joined: removing its Counter takes the Callout with it.
        int counter = -1;
        for (const auto &item : loaded.items())
            if (auto *c = dynamic_cast<const CalloutAnnotation *>(item.annotation.get()); c && c->text() == "Kept\nsafe")
                counter = c->counterId();
        QVERIFY(counter > 0);
        const int before_count = loaded.count();
        loaded.remove({counter});
        QCOMPARE(loaded.count(), before_count - 2);
    }

    void aDamagedCalloutCountsAsUnreadable() {
        tarn::CaptureDocument doc(testShot());
        const QByteArray bytes = R"({"annotations":[{"id":1,"kind":"callout","data":{"counter":1,"text":"x"}}],"crop":{},"frame":{}})";
        QVERIFY(!tarn::loadProject(doc, bytes).ok);
    }

    void duplicatingACounterDoesNotDuplicateItsCallout() {
        openCounter();
        counterWithCallout(40, 60, 140, 60, "Once");
        session->setSelection({session->document().order().first(), idOf(calloutAt())});
        session->duplicateSelection();
        QCOMPARE(callouts(), 1);
        QCOMPARE(session->annotationCount(), 3);
    }
};

QTEST_MAIN(CalloutTest)
#include "callout_test.moc"
