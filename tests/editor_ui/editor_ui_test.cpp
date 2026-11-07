// The Editor window (QML, compiled in), driven with the same mouse and key
// events a user makes, off-screen. What this cannot check is needs-eyes: the
// real compositor (floating, centered, no animation, dragging by a bar) and how
// it looks next to the prototype.
#include "hyprland_input.h"
#include "test_env.h"

#include <algorithm>

#include "editor/editor.h"
#include "editor_windows.h"
#include "palette/palette.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QQuickItem>
#include <QRegularExpression>
#include <QQuickView>
#include <QTest>

using tarn::test::TestEnv;

namespace {

QQuickItem *findItem(QQuickItem *from, const QString &objectName) {
    if (from->objectName() == objectName)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = findItem(child, objectName))
            return hit;
    return nullptr;
}

QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QImage shot(int w = 400, int h = 300) {
    QImage image(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            image.setPixel(x, y, qRgb(20 + x % 200, 40 + y % 180, 150));
    return image;
}

struct Fixture {
    TestEnv env;
    tarn::ThumbnailStack &stack = env.daemon().service<tarn::ThumbnailStack>();
    tarn::Editor &editor = env.daemon().service<tarn::Editor>();
    tarn::EditorWindows windows{editor, env.daemon().service<tarn::Palette>()};
    int id = stack.add(pngOf(shot()), env.daemon().now());

    // Opens the Editor and waits until its window is up and laid out.
    QQuickView *open() {
        stack.edit(id);
        QQuickView *view = windows.viewFor(id);
        if (!view || view->status() != QQuickView::Ready || !QTest::qWaitForWindowExposed(view))
            return nullptr;
        QTest::qWait(50);
        return view;
    }
    static QPoint centerOf(QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }
    static void click(QQuickView *view, QQuickItem *item) { QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(item)); }
    // Drags with the left button between two points of the canvas, in canvas pixels.
    static void drag(QQuickView *view, QPoint from, QPoint to) {
        QTest::mousePress(view, Qt::LeftButton, {}, from);
        QTest::mouseMove(view, (from + to) / 2);
        QTest::mouseMove(view, to);
        QTest::mouseRelease(view, Qt::LeftButton, {}, to);
    }
};

} // namespace

class EditorUiTest : public QObject {
    Q_OBJECT
private slots:
    void theWindowIsFramelessTitledForTheWindowRuleAndSizedBeforeItShows() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        // tarn.lua's window rule (float, center, no animation) matches ^(Tarn editor.*)$; each
        // Editor adds its own number so Hyprland can focus that one.
        QVERIFY2(QRegularExpression("^(Tarn editor.*)$").match(view->title()).hasMatch(), qPrintable(view->title()));
        QCOMPARE(view->title(), f.editor.session(f.id)->windowTitle());
        QVERIFY(view->flags() & Qt::FramelessWindowHint);
        QCOMPARE(view->size(), f.editor.session(f.id)->openingSize()); // 2560 x 1440 monitor: 1280 x 806
        QCOMPARE(view->size(), QSize(1280, 806));
    }

    void theBarsAre52PxAndTheCanvasFillsBetween() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        auto *top = findItem(root, "topBar"), *bottom = findItem(root, "bottomBar"), *canvas = findItem(root, "canvas");
        QCOMPARE(top->height(), 52.0);
        QCOMPARE(bottom->height(), 52.0);
        QCOMPARE(top->mapToScene(QPointF(0, 0)).y(), 0.0);
        QCOMPARE(bottom->mapToScene(QPointF(0, 0)).y(), 806.0 - 52);
        QCOMPARE(canvas->height(), 806.0 - 104);
        QCOMPARE(canvas->width(), 1280.0);
    }

    void theBarsHoldTheControlsOfTheFirstPath() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        for (const char *name : {"closeButton", "toolCapsule", "tool-rect", "saveAsButton", "doneButton", "zoomMenu", "copyButton"})
            QVERIFY2(findItem(root, name), name);
        QCOMPARE(findItem(root, "tool-rect")->property("active").toBool(), true); // opens on Rectangle
    }

    void theShotSitsCenteredWithNoHairlineAndTheMarginsAreAtLeast40() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        QVERIFY(!findItem(view->rootObject(), "hairline")); // no outline round the shot
        auto *canvas = findItem(view->rootObject(), "canvas");
        // 400 x 300 at 100%, 1280 x 702 canvas: centered.
        const QPointF origin = f.editor.session(f.id)->origin();
        QCOMPARE(origin.x(), (1280 - 400) / 2.0);
        QVERIFY(origin.x() >= 40 && origin.y() >= 40);
        QVERIFY(canvas->width() - (origin.x() + 400) >= 40);
    }

    void aBigShotOpensAtFitWithAtLeast40pxOfMargin() {
        TestEnv env;
        auto &stack = env.daemon().service<tarn::ThumbnailStack>();
        tarn::EditorWindows windows(env.daemon().service<tarn::Editor>(), env.daemon().service<tarn::Palette>());
        const int id = stack.add(pngOf(shot(3000, 1500)), env.daemon().now());
        stack.edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        auto *session = env.daemon().service<tarn::Editor>().session(id);
        QVERIFY(session->fit());
        QVERIFY(session->zoom() < 1.0);
        auto *canvas = findItem(view->rootObject(), "canvas");
        const QPointF origin = session->origin();
        const QSizeF shown = QSizeF(session->documentSize()) * session->zoom();
        QVERIFY(origin.x() >= 40 - 1 && origin.y() >= 40 - 1);
        QVERIFY(canvas->width() - (origin.x() + shown.width()) >= 40 - 1);
        QVERIFY(canvas->height() - (origin.y() + shown.height()) >= 40 - 1);
    }

    void dragWithTheMouseDrawsARectangleAndDoneReturnsToTheThumbnail() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *canvas = findItem(view->rootObject(), "canvas");
        const QPoint a = canvas->mapToScene(QPointF(500, 200)).toPoint();
        Fixture::drag(view, a, a + QPoint(120, 80));
        auto *session = f.editor.session(f.id);
        QCOMPARE(session->annotationCount(), 1);
        Fixture::click(view, findItem(view->rootObject(), "doneButton"));
        QVERIFY(!f.editor.session(f.id));
        QVERIFY(!f.stack.isHidden(f.id));
        QVERIFY(!f.stack.edited(f.id).isEmpty());
        // reopened: still editable
        f.stack.edit(f.id);
        QCOMPARE(f.editor.session(f.id)->annotationCount(), 1);
    }

    void theCanvasPaintsWhatTheExportWrites() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        session->setTool("rect");
        session->pointerPressed(40, 30);
        session->pointerReleased(250, 180);
        session->deselect(); // a drawn Annotation is selected, and one under the pointer shows its ghost:
        session->pointerLeft(); // Handles are chrome on top of the picture, not part of it
        QTest::qWait(100);
        // At Fit (= 100%) the shot sits at origin() on the canvas, 1:1.
        const QImage screen = view->grabWindow();
        QVERIFY(!screen.isNull());
        auto *canvas = findItem(view->rootObject(), "canvas");
        const QPoint at = (canvas->mapToScene(QPointF(0, 0)) + session->origin()).toPoint();
        const QImage painted = screen.copy(QRect(at, session->documentSize())).convertToFormat(QImage::Format_ARGB32);
        QCOMPARE(painted, session->document().flatten().convertToFormat(QImage::Format_ARGB32));
    }

    void aWindowClosedByTheCompositorActsLikeDoneAndTheNextEditOpensAFreshWindow() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        f.editor.session(f.id)->pointerPressed(10, 10);
        f.editor.session(f.id)->pointerReleased(80, 80);
        QCOMPARE(f.editor.session(f.id)->annotationCount(), 1);
        view->close(); // Hyprland's Super+Q: the window is hidden, Tarn did not ask
        QVERIFY(!f.editor.session(f.id)); // released
        QVERIFY(!f.stack.isHidden(f.id));
        QVERIFY(!f.stack.edited(f.id).isEmpty()); // the edits are kept
        f.stack.edit(f.id);
        QQuickView *again = f.windows.viewFor(f.id);
        QVERIFY(again && again->isVisible());
        QCOMPARE(f.editor.session(f.id)->annotationCount(), 1); // still re-editable
    }

    void escapeClosesTheEditorWithoutAPromptAndKeepsTheThumbnail() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        f.editor.session(f.id)->pointerPressed(10, 10);
        f.editor.session(f.id)->pointerReleased(80, 80);
        QTest::keyClick(view, Qt::Key_Escape); // first step back: deselects the new Rectangle
        QVERIFY(f.editor.session(f.id));
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(!f.editor.session(f.id));
        QVERIFY(!f.stack.isHidden(f.id));
        QVERIFY(f.stack.edited(f.id).isEmpty());
        QTRY_VERIFY(!f.windows.viewFor(f.id));
    }

    void enterAndCtrlEnterAreDone() {
        for (auto mods : {Qt::KeyboardModifiers(), Qt::KeyboardModifiers(Qt::ControlModifier)}) {
            Fixture f;
            QQuickView *view = f.open();
            QVERIFY(view);
            QTest::keyClick(view, Qt::Key_Return, mods);
            QVERIFY(!f.editor.session(f.id));
            QVERIFY(!f.stack.isHidden(f.id));
        }
    }

    void copyButtonAndCtrlCCopyAndCloseBoth() {
        {
            Fixture f;
            QQuickView *view = f.open();
            QVERIFY(view);
            Fixture::click(view, findItem(view->rootObject(), "copyButton"));
            QCOMPARE(f.env.calls("tarn-clip").size(), 1);
            QCOMPARE(f.stack.totalCount(), 0);
            QVERIFY(!f.editor.session(f.id));
        }
        {
            Fixture f;
            QQuickView *view = f.open();
            QVERIFY(view);
            QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
            QCOMPARE(f.env.calls("tarn-clip").size(), 1);
            QCOMPARE(f.stack.totalCount(), 0);
        }
    }

    void ctrlShiftCCopiesAndKeepsTheEditorOpen() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(f.env.calls("tarn-clip").size(), 1);
        QVERIFY(f.editor.session(f.id));
        QVERIFY(view->isVisible());
    }

    void ctrlSSavesAndClosesBoth() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        QTest::keyClick(view, Qt::Key_S, Qt::ControlModifier);
        QCOMPARE(TestEnv::filesIn(f.env.screenshots()).size(), 1);
        QCOMPARE(f.stack.totalCount(), 0);
        QVERIFY(!f.editor.session(f.id));
    }

    void zoomKeysAndMenu() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        QTest::keyClick(view, Qt::Key_Equal, Qt::ControlModifier);
        QCOMPARE(session->zoomPercent(), 200);
        QTest::keyClick(view, Qt::Key_Minus, Qt::ControlModifier);
        QCOMPARE(session->zoomPercent(), 100);
        QTest::keyClick(view, Qt::Key_Minus, Qt::ControlModifier);
        QCOMPARE(session->zoomPercent(), 50);
        QTest::keyClick(view, Qt::Key_1, Qt::ControlModifier);
        QCOMPARE(session->zoomPercent(), 100);
        QTest::keyClick(view, Qt::Key_0, Qt::ControlModifier);
        QVERIFY(session->fit());
        // the menu's label says the zoom
        QCOMPARE(findItem(findItem(view->rootObject(), "zoomMenu"), "label")->property("text").toString(), QString("Fit · 100%"));
    }

    void ctrlScrollZoomsAndMiddleDragPans() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        auto *canvas = findItem(view->rootObject(), "canvas");
        const QPoint center = Fixture::centerOf(canvas);
        tarn::test::sendWheel(view, center, 120, Qt::ControlModifier);
        QVERIFY(session->zoom() > 1.0);
        session->zoomTo(4); // big enough that the shot can be pulled about

        const QPointF before = session->origin();
        QTest::mousePress(view, Qt::MiddleButton, {}, center);
        QTest::mouseMove(view, center + QPoint(-30, -10));
        QTest::mouseMove(view, center + QPoint(-60, -20));
        QTest::mouseRelease(view, Qt::MiddleButton, {}, center + QPoint(-60, -20));
        QCOMPARE(session->annotationCount(), 0); // a pan, not a drawing
        QVERIFY(session->origin().x() < before.x());
        QVERIFY(session->origin().y() < before.y());
    }

    void spaceDragPans() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        session->zoomTo(2); // 800 x 600 on a 1280 x 702 canvas: it stays centered, so pan needs a bigger zoom
        session->zoomTo(4);
        auto *canvas = findItem(view->rootObject(), "canvas");
        const QPoint center = Fixture::centerOf(canvas);
        const QPointF before = session->origin();
        QTest::keyPress(view, Qt::Key_Space);
        Fixture::drag(view, center, center + QPoint(-50, -40));
        QTest::keyRelease(view, Qt::Key_Space);
        QCOMPARE(session->annotationCount(), 0);
        QVERIFY(session->origin().x() < before.x());
    }

    void handToolButtonFollowsZoomAndGPicksItOnlyWhenZoomed() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        auto *hand = findItem(view->rootObject(), "tool-hand");
        QVERIFY(hand);
        QVERIFY(!hand->isVisible()); // at Fit
        QTest::keyClick(view, Qt::Key_G);
        QVERIFY(session->tool() != QString("hand"));
        session->zoomTo(4);
        QTRY_VERIFY(hand->isVisible());
        QCOMPARE(hand->property("tip").toString(), QString::fromUtf8("Hand (G) · or hold Space and drag, or middle-drag"));
        QTest::keyClick(view, Qt::Key_G);
        QCOMPARE(session->tool(), QString("hand"));
        session->zoomFit();
        QTRY_VERIFY(!hand->isVisible());
        QVERIFY(session->tool() != QString("hand"));
    }

    // The Hand grows into the capsule next to Select (no pop-in), and shrinks away at Fit.
    void theHandGrowsInNextToSelect() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        auto *select = findItem(view->rootObject(), "tool-select");
        auto *hand = findItem(view->rootObject(), "tool-hand");
        auto *rect = findItem(view->rootObject(), "tool-rect");
        QVERIFY(select && hand && rect);
        auto sceneX = [](QQuickItem *i) { return i->mapToScene(QPointF(0, 0)).x(); };
        const qreal rectAtFit = sceneX(rect);
        QQuickItem *slot = hand->parentItem();
        QList<qreal> widths; // every width the slot passes through on its way open
        QObject::connect(slot, &QQuickItem::widthChanged, slot, [&] { widths << slot->width(); });
        session->zoomTo(4);
        QTRY_COMPARE(slot->width(), hand->width());
        QVERIFY2(std::any_of(widths.cbegin(), widths.cend(), [&](qreal w) { return w > 0 && w < hand->width(); }),
                 "the slot grows in, not popping to full width");

        QTRY_COMPARE(sceneX(rect) - rectAtFit, hand->width()); // then the whole button
        QVERIFY(sceneX(select) < sceneX(hand) && sceneX(hand) < sceneX(rect));
        session->zoomFit();
        QTRY_COMPARE(sceneX(rect), rectAtFit); // and closes again
    }

    // Zooming back past Fit while the slot is still opening bends the motion (a Spring keeps
    // its speed) instead of turning on the spot: CODING_STANDARDS.md, Motion.
    void reversingMidGrowthBendsInsteadOfRestarting() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        QQuickItem *hand = findItem(view->rootObject(), "tool-hand");
        QQuickItem *slot = hand->parentItem();
        QList<qreal> widths;
        QObject::connect(slot, &QQuickItem::widthChanged, slot, [&] { widths << slot->width(); });
        session->zoomTo(4);
        QTRY_VERIFY(slot->width() > 2);
        const qreal atReversal = slot->width();
        if (atReversal >= hand->width() - 1)
            QSKIP("the slot opened fully before the test could reverse it (a slow machine)");
        const int from = widths.size();
        session->zoomFit();
        QTRY_COMPARE(slot->width(), 0.0);
        const qreal peak = *std::max_element(widths.cbegin() + from, widths.cend());
        QVERIFY2(peak > atReversal, qPrintable(QString("turned on the spot at %1").arg(atReversal)));
        QVERIFY(peak < hand->width()); // and never reached full width
    }

    // Ctrl+scroll zooms and plain scroll pans, whatever kind of pointer Qt thinks sent it:
    // Hyprland's seat reaches Qt as a "touchpad" even for a mouse wheel, and the handlers
    // used to decline it (nothing happened on the user's desktop).
    void ctrlScrollZoomsAlsoWhenZoomedIn() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        auto *canvas = findItem(view->rootObject(), "canvas");
        const QPoint at = canvas->mapToScene(QPointF(canvas->width() / 2, canvas->height() / 2)).toPoint();
        auto scroll = [&](int dy, Qt::KeyboardModifiers mods) { tarn::test::sendWheel(view, at, dy, mods); };
        auto ctrlScroll = [&](int dy) { scroll(dy, Qt::ControlModifier); };
        const qreal fit = session->zoom();
        ctrlScroll(120);
        QTRY_VERIFY(session->zoom() > fit); // from Fit: works
        session->zoomTo(2);
        QVERIFY(session->handAvailable());
        ctrlScroll(-120);
        QTRY_VERIFY2(session->zoom() < 2, qPrintable(QString::number(session->zoom())));
        const qreal before = session->zoom();
        ctrlScroll(120);
        QTRY_VERIFY(session->zoom() > before);
        session->setTool("hand");
        const qreal withHand = session->zoom();
        ctrlScroll(-120);
        QTRY_VERIFY(session->zoom() < withHand);
        session->zoomTo(4); // the test shot is larger than the view only from here
        const QPointF origin = session->origin();
        scroll(-120, Qt::NoModifier); // plain scroll pans
        QTRY_VERIFY(session->origin() != origin);
    }

    void leftDragWithHandPansWithoutDrawing() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        session->zoomTo(4);
        session->setTool("hand");
        const QPoint center = Fixture::centerOf(findItem(view->rootObject(), "canvas"));
        const QPointF before = session->origin();
        Fixture::drag(view, center, center + QPoint(-50, -40));
        QCOMPARE(session->annotationCount(), 0);
        QVERIFY(!session->canUndo());
        QVERIFY(session->origin().x() < before.x());
        QVERIFY(session->origin().y() < before.y());
    }

    void spaceDragStillPansWithAnotherTool() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        session->zoomTo(4);
        session->setTool("rect");
        const QPoint center = Fixture::centerOf(findItem(view->rootObject(), "canvas"));
        const QPointF before = session->origin();
        QTest::keyPress(view, Qt::Key_Space);
        Fixture::drag(view, center, center + QPoint(-50, -40));
        QTest::keyRelease(view, Qt::Key_Space);
        QCOMPARE(session->annotationCount(), 0);
        QVERIFY(session->origin().x() < before.x());
    }

    // ---- selecting and editing ----

    // Draws a Rectangle with the mouse, from (60, 49) to (180, 129) in shot pixels, and returns the canvas.
    static QQuickItem *drawWithMouse(QQuickView *view, tarn::EditorSession *session) {
        auto *canvas = findItem(view->rootObject(), "canvas");
        const QPoint a = (canvas->mapToScene(QPointF(0, 0)) + session->docToView(60, 49)).toPoint();
        Fixture::drag(view, a, a + QPoint(120, 80));
        return canvas;
    }
    static QPoint viewPoint(QQuickView *view, tarn::EditorSession *session, qreal docX, qreal docY) {
        return (findItem(view->rootObject(), "canvas")->mapToScene(QPointF(0, 0)) + session->docToView(docX, docY)).toPoint();
    }

    void aSelectedRectangleShowsEightDotsAndAnOutlineAboveTheCanvas() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        drawWithMouse(view, session);
        QTest::mouseMove(view, viewPoint(view, session, 20, 220)); // pointer away from every Handle
        QTest::qWait(30);
        auto *root = view->rootObject();
        for (int h = 0; h < 8; ++h) {
            auto *dot = findItem(root, "chromeDot" + QString::number(h));
            QVERIFY2(dot, qPrintable("dot " + QString::number(h)));
            QCOMPARE(dot->width(), 14.0);
            QCOMPARE(dot->property("radius").toDouble(), 7.0);
            QCOMPARE(dot->property("color").value<QColor>(), QColor(Qt::white));
            QCOMPARE(dot->property("border").value<QObject *>()->property("width").toInt(), 2);
        }
        auto *outline = findItem(root, "chromeOutline");
        QVERIFY(outline);
        QCOMPARE(outline->property("border").value<QObject *>()->property("width").toInt(), 3);
        QVERIFY(outline->property("border").value<QObject *>()->property("color").value<QColor>().alphaF() > 0.49 && outline->property("border").value<QObject *>()->property("color").value<QColor>().alphaF() < 0.51);
        // Drawn above the picture: the chrome comes after the document canvas.
        auto *chrome = findItem(root, "selectionChrome");
        auto *picture = findItem(root, "documentCanvas");
        QVERIFY(chrome->z() >= picture->z());
        QCOMPARE(chrome->parentItem(), picture->parentItem());
    }

    void aDotGrowsTo18UnderThePointerAndAGhostShowsOnHover() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        drawWithMouse(view, session);
        auto *root = view->rootObject();
        QTest::mouseMove(view, viewPoint(view, session, 180, 129)); // bottom-right Handle
        QTRY_COMPARE(findItem(root, "chromeDot2")->width(), 18.0);
        QCOMPARE(findItem(root, "chromeDot0")->width(), 14.0);
        QCOMPARE(session->cursor(), int(Qt::SizeFDiagCursor));
        session->deselect();
        QTest::mouseMove(view, viewPoint(view, session, 90, 49)); // on the top edge
        QTRY_VERIFY(findItem(root, "chromeOutline"));
        QCOMPARE(findItem(root, "chromeOutline")->opacity(), 0.4);
        QCOMPARE(findItem(root, "chromeDot0")->opacity(), 0.4);
        QTest::mouseMove(view, viewPoint(view, session, 10, 10));
        QTRY_VERIFY(!findItem(root, "chromeOutline"));
    }

    void draggingAHandleWithTheMouseResizesAndCtrlZUndoesIt() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        drawWithMouse(view, session);
        const QPoint corner = viewPoint(view, session, 180, 129);
        Fixture::drag(view, corner, corner + QPoint(30, 20));
        QCOMPARE(session->document().items()[0].annotation->frame(), QRectF(60, 49, 150, 100));
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(session->document().items()[0].annotation->frame(), QRectF(60, 49, 120, 80));
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(session->document().items()[0].annotation->frame(), QRectF(60, 49, 150, 100));
    }

    void theEditingKeys() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        auto frame = [&](int i) { return session->document().items()[i].annotation->frame(); };
        drawWithMouse(view, session);
        QCOMPARE(session->selectionCount(), 1);

        QTest::keyClick(view, Qt::Key_Left);
        QCOMPARE(frame(0).x(), 59.0);
        QTest::keyClick(view, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(frame(0).y(), 59.0);

        QTest::keyClick(view, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(session->annotationCount(), 2);
        QTest::keyClick(view, Qt::Key_BracketLeft, Qt::ControlModifier); // send the copy backward
        QCOMPARE(session->document().order().first(), session->selection().first());
        QTest::keyClick(view, Qt::Key_BracketRight, Qt::ControlModifier);
        QCOMPARE(session->document().order().last(), session->selection().first());

        QTest::keyClick(view, Qt::Key_Backspace);
        QCOMPARE(session->annotationCount(), 1);
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(session->annotationCount(), 2);
        QTest::keyClick(view, Qt::Key_Delete); // nothing is selected after the undo: does nothing
        QCOMPARE(session->annotationCount(), 2);
        QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(session->selectionCount(), 2);
        QTest::keyClick(view, Qt::Key_Delete);
        QCOMPARE(session->annotationCount(), 0);
        QTest::keyClick(view, Qt::Key_Y, Qt::ControlModifier); // redo of nothing: the delete is the last step
        QCOMPARE(session->annotationCount(), 0);
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(session->annotationCount(), 2);
        QTest::keyClick(view, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(session->annotationCount(), 0);
    }

    void escStepsBackDeselectThenClosesAndCtrlCCopiesTheSelectionWithoutClosing() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        drawWithMouse(view, session);
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier); // Annotations selected: copies them
        QVERIFY(f.editor.hasAnnotationsToPaste());
        QVERIFY(f.env.calls("tarn-clip").isEmpty());
        QVERIFY(f.editor.session(f.id));
        QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(session->annotationCount(), 2);
        QTest::keyClick(view, Qt::Key_Escape);
        QCOMPARE(session->selectionCount(), 0);
        QVERIFY(f.editor.session(f.id));
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(!f.editor.session(f.id));
    }

    void ctrlCWithNothingSelectedStillCopiesAndClosesBoth() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        drawWithMouse(view, f.editor.session(f.id));
        f.editor.session(f.id)->deselect();
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(f.env.calls("tarn-clip").size(), 1);
        QCOMPARE(f.stack.totalCount(), 0);
    }

    void theSelectToolIsInTheCapsuleAndPicksABoxOfAnnotations() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *session = f.editor.session(f.id);
        QVERIFY(findItem(view->rootObject(), "tool-select"));
        Fixture::click(view, findItem(view->rootObject(), "tool-select"));
        QCOMPARE(session->tool(), QString("select"));
        session->setTool("rect");
        drawWithMouse(view, session);
        session->deselect();
        QTest::keyClick(view, Qt::Key_V); // the tool key
        QCOMPARE(session->tool(), QString("select"));
        Fixture::drag(view, viewPoint(view, session, 30, 20), viewPoint(view, session, 300, 250));
        QCOMPARE(session->selectionCount(), 1);
        QVERIFY(findItem(view->rootObject(), "chromeDot0"));
    }

    void closingTheEditorTakesItsWindow() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        Fixture::click(view, findItem(view->rootObject(), "closeButton"));
        QTRY_VERIFY(!f.windows.viewFor(f.id));
    }
};

QTEST_MAIN(EditorUiTest)
#include "editor_ui_test.moc"
