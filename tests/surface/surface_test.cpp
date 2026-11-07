// The Thumbnail surface (QML, compiled in) follows the stack, never
// takes keyboard focus, and has the look and motion of the spec. Layer-shell
// roles need a Wayland session, so what is checked here is what can be seen
// off-screen: sizes, positions, hover states and the exit timing. How it looks
// and feels at real speed is needs-eyes.
#include <algorithm>
#include "test_env.h"

#include "editor/editor.h"
#include "palette/palette.h"
#include "thumbnail_surface.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QTest>

using tarn::test::TestEnv;

namespace {
// Image.PreserveAspectCrop, and Image.AlignTop / AlignVCenter (= Qt::AlignTop / AlignVCenter).
constexpr int kPreserveAspectCrop = 2;
constexpr int kAlignTop = Qt::AlignTop;
constexpr int kAlignVCenter = Qt::AlignVCenter;

// Repeater delegates are visual children only, so walk childItems().
QQuickItem *findItem(QQuickItem *from, const QString &objectName) {
    if (from->objectName() == objectName)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = findItem(child, objectName))
            return hit;
    return nullptr;
}

void collect(QQuickItem *from, const QString &objectName, QList<QQuickItem *> &out) {
    if (from->objectName() == objectName)
        out << from;
    for (QQuickItem *child : from->childItems())
        collect(child, objectName, out);
}

// The Thumbnail cards on screen, lowest (newest) first.
QList<QQuickItem *> cards(tarn::ThumbnailSurface &surface) {
    QList<QQuickItem *> out;
    collect(surface.view().rootObject(), "card", out);
    std::sort(out.begin(), out.end(), [](QQuickItem *a, QQuickItem *b) { return a->property("rank").toInt() < b->property("rank").toInt(); });
    return out;
}

QPoint centerOf(QQuickItem *item) {
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

QByteArray pngOf(int w, int h) {
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

struct Fixture {
    TestEnv env;
    tarn::ThumbnailStack &stack = env.daemon().service<tarn::ThumbnailStack>();
    tarn::ThumbnailSurface surface{stack, env.daemon().service<tarn::Palette>()};
    tarn::Editor &editor = env.daemon().service<tarn::Editor>();

    int addShot(int w = 1280, int h = 800) { return stack.add(pngOf(w, h), env.daemon().now()); }
    // Shows the surface and waits until it has laid out `n` cards, each done springing in.
    bool exposed(int n) {
        return QTest::qWaitForWindowExposed(&surface.view()) && QTest::qWaitFor([&] {
            const auto list = cards(surface);
            return list.size() == n && std::all_of(list.begin(), list.end(), [](QQuickItem *c) { return c->x() == 16.0; });
        });
    }
    // Hover the pointer over a card, away from its buttons.
    void hover(QQuickItem *card) {
        // The offscreen pointer is global: off the spot first, or an earlier test's
        // pointer already standing there makes this a move to nowhere.
        QTest::mouseMove(&surface.view(), QPoint(0, 0));
        QTest::mouseMove(&surface.view(), card->mapToScene(QPointF(card->width() - 20, card->height() - 20)).toPoint());
    }
    void click(QQuickItem *button) { QTest::mouseClick(&surface.view(), Qt::LeftButton, {}, centerOf(button)); }
};
} // namespace

class SurfaceTest : public QObject {
    Q_OBJECT
private slots:
    void theQmlIsCompiledInAndLoads() {
        TestEnv env;
        tarn::ThumbnailSurface surface(env.daemon().service<tarn::ThumbnailStack>());
        QCOMPARE(surface.view().status(), QQuickView::Ready);
    }

    void itShowsOnlyWhileThereIsAThumbnailAndNeverAcceptsFocus() {
        Fixture f;
        QVERIFY(!f.surface.visible());
        QVERIFY(f.surface.view().flags() & Qt::WindowDoesNotAcceptFocus);

        { auto r = f.env.request({"capture", "fullscreen"}); QVERIFY2(r.ok, qPrintable(r.error)); }
        QVERIFY(f.surface.visible());
        QVERIFY(f.exposed(1));

        f.stack.close(f.stack.ids().first());
        // It stays up for the exit, then goes.
        QVERIFY(f.surface.visible());
        QTRY_VERIFY_WITH_TIMEOUT(!f.surface.visible(), 3000);
    }

    void everyThumbnailIs240By150SixteenFromTheCornerAndTwelveApart() {
        Fixture f;
        f.addShot();
        f.addShot(300, 900);
        f.addShot(3000, 200);
        QVERIFY(f.exposed(3));
        const double h = f.surface.view().rootObject()->height();
        QTRY_COMPARE_WITH_TIMEOUT(cards(f.surface)[2]->y() + 150, cards(f.surface)[1]->y() - 12, 1000);
        QTRY_COMPARE_WITH_TIMEOUT(cards(f.surface)[1]->y() + 150, cards(f.surface)[0]->y() - 12, 1000);
        auto list = cards(f.surface);
        for (auto *card : list)
            QTRY_COMPARE_WITH_TIMEOUT(card->x(), 16.0, 1500); // springing in from the edge
        for (auto *card : list) {
            QCOMPARE(card->width(), 240.0);
            QCOMPARE(card->height(), 150.0);
            QCOMPARE(card->x(), 16.0);
        }
        // Newest in the corner, 16 up from the bottom edge, then upward 12 px apart.
        QCOMPARE(list[0]->y() + 150, h - 16);
        QCOMPARE(list[1]->y() + 150, list[0]->y() - 12);
        QCOMPARE(list[2]->y() + 150, list[1]->y() - 12);
    }

    void aTallCaptureKeepsItsTopAndOthersAreCroppedToFill() {
        Fixture f;
        f.addShot(300, 900);  // tall (older)
        f.addShot(1280, 800); // 16:10
        QVERIFY(f.exposed(2));
        auto list = cards(f.surface);
        auto *wide = findItem(list[0], "shot");
        auto *tall = findItem(list[1], "shot");
        QCOMPARE(wide->property("fillMode").toInt(), kPreserveAspectCrop);
        QCOMPARE(tall->property("fillMode").toInt(), kPreserveAspectCrop);
        QTRY_COMPARE(tall->property("verticalAlignment").toInt(), kAlignTop);
        QCOMPARE(wide->property("verticalAlignment").toInt(), kAlignVCenter);
    }

    void hoverFadesInTheScrimAndControlsAndMovingAwayHidesThem() {
        Fixture f;
        f.addShot();
        QVERIFY(f.exposed(1));
        auto *card = cards(f.surface)[0];
        auto *scrim = findItem(card, "scrim");
        auto *controls = findItem(card, "controls");
        QCOMPARE(scrim->property("opacity").toDouble(), 0.0);
        QVERIFY(!controls->isVisible()); // faded controls are invisible, so invisible to hover
        QCOMPARE(scrim->property("color").value<QColor>().alphaF(), 0.72f);

        f.hover(card);
        QTRY_COMPARE(scrim->property("opacity").toDouble(), 1.0);
        QVERIFY(controls->isVisible());
        QVERIFY(findItem(card, "copyButton") && findItem(card, "saveButton") && findItem(card, "closeButton"));
        QCOMPARE(findItem(card, "closeButton")->width(), 28.0);
        QCOMPARE(findItem(card, "closeButton")->height(), 28.0);

        QTest::mouseMove(&f.surface.view(), {2, 2});
        QTRY_COMPARE(scrim->property("opacity").toDouble(), 0.0);
        QTRY_VERIFY(!controls->isVisible());
    }

    void movingBetweenThumbnailsNeverLeavesAGapWithNoHover() {
        Fixture f;
        f.addShot();
        f.addShot();
        QVERIFY(f.exposed(2));
        QTRY_VERIFY(cards(f.surface)[1]->y() < cards(f.surface)[0]->y() - 100);
        QTest::qWait(350); // the older one has finished moving up
        auto list = cards(f.surface);
        auto *lower = list[0], *upper = list[1];
        const int x = int(lower->x()) + 20;
        // Walk from the middle of the upper card to the middle of the lower one, 1 px at a time.
        for (int y = centerOf(upper).y(); y <= centerOf(lower).y(); ++y) {
            QTest::mouseMove(&f.surface.view(), {x, y});
            const bool a = upper->property("hovered").toBool(), b = lower->property("hovered").toBool();
            QVERIFY2(a || b, qPrintable(QString("no card hovered at y=%1").arg(y)));
            QVERIFY2(!(a && b), qPrintable(QString("two cards hovered at y=%1").arg(y)));
        }
        // The zones meet in the middle of the gap.
        QTest::mouseMove(&f.surface.view(), {x, int(upper->y() + 150 + 4)});
        QVERIFY(upper->property("hovered").toBool());
        QTest::mouseMove(&f.surface.view(), {x, int(upper->y() + 150 + 8)});
        QVERIFY(lower->property("hovered").toBool());
    }

    void aNewThumbnailSpringsInFromTheLeftEdgeAndTheOthersSpringUp() {
        Fixture f;
        f.addShot();
        QVERIFY(f.exposed(1));
        auto *first = cards(f.surface)[0];
        QTRY_COMPARE_WITH_TIMEOUT(first->x(), 16.0, 1000);
        const double corner = first->y();
        QElapsedTimer clock;
        clock.start();
        const int newest = f.addShot();
        QTRY_COMPARE(cards(f.surface).size(), 2);
        auto *added = cards(f.surface)[0];
        QCOMPARE(added->property("captureId").toInt(), newest);
        QCOMPARE(added->y(), corner);         // already at the corner's height
        QVERIFY(added->x() <= -240.0);        // but starting just past the left edge,
        QCOMPARE(added->opacity(), 0.0);      // clear
        // It springs in and settles exactly in place, solid.
        QTRY_VERIFY(added->x() > -240.0);
        QTRY_COMPARE_WITH_TIMEOUT(added->x(), 16.0, 1500);
        QCOMPARE(added->opacity(), 1.0);
        QVERIFY2(clock.elapsed() >= 250, qPrintable(QString::number(clock.elapsed())));
        // The old one springs up one slot over the same moment.
        QCOMPARE(added->y(), corner);
        QTRY_COMPARE_WITH_TIMEOUT(first->y(), corner - 162, 1500);
    }

    void aThumbnailBackFromTheEditorIsThereAtOnce() {
        Fixture f;
        const int a = f.addShot();
        QVERIFY(f.exposed(1));
        QTRY_COMPARE_WITH_TIMEOUT(cards(f.surface)[0]->x(), 16.0, 1000);
        f.stack.setHidden(a, true);
        QTRY_COMPARE(cards(f.surface).size(), 0);
        f.stack.setHidden(a, false);
        QTRY_COMPARE(cards(f.surface).size(), 1);
        QCOMPARE(cards(f.surface)[0]->x(), 16.0);
        QCOMPARE(cards(f.surface)[0]->opacity(), 1.0);
    }

    void closeDiscardsWithoutSavingAndSlidesOutAtOnce() {
        Fixture f;
        f.addShot();
        QVERIFY(f.exposed(1));
        auto *card = cards(f.surface)[0];
        f.hover(card);
        QTRY_VERIFY(findItem(card, "closeButton")->isVisible());
        f.click(findItem(card, "closeButton"));
        QCOMPARE(f.stack.count(), 0);
        QVERIFY(card->property("leaving").toBool());
        QVERIFY(TestEnv::filesIn(f.env.screenshots()).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(cards(f.surface).isEmpty(), 3000);
    }

    void saveShowsACheckThenTheSlideStarts450msAfterTheClick() {
        Fixture f;
        f.addShot();
        QVERIFY(f.exposed(1));
        auto *card = cards(f.surface)[0];
        f.hover(card);
        QTRY_VERIFY(findItem(card, "saveButton")->isVisible());

        QPointer<QQuickItem> check = findItem(findItem(card, "saveButton"), "check");
        QElapsedTimer clock;
        clock.start();
        f.click(findItem(card, "saveButton"));
        QCOMPARE(TestEnv::filesIn(f.env.screenshots()).size(), 1);
        QCOMPARE(f.stack.count(), 0);
        QCOMPARE(card->property("confirming").toString(), QString("save"));
        // The check arrives (200 ms) while the card stays put and fully visible.
        QTRY_COMPARE_WITH_TIMEOUT(check->opacity(), 1.0, 1000);
        QCOMPARE(card->x(), 16.0);
        QVERIFY(!card->property("leaving").toBool());
        // The slide starts at 450 ms.
        QTRY_VERIFY_WITH_TIMEOUT(card->property("leaving").toBool(), 1500);
        QVERIFY2(clock.elapsed() >= 440, qPrintable(QString::number(clock.elapsed())));
        QVERIFY2(clock.elapsed() < 640, qPrintable(QString::number(clock.elapsed())));
        // Then about 400 ms of gliding off the left edge, fading out.
        QTRY_VERIFY_WITH_TIMEOUT(card->x() < 16.0, 500);
        QTRY_VERIFY_WITH_TIMEOUT(cards(f.surface).isEmpty(), 3000);
        QVERIFY2(clock.elapsed() >= 800, qPrintable(QString::number(clock.elapsed())));
        QTRY_VERIFY_WITH_TIMEOUT(!f.surface.visible(), 1000);
    }

    void copyShowsACheckToo() {
        Fixture f;
        f.addShot();
        QVERIFY(f.exposed(1));
        auto *card = cards(f.surface)[0];
        f.hover(card);
        QTRY_VERIFY(findItem(card, "copyButton")->isVisible());
        f.click(findItem(card, "copyButton"));
        QCOMPARE(card->property("confirming").toString(), QString("copy"));
        QCOMPARE(f.stack.count(), 0);
        QCOMPARE(f.env.calls("tarn-clip").size(), 1);
    }

    void aFailedSaveKeepsTheThumbnailWithoutACheck() {
        Fixture f;
        QVERIFY(QDir().mkpath(f.env.pictures()));
        QFile blocker(f.env.screenshots()); // a file where the folder should be
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        f.addShot();
        QVERIFY(f.exposed(1));
        auto *card = cards(f.surface)[0];
        f.hover(card);
        QTRY_VERIFY(findItem(card, "saveButton")->isVisible());
        f.click(findItem(card, "saveButton"));
        QCOMPARE(f.stack.count(), 1);
        QCOMPARE(card->property("confirming").toString(), QString());
        QVERIFY(!card->property("leaving").toBool());
    }

    // Pressing does nothing until release; releasing opens the Editor, which
    // hides the Thumbnail from the stack.
    void pressingTheCardDoesNothingUntilReleaseWhichOpensTheEditor() {
        Fixture f;
        const int id = f.addShot();
        QVERIFY(f.exposed(1));
        auto *card = cards(f.surface)[0];
        f.hover(card);
        QTRY_VERIFY(findItem(card, "saveButton")->isVisible());
        const QPoint spot = card->mapToScene(QPointF(card->width() - 20, card->height() - 20)).toPoint();
        QTest::mousePress(&f.surface.view(), Qt::LeftButton, {}, spot);
        QTest::qWait(100);
        QCOMPARE(f.stack.count(), 1);
        QVERIFY(!f.editor.session(id));
        QCOMPARE(f.env.calls("tarn-clip").size(), 0);
        QVERIFY(TestEnv::filesIn(f.env.screenshots()).isEmpty());
        QTest::mouseRelease(&f.surface.view(), Qt::LeftButton, {}, spot);
        QVERIFY(f.editor.session(id));
        QVERIFY(f.stack.isHidden(id));
        QTRY_VERIFY(cards(f.surface).isEmpty()); // gone at once, with no exit animation
    }

    void theEditButtonOpensTheEditorAndNothingElse() {
        Fixture f;
        const int id = f.addShot();
        QVERIFY(f.exposed(1));
        auto *card = cards(f.surface)[0];
        f.hover(card);
        QTRY_VERIFY(findItem(card, "editButton")->isVisible());
        f.click(findItem(card, "editButton"));
        QVERIFY(f.editor.session(id));
        QCOMPARE(f.env.calls("tarn-clip").size(), 0);
    }

    void aButtonOnTheCardDoesNotAlsoOpenTheEditor() {
        Fixture f;
        const int id = f.addShot();
        QVERIFY(f.exposed(1));
        auto *card = cards(f.surface)[0];
        f.hover(card);
        QTRY_VERIFY(findItem(card, "closeButton")->isVisible());
        f.click(findItem(card, "closeButton"));
        QVERIFY(!f.editor.session(id));
    }

    void onlyTheLiveCardsTakeThePointer() {
        Fixture f;
        f.addShot();
        f.addShot();
        QVERIFY(f.exposed(2));
        // The input region covers both cards plus half a gap each side, not the 16 px edge strip.
        const QRect mask = f.surface.view().rootObject()->property("inputRect").toRect();
        QCOMPARE(mask.x(), 16);
        QCOMPARE(mask.width(), 240);
        QCOMPARE(mask.height(), 2 * 150 + 12 + 12);
    }

    void theSurfaceFollowsThePaletteLive() {
        Fixture f;
        f.addShot();
        QTRY_VERIFY(findItem(f.surface.view().rootObject(), "backdrop"));
        auto *backdrop = findItem(f.surface.view().rootObject(), "backdrop");
        QCOMPARE(backdrop->property("color").value<QColor>(), QColor("#101010")); // built-in fallback

        // A Palette file comes into use: the open surface recolors.
        const QString dir = f.env.scratch() + "/shell";
        QDir().mkpath(dir);
        QFile file(dir + "/colors.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"surface_container_lowest\":\"#302921\"}");
        file.close();
        QFile config(f.env.scratch() + "/config/tarn/config.toml");
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
        config.write(("palette_file = \"" + dir + "/colors.json\"\n").toUtf8());
        config.close();
        QTRY_COMPARE(backdrop->property("color").value<QColor>(), QColor("#302921"));
    }

    // ---- Right-click menu, hidden while editing ----

    void rightClickOpensTheMenuWithSaveAsDeleteAndCloseAll() {
        Fixture f;
        f.addShot();
        QVERIFY(f.exposed(1));
        auto *card = cards(f.surface)[0];
        QVERIFY(!findItem(card, "menu")->isVisible());
        f.hover(card);
        QTest::mouseClick(&f.surface.view(), Qt::RightButton, {}, card->mapToScene(QPointF(60, 40)).toPoint());
        QTRY_VERIFY(findItem(card, "menu")->isVisible());
        QVERIFY(findItem(card, "menuSaveAs") && findItem(card, "menuDelete") && findItem(card, "menuCloseAll"));
        // It stays inside the card, so the surface's input mask covers it.
        auto *menu = findItem(card, "menu");
        QVERIFY(menu->x() >= 0 && menu->x() + menu->width() <= card->width());
        QVERIFY(menu->y() >= 0 && menu->y() + menu->height() <= card->height());
    }

    void closeAllInTheMenuClosesEveryThumbnail() {
        Fixture f;
        f.addShot();
        f.addShot();
        QVERIFY(f.exposed(2));
        auto *card = cards(f.surface)[0];
        f.hover(card);
        QTest::mouseClick(&f.surface.view(), Qt::RightButton, {}, card->mapToScene(QPointF(60, 40)).toPoint());
        QTRY_VERIFY(findItem(card, "menu")->isVisible());
        f.click(findItem(card, "menuCloseAll"));
        QCOMPARE(f.stack.count(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(cards(f.surface).isEmpty(), 3000);
    }

    void aHiddenThumbnailLeavesTheScreenAtOnceAndComesBack() {
        Fixture f;
        const int a = f.addShot();
        f.addShot();
        QVERIFY(f.exposed(2));
        f.stack.setHidden(a, true);
        QTRY_COMPARE(cards(f.surface).size(), 1);
        f.stack.setHidden(a, false);
        QTRY_COMPARE(cards(f.surface).size(), 2);
        // Oldest on top, as before.
        QVERIFY(cards(f.surface)[1]->property("captureId").toInt() == a);
    }
};

QTEST_MAIN(SurfaceTest)
#include "surface_test.moc"
