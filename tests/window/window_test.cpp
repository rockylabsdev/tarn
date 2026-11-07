// Pointing at a window in the Overlay highlights it (sticky for 16 px), a
// click captures just that window with transparent rounded corners, and the file
// name and History carry the app. hyprctl is a stub that answers with a window list.
#include "test_env.h"

#include "overlay/overlay.h"
#include "overlay/window_shot.h"
#include "overlay_surface.h"
#include "palette/palette.h"
#include "history/history.h"
#include "thumbnails/thumbnail_stack.h"

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
using tarn::ThumbnailStack;
using tarn::WindowInfo;
using tarn::test::TestEnv;

namespace {
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

// One 128 x 80 monitor at scale 2 (64 x 40 logical pixels) showing workspace 1.
const QByteArray kMonitor =
    R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0,"activeWorkspace":{"id":1},"specialWorkspace":{"id":0}}])";

QJsonObject client(const QString &app, const QString &title, int x, int y, int w, int h, int focus, int workspace = 1,
                   bool floating = false, bool hidden = false) {
    return {{"mapped", true}, {"hidden", hidden}, {"workspace", QJsonObject{{"id", workspace}}},
            {"at", QJsonArray{x, y}}, {"size", QJsonArray{w, h}}, {"fullscreen", 0}, {"floating", floating},
            {"class", app}, {"title", title}, {"focusHistoryID", focus}};
}

QQuickItem *findItem(QQuickItem *from, const QString &objectName) {
    if (from->objectName() == objectName)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = findItem(child, objectName))
            return hit;
    return nullptr;
}

WindowInfo win(int x, int y, int w, int h, const QString &app) { return {QRect(x, y, w, h), app, app + " title", 0}; }
} // namespace

class WindowTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    void setClients(const QJsonArray &clients) {
        env->setFixture("hyprctl", "clients.json", QJsonDocument(clients).toJson());
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setFixture("grim", "output.png", pngOf(gradient()));
        env->setPointer(10, 10);
    }
    void cleanup() { env.reset(); }

    // Hit-testing and stickiness, on the model alone.
    void theFrontmostWindowUnderThePointerIsHighlighted() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, QSize(200, 100));
        m.setWindows({win(10, 10, 40, 40, "front"), win(0, 0, 100, 100, "back")});
        m.pointerMoved(20, 20);
        QCOMPARE(m.highlightApp(), QString("front"));
        QCOMPARE(m.highlight(), QRect(10, 10, 40, 40));
        m.pointerMoved(80, 80); // only the window behind is there
        QCOMPARE(m.highlightApp(), QString("back"));
        m.pointerMoved(150, 150); // outside both, further than 16 px
        QVERIFY(!m.hasHighlight());
    }

    void theHighlightSticksForSixteenPixelsAndDoesNotDropCrossingAGap() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, QSize(200, 100));
        // Two windows 16 px apart: A covers x 10..49, B covers x 66..105.
        m.setWindows({win(10, 10, 40, 40, "a"), win(66, 10, 40, 40, "b")});
        QSignalSpy changed(&m, &OverlayModel::highlightChanged);
        m.pointerMoved(30, 30);
        QCOMPARE(m.highlightApp(), QString("a"));
        changed.clear();
        for (int x = 50; x <= 65; ++x) { // the whole gap
            m.pointerMoved(x, 30);
            QCOMPARE(m.highlightApp(), QString("a")); // never drops
        }
        QCOMPARE(changed.count(), 0);
        m.pointerMoved(66, 30);
        QCOMPARE(m.highlightApp(), QString("b")); // entering B switches

        // Leaving A into empty space: kept up to 16 px, gone at 17.
        m.pointerMoved(30, 30);
        m.pointerMoved(49 + 16, 30);
        QCOMPARE(m.highlightApp(), QString("a"));
        m.setWindows({win(10, 10, 40, 40, "a")});
        m.pointerMoved(30, 30);
        m.pointerMoved(49 + 17, 30);
        QVERIFY(!m.hasHighlight());
        // Vertically too.
        m.pointerMoved(30, 30);
        m.pointerMoved(30, 49 + 16);
        QVERIFY(m.hasHighlight());
        m.pointerMoved(30, 49 + 17);
        QVERIFY(!m.hasHighlight());
    }

    void aClickOnTheHighlightConfirmsTheWindowAndADragStillMakesAnArea() {
        OverlayModel m;
        QSignalSpy window(&m, &OverlayModel::confirmedWindow);
        QSignalSpy area(&m, &OverlayModel::confirmed);
        m.open(OverlayModel::Screenshot, QSize(200, 100));
        m.setWindows({win(10, 10, 40, 40, "a")});
        m.pointerMoved(20, 20);
        m.pointerPressed(20, 20, Qt::LeftButton);
        m.pointerMoved(21, 21); // a click's wobble
        QVERIFY(!m.hasSelection());
        m.pointerReleased(21, 21, Qt::LeftButton);
        QCOMPARE(window.count(), 1);
        QCOMPARE(window.first().first().value<WindowInfo>().app, QString("a"));
        QVERIFY(area.isEmpty());
        QVERIFY(!m.active());

        m.open(OverlayModel::Screenshot, QSize(200, 100));
        m.setWindows({win(10, 10, 40, 40, "a")});
        m.pointerMoved(20, 20);
        m.pointerPressed(20, 20, Qt::LeftButton);
        m.pointerMoved(40, 40);
        QVERIFY(!m.hasHighlight()); // it is an area now
        m.pointerReleased(40, 40, Qt::LeftButton);
        QCOMPARE(window.count(), 1);
        QCOMPARE(area.count(), 1);
        QCOMPARE(area.first().first().toRect(), QRect(20, 20, 20, 20));
    }

    void recordModeSelectsAreasOnly() {
        for (auto mode : {OverlayModel::Record}) {
            OverlayModel m;
            m.open(mode, QSize(200, 100));
            m.setWindows({win(10, 10, 40, 40, "a")});
            m.pointerMoved(20, 20);
            QVERIFY(!m.hasHighlight());
        }
    }

    // The corner mask.
    void theCutHasTransparentRoundedCornersAndTheFrozenPixelsInside() {
        const QImage frozen = gradient();
        const WindowInfo w{QRect(4, 4, 30, 30), "app", "t", 4}; // radius 8 device px at scale 2
        const QImage cut = tarn::cutWindow(frozen, w, 2.0);
        QCOMPARE(cut.size(), QSize(60, 60));
        QVERIFY(cut.hasAlphaChannel());
        for (const QPoint corner : {QPoint(0, 0), QPoint(59, 0), QPoint(0, 59), QPoint(59, 59)})
            QCOMPARE(qAlpha(cut.pixel(corner)), 0);
        QCOMPARE(qAlpha(cut.pixel(30, 0)), 255);  // the straight edges are solid
        QCOMPARE(qAlpha(cut.pixel(0, 30)), 255);
        // Everything away from a corner is exactly what the screen showed (a menu over
        // the window included, since it is in the frozen image).
        for (int y = 8; y < 52; ++y)
            for (int x = 8; x < 52; ++x)
                QCOMPARE(cut.pixel(x, y), frozen.pixel(8 + x, 8 + y) | 0xff000000);
    }

    void aSquareWindowKeepsItsCorners() {
        const QImage cut = tarn::cutWindow(gradient(), {QRect(4, 4, 30, 30), "app", "t", 0}, 2.0);
        QCOMPARE(qAlpha(cut.pixel(0, 0)), 255);
    }

    // Through the daemon.
    void theBridgeListsOnlyVisibleWindowsOnTheShownWorkspaceFrontFirst() {
        setClients({client("tiled", "t", 0, 0, 30, 30, 1),
                    client("floating", "f", 20, 20, 20, 20, 5, 1, true),
                    client("recent-tiled", "r", 40, 0, 20, 20, 0),
                    client("other-workspace", "o", 0, 0, 30, 30, 2, 2),
                    client("hidden", "h", 0, 0, 30, 30, 3, 1, false, true),
                    client("off-screen", "x", 500, 500, 30, 30, 4)});
        QVERIFY(env->request({"capture", "area"}).ok);
        model().pointerMoved(25, 25);
        QCOMPARE(model().highlightApp(), QString("floating")); // floating beats tiled
        model().pointerMoved(5, 5);
        QCOMPARE(model().highlightApp(), QString("tiled"));
        model().pointerMoved(45, 5);
        QCOMPARE(model().highlightApp(), QString("recent-tiled"));
        QCOMPARE(model().highlightRounding(), 10.0); // from decoration:rounding
    }

    void aWindowOnAMonitorWithAnOffsetIsMonitorLocal() {
        env->setFixture("hyprctl", "monitors.json",
                        R"([{"id":0,"name":"DP-1","x":100,"y":50,"width":128,"height":80,"scale":2.0,"transform":0,"activeWorkspace":{"id":1}}])");
        env->setPointer(110, 60);
        setClients({client("a", "t", 110, 60, 30, 30, 0)});
        QVERIFY(env->request({"capture", "area"}).ok);
        model().pointerMoved(15, 15);
        QCOMPARE(model().highlight(), QRect(10, 10, 30, 30));
    }

    void clickingAWindowCapturesJustItNamedWithTheAppAndRecordsItInHistory() {
        env->setNow(QDateTime(QDate(2026, 11, 2), QTime(14, 3, 22)));
        setClients({client("firefox", "Rust docs - Mozilla Firefox", 4, 4, 30, 30, 0)});
        QVERIFY(env->request({"capture", "area"}).ok);
        model().pointerMoved(10, 10);
        model().pointerPressed(10, 10, Qt::LeftButton);
        model().pointerReleased(10, 10, Qt::LeftButton);

        QCOMPARE(env->calls("grim").size(), 1); // cut from the frozen image, not grabbed again
        QCOMPARE(stack().count(), 1);
        const int id = stack().ids().first();
        const QString path = stack().save(id);
        QCOMPARE(path, env->screenshots() + "/2026-11-02_14-03-22_firefox.png");

        const QImage saved(path);
        QCOMPARE(saved.size(), QSize(60, 60)); // logical (4,4) 30 x 30 at scale 2
        QCOMPARE(qAlpha(saved.pixel(0, 0)), 0);
        QCOMPARE(qAlpha(saved.pixel(30, 30)), 255);
        QCOMPARE(saved.pixel(30, 30), gradient().pixel(8 + 30, 8 + 30));

        auto &history = env->daemon().service<tarn::History>();
        history.flush();
        const QStringList ids = history.ids();
        QCOMPARE(ids.size(), 1);
        const QJsonObject d = QJsonDocument::fromJson(TestEnv::readFile(history.descriptionPath(ids.first()))).object();
        QCOMPARE(d.value("kind").toString(), QString("window"));
        QCOMPARE(d.value("app").toString(), QString("firefox"));
        QCOMPARE(d.value("title").toString(), QString("Rust docs - Mozilla Firefox"));
        QCOMPARE(d.value("savedPaths").toArray().first().toString(), path);
    }

    void twoWindowCapturesInTheSameSecondDoNotOverwrite() {
        env->setNow(QDateTime(QDate(2026, 11, 2), QTime(14, 3, 22)));
        setClients({client("kitty", "t", 4, 4, 30, 30, 0)});
        for (int i = 0; i < 2; ++i) {
            QVERIFY(env->request({"capture", "area"}).ok);
            model().pointerMoved(10, 10);
            model().pointerPressed(10, 10, Qt::LeftButton);
            model().pointerReleased(10, 10, Qt::LeftButton);
        }
        for (int id : stack().ids())
            QVERIFY(!stack().save(id).isEmpty());
        QCOMPARE(TestEnv::filesIn(env->screenshots()),
                 (QStringList{"2026-11-02_14-03-22_kitty.png", "2026-11-02_14-03-22_kitty_2.png"}));
    }

    void aFailingWindowListLeavesAreaSelectionWorking() {

        setClients({});
        QVERIFY(env->request({"capture", "area"}).ok);
        model().pointerMoved(10, 10);
        QVERIFY(!model().hasHighlight());
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerReleased(30, 25, Qt::LeftButton);
        QCOMPARE(stack().count(), 1);
    }

    void theSurfaceShowsTheOutlineAndAppPillOnHover() {
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        setClients({client("firefox", "t", 4, 4, 30, 30, 0)});
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        QTest::mouseMove(&surface.view(), QPoint(10, 10));
        QTRY_VERIFY(findItem(root, "appPill")->isVisible());
        QVERIFY(findItem(root, "windowOutline")->isVisible());
        QCOMPARE(findItem(root, "appLabel")->property("text").toString(), QString("firefox"));
        QTest::mouseMove(&surface.view(), QPoint(60, 5)); // above the Mode bar, clear of the window
        QTRY_VERIFY(!findItem(root, "appPill")->isVisible());
    }
};

QTEST_MAIN(WindowTest)
#include "window_test.moc"
