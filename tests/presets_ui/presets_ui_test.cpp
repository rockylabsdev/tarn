// Presets (QML): the "No frame" pill in the Overlay and the Presets section of the
// Background panel, off-screen. Look and feel are needs-eyes.
#include "test_env.h"

#include "editor/editor.h"
#include "editor_windows.h"
#include "overlay/overlay.h"
#include "overlay_surface.h"
#include "palette/palette.h"
#include "preset/preset_store.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>

using tarn::Frame;
using tarn::Overlay;
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

Frame redFrame() {
    Frame f;
    f.background.kind = "plain";
    f.background.params = {{"color", "#ffff0000"}};
    return f;
}

const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":256,"height":160,"scale":2.0,"transform":0}])";

QPoint centerOf(QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }

} // namespace

class PresetsUiTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

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

    void bShowsTheNoFramePillOnlyWhileASkipIsOnAndAPresetIsMarked() {
        auto &presets = env->daemon().service<tarn::PresetStore>();
        QVERIFY(presets.save("Red", redFrame()));
        QVERIFY(presets.setAuto("Red"));
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *pill = findItem(surface.view().rootObject(), "noFramePill");
        QVERIFY(pill);
        QVERIFY(!pill->isVisible());
        QTest::keyClick(&surface.view(), Qt::Key_B);
        QTRY_VERIFY(pill->isVisible());
        QTest::keyClick(&surface.view(), Qt::Key_B);
        QTRY_VERIFY(!pill->isVisible());
    }

    void withoutAMarkedPresetBDoesNotShowThePill() {
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        QTest::keyClick(&surface.view(), Qt::Key_B);
        QTest::qWait(60);
        QVERIFY(!findItem(surface.view().rootObject(), "noFramePill")->isVisible());
    }

    void thePanelListsSavesMarksAppliesAndDeletesPresets() {
        auto &stack = env->daemon().service<tarn::ThumbnailStack>();
        auto &editor = env->daemon().service<tarn::Editor>();
        tarn::EditorWindows windows{editor, env->daemon().service<tarn::Palette>()};
        auto &presets = env->daemon().service<tarn::PresetStore>();
        env->setFixture("hyprctl", "monitors.json", R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":2560,"height":1440,"scale":1.0,"transform":0}])");
        QVERIFY(presets.save("Existing", redFrame()));
        QImage shot(400, 300, QImage::Format_RGB32);
        shot.fill(qRgb(40, 120, 200));
        const int id = stack.add(pngOf(shot), env->daemon().now());
        stack.edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && view->status() == QQuickView::Ready && QTest::qWaitForWindowExposed(view));
        auto *session = editor.session(id);
        auto *root = view->rootObject();
        QTest::keyClick(view, Qt::Key_B); // the panel
        QTest::qWait(60);
        auto *scroll = findItem(root, "frameScroll");
        scroll->setProperty("contentY", scroll->property("contentHeight").toDouble() - scroll->height());
        QTest::qWait(80);

        QVERIFY(findItem(root, "presetRow-0"));
        QVERIFY(!findItem(root, "presetRow-1"));
        // Applying one puts its Frame on the shot.
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(root, "presetRow-0")) - QPoint(30, 0));
        QVERIFY(session->document().frame().active());
        QCOMPARE(session->document().frame().background.params.value("color").toString(), QString("#ffff0000"));

        // Saving the current Frame under a typed name adds a row (and the file).
        auto *name = findItem(root, "presetName");
        name->forceActiveFocus();
        name->setProperty("text", "Mine");
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(root, "savePresetButton")));
        QTRY_VERIFY(findItem(root, "presetRow-1"));
        QVERIFY(presets.contains("Mine"));
        QTest::qWait(100); // the rows lay out again

        // Auto marks one; clicking again unmarks.
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(root, "presetAuto-0")));
        QCOMPARE(presets.autoName(), QString("Existing"));
        QTest::qWait(100);
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(root, "presetAuto-0")));
        QVERIFY(!presets.hasAuto());
        QTest::qWait(100);

        // Delete removes the row.
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(root, "presetDelete-0")));
        QTRY_VERIFY(!findItem(root, "presetRow-1"));
        QCOMPARE(presets.names(), QStringList{"Mine"});
    }
};

QTEST_MAIN(PresetsUiTest)
#include "presets_ui_test.moc"
