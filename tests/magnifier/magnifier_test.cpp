// M turns the Magnifier on and off; it stays that way for the next capture
// and across restarts (state.toml); the tile and its x, y pill sit 24 px
// below-right of the pointer and flip on each axis at the monitor's edges.
#include "test_env.h"

#include "overlay/overlay.h"
#include "overlay_surface.h"
#include "palette/palette.h"
#include "state/remembered_state.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QQuickItem>
#include <QTest>

using tarn::Overlay;
using tarn::OverlayModel;
using tarn::test::TestEnv;

namespace {
QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// One 256 x 160 monitor at scale 2: 128 x 80 logical pixels.
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":256,"height":160,"scale":2.0,"transform":0}])";

QQuickItem *findItem(QQuickItem *from, const QString &objectName) {
    if (from->objectName() == objectName)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = findItem(child, objectName))
            return hit;
    return nullptr;
}

constexpr int kKeyM = Qt::Key_M;
} // namespace

class MagnifierTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    bool press() { return env->request({"capture", "area"}).ok; }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        QImage image(256, 160, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setFixture("grim", "output.png", pngOf(image));
        env->setPointer(10, 10);
    }
    void cleanup() { env.reset(); }

    // The toggle.
    void mTurnsTheMagnifierOnAndOffWhileTheOverlayIsOpen() {
        QVERIFY(press());
        QVERIFY(!model().magnifier());
        model().keyPressed(kKeyM);
        QVERIFY(model().magnifier());
        model().keyPressed(kKeyM);
        QVERIFY(!model().magnifier());
    }
    void mDoesNothingWhileTheOverlayIsClosed() {
        OverlayModel m;
        m.keyPressed(kKeyM);
        QVERIFY(!m.magnifier());
    }

    // Remembered.
    void theMagnifierStaysOnForTheNextCapture() {
        QVERIFY(press());
        model().keyPressed(kKeyM);
        model().cancel();
        QVERIFY(press());
        QVERIFY(model().magnifier());
        model().cancel();
    }
    void theMagnifierStaysAsLeftAcrossARestart() {
        QVERIFY(press());
        model().keyPressed(kKeyM);
        model().cancel();
        env->restartDaemon();
        QVERIFY(press());
        QVERIFY(model().magnifier());
        model().keyPressed(kKeyM); // off again
        model().cancel();
        env->restartDaemon();
        QVERIFY(press());
        QVERIFY(!model().magnifier());
    }
    void itIsWrittenToStateToml() {
        QVERIFY(press());
        model().keyPressed(kKeyM);
        QFile file(env->daemon().service<tarn::RememberedState>().path());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY2(text.contains("[magnifier]") && text.contains("on = true"), qPrintable(text));
    }

    // Where the tile goes: 24 px below-right of the pointer; pill 8 px under it.
    void theTileSitsBelowRightOfThePointer() {
        QCOMPARE(OverlayModel::magnifierTileAt({100, 100}, QSize(1000, 600)), QPoint(124, 124));
    }
    void thePillIsEightPixelsUnderTheTile() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, QSize(1000, 600));
        m.pointerMoved(100, 100);
        QCOMPARE(m.magnifierTile(), QPoint(124, 124));
        QCOMPARE(m.magnifierPill(), QPoint(124, 124 + 120 + 8));
    }
    void theTileFlipsLeftNearTheRightEdge() {
        const QSize screen(1000, 600);
        // 120 px tile, kept 4 px off the edge: the last pointer x that stays right is 1000-4-120-24 = 852.
        QCOMPARE(OverlayModel::magnifierTileAt({852, 100}, screen), QPoint(876, 124));
        QCOMPARE(OverlayModel::magnifierTileAt({853, 100}, screen), QPoint(853 - 24 - 120, 124));
        QCOMPARE(OverlayModel::magnifierTileAt({999, 100}, screen), QPoint(855, 124));
    }
    void theTileFlipsUpNearTheBottomEdge() {
        const QSize screen(1000, 600);
        // The tile and its pill are 152 px tall: the last y that stays below is 600-4-152-24 = 420.
        QCOMPARE(OverlayModel::magnifierTileAt({100, 420}, screen), QPoint(124, 444));
        QCOMPARE(OverlayModel::magnifierTileAt({100, 421}, screen), QPoint(124, 421 - 24 - 152));
        QCOMPARE(OverlayModel::magnifierTileAt({100, 599}, screen), QPoint(124, 423));
    }
    void eachAxisFlipsOnItsOwn() {
        const QSize screen(1000, 600);
        QCOMPARE(OverlayModel::magnifierTileAt({999, 599}, screen), QPoint(855, 423)); // bottom-right corner
        QCOMPARE(OverlayModel::magnifierTileAt({0, 0}, screen), QPoint(24, 24));        // top-left corner
        QCOMPARE(OverlayModel::magnifierTileAt({999, 0}, screen), QPoint(855, 24));     // top-right
        QCOMPARE(OverlayModel::magnifierTileAt({0, 599}, screen), QPoint(24, 423));     // bottom-left
    }
    void theTileAndPillNeverLeaveTheMonitor() {
        const QSize screen(1000, 600);
        for (int x = 0; x < screen.width(); x += 7)
            for (int y = 0; y < screen.height(); y += 7) {
                const QPoint t = OverlayModel::magnifierTileAt({x, y}, screen);
                QVERIFY2(t.x() >= 0 && t.x() + 120 <= screen.width() && t.y() >= 0 && t.y() + 152 <= screen.height(),
                         qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
            }
    }

    // The pill reads the real position: device pixels (this monitor is at scale 2).
    void theXyPillShowsThePointerInDevicePixels() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, QSize(128, 80), 2.0);
        m.pointerMoved(10, 5);
        QCOMPARE(m.magnifierLabel(), QStringLiteral("20, 10"));
    }

    // The surface, off-screen.
    void theSurfaceShowsTheTileAndPillOnlyWhileOn() {
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(press());
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        auto *magnifier = findItem(root, "magnifier");
        QVERIFY(magnifier);
        QVERIFY(!magnifier->isVisible());

        QTest::keyClick(&surface.view(), Qt::Key_M);
        QTRY_VERIFY(magnifier->isVisible());
        QTest::mouseMove(&surface.view(), QPoint(30, 20));
        QTRY_COMPARE(findItem(root, "magnifierLabel")->property("text").toString(), QStringLiteral("60, 40"));
        QCOMPARE(findItem(root, "magnifierTile")->size(), QSizeF(120, 120));
        QVERIFY(findItem(root, "magnifierPill")->isVisible());

        QTest::keyClick(&surface.view(), Qt::Key_M);
        QTRY_VERIFY(!magnifier->isVisible());
    }
};

QTEST_MAIN(MagnifierTest)
#include "magnifier_test.moc"
