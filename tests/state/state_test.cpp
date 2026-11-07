// Shift+Print repeats the focused app's Last area; L brings it back; the remembered-state file.
#include "test_env.h"

#include "overlay/overlay.h"
#include "state/remembered_state.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using tarn::Overlay;
using tarn::OverlayModel;
using tarn::RememberedState;
using tarn::ThumbnailStack;
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
// One 128 x 80 monitor at scale 2: 64 x 40 logical pixels, at (x, 0) in the layout.
QByteArray monitorAt(int x, const char *name = "DP-1") {
    return QByteArray(R"([{"id":0,"name":")") + name + R"(","x":)" + QByteArray::number(x) +
           R"(,"y":0,"width":128,"height":80,"scale":2.0,"transform":0,"activeWorkspace":{"id":1},"specialWorkspace":{"id":0}}])";
}
QJsonObject client(const QString &app, int x, int y, int w, int h) {
    return {{"address", "0x" + app}, {"mapped", true}, {"hidden", false}, {"workspace", QJsonObject{{"id", 1}}},
            {"at", QJsonArray{x, y}}, {"size", QJsonArray{w, h}}, {"fullscreen", 0}, {"floating", false},
            {"class", app}, {"title", app}, {"focusHistoryID", 0}};
}
} // namespace

class StateTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    QString stateFile() const { return env->scratch() + "/state/tarn/state.toml"; }
    QString stateText() const { return QString::fromUtf8(TestEnv::readFile(stateFile())); }
    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    QImage firstImage() { return stack().image(stack().data(stack().index(0), ThumbnailStack::IdRole).toInt()); }
    // Two side-by-side windows on a monitor at layout x = `offset`; `focused` has the focus.
    void setWindows(const QString &focused, int offset = 0) {
        const QJsonArray clients{client("left", offset, 0, 30, 40), client("right", offset + 32, 0, 30, 40)};
        env->setFixture("hyprctl", "clients.json", QJsonDocument(clients).toJson());
        QByteArray active = "{}";
        for (const auto &c : clients)
            if (c.toObject().value("class").toString() == focused)
                active = QJsonDocument(c.toObject()).toJson();
        env->setFixture("hyprctl", "activewindow.json", active);
    }
    // Print, then drag (x1,y1) to (x2,y2) on the model and let go.
    void drag(int x1, int y1, int x2, int y2) {
        QVERIFY(env->request({"capture", "area"}).ok);
        model().pointerPressed(x1, y1, Qt::LeftButton);
        model().pointerMoved(x2, y2);
        model().pointerReleased(x2, y2, Qt::LeftButton);
    }
    // 10..30 x 5..25: lies wholly over "left".
    void dragOverLeft() { drag(10, 5, 30, 25); }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", monitorAt(0));
        env->setFixture("grim", "output.png", pngOf(gradient()));
        env->setPointer(10, 10);
        setWindows("left");
    }
    void cleanup() { env.reset(); }

    // ---- filing ----

    void anAreaIsFiledUnderTheAppItOverlapsMostNotTheFocusedOne() {
        setWindows("right");
        drag(2, 5, 42, 25); // 28 columns of it over "left", 10 over "right"
        QCOMPARE(stack().count(), 1);
        const QString text = stateText();
        QVERIFY2(text.contains("[last_areas]\n"), qPrintable(text));
        QVERIFY2(text.contains("left = \"2,5,40,20\""), qPrintable(text));
        QVERIFY2(!text.contains("right"), qPrintable(text));
    }

    void anAreaOverBareDesktopIsFiledUnderNoApp() {
        env->setFixture("hyprctl", "clients.json", "[]");
        env->setFixture("hyprctl", "activewindow.json", "{}");
        dragOverLeft();
        QCOMPARE(stack().count(), 1);
        QVERIFY2(!stateText().contains("last_areas"), qPrintable(stateText()));
    }

    void theStateFileIsNotTheSettingsFile() {
        dragOverLeft();
        const QByteArray settings = TestEnv::readFile(env->scratch() + "/config/tarn/config.toml");
        QVERIFY(!settings.isEmpty()); // the Settings file exists on its own...
        QVERIFY(!settings.contains("last_area")); // ...and holds nothing remembered
        QVERIFY(QFile::exists(stateFile()));
    }

    void anAppClassWithDotsIsOneKeyAndComesBack() {
        const QJsonArray clients{client("org.gnome.Nautilus", 0, 0, 64, 40)};
        env->setFixture("hyprctl", "clients.json", QJsonDocument(clients).toJson());
        env->setFixture("hyprctl", "activewindow.json", QJsonDocument(clients[0].toObject()).toJson());
        dragOverLeft();
        const QImage first = firstImage();
        env->restartDaemon();
        QVERIFY(env->request({"capture", "repeat"}).ok);
        QCOMPARE(stack().count(), 1);
        QCOMPARE(firstImage(), first);
    }

    // ---- Shift+Print ----

    void repeatAfterARestartCapturesTheFocusedAppsAreaWithNoOverlay() {
        dragOverLeft();
        const QImage first = firstImage();
        QVERIFY(!first.isNull());
        QCOMPARE(first.size(), QSize(40, 40));
        env->restartDaemon();
        QCOMPARE(stack().count(), 0);

        QVERIFY(env->request({"capture", "repeat"}).ok);
        QVERIFY(!env->daemon().service<Overlay>().isOpen());
        QCOMPARE(stack().count(), 1);
        QCOMPARE(firstImage(), first);
    }

    void repeatTakesTheAreaOfTheFocusedAppAndNotTheLastOneTaken() {
        dragOverLeft();                // left: 10,5 20x20
        const QImage left = firstImage();
        setWindows("right");
        drag(34, 2, 54, 32);           // right: 34,2 20x30
        env->restartDaemon();
        setWindows("left");
        QVERIFY(env->request({"capture", "repeat"}).ok);
        QCOMPARE(stack().count(), 1);
        QCOMPARE(firstImage(), left);
    }

    void repeatWhenTheFocusedAppHasNoAreaOpensTheOverlay() {
        dragOverLeft();
        env->restartDaemon();
        setWindows("right");
        QVERIFY(env->request({"capture", "repeat"}).ok);
        QVERIFY(env->daemon().service<Overlay>().isOpen());
        QCOMPARE(stack().count(), 0);
    }

    void repeatWithNoSavedAreaOpensTheOverlay() {
        QVERIFY(env->request({"capture", "repeat"}).ok);
        QVERIFY(env->daemon().service<Overlay>().isOpen());
        QCOMPARE(stack().count(), 0);
    }

    void repeatWithNoFocusedWindowOpensTheOverlay() {
        dragOverLeft();
        env->restartDaemon();
        setWindows({});
        QVERIFY(env->request({"capture", "repeat"}).ok);
        QVERIFY(env->daemon().service<Overlay>().isOpen());
    }

    void repeatAfterTheStateFileIsDeletedOpensTheOverlay() {
        dragOverLeft();
        QVERIFY(QFile::remove(stateFile()));
        env->restartDaemon();
        QVERIFY(env->request({"capture", "repeat"}).ok);
        QVERIFY(env->daemon().service<Overlay>().isOpen());
    }

    void aGarbledStateFileIsJustNothingRemembered() {
        QDir().mkpath(QFileInfo(stateFile()).absolutePath());
        QFile f(stateFile());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\x01\x02 not toml [[[ = = \n[last_areas]\nleft = oops\n");
        f.close();
        QVERIFY(env->request({"capture", "repeat"}).ok);
        QVERIFY(env->daemon().service<Overlay>().isOpen());
    }

    void areasAreKeptInScreenCoordinatesOnAMonitorNotAtTheOrigin() {
        env->setFixture("hyprctl", "monitors.json", monitorAt(100));
        env->setPointer(110, 10);
        setWindows("left", 100);
        dragOverLeft();
        QVERIFY2(stateText().contains("left = \"110,5,20,20\""), qPrintable(stateText()));
        const QImage first = firstImage();
        env->restartDaemon();
        QVERIFY(env->request({"capture", "repeat"}).ok);
        QVERIFY(!env->daemon().service<Overlay>().isOpen());
        QCOMPARE(firstImage(), first);
    }

    void repeatWhenTheAreaIsOnNoMonitorAnyMoreOpensTheOverlay() {
        dragOverLeft();
        env->restartDaemon();
        env->setFixture("hyprctl", "monitors.json", monitorAt(500, "HDMI-A-1"));
        env->setPointer(510, 10);
        setWindows("left", 500);
        QVERIFY(env->request({"capture", "repeat"}).ok);
        QVERIFY(env->daemon().service<Overlay>().isOpen());
        QCOMPARE(stack().count(), 0);
    }

    // ---- v1 ----

    void v1sSingleLastAreaIsIgnored() {
        QDir().mkpath(QFileInfo(stateFile()).absolutePath());
        QFile f(stateFile());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[last_area]\nmonitor = \"DP-1\"\nx = 10\ny = 5\nwidth = 20\nheight = 20\n");
        f.close();
        QVERIFY(env->request({"capture", "repeat"}).ok);
        QVERIFY(env->daemon().service<Overlay>().isOpen());
        QCOMPARE(stack().count(), 0);

        // ...and L finds nothing either.
        model().keyPressed(Qt::Key_L);
        QVERIFY(!model().kept());
    }

    // ---- L ----

    void lRestoresTheFocusedAppsAreaAsAKeptSelection() {
        dragOverLeft();
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(!model().hasSelection());
        model().keyPressed(Qt::Key_L);
        QVERIFY(model().kept());
        QCOMPARE(model().selection(), QRect(10, 5, 20, 20));
        QCOMPARE(stack().count(), 1); // kept to adjust, not taken again
    }

    void lWorksFromTheModeBarButtonToo() {
        dragOverLeft();
        QVERIFY(env->request({"capture", "area"}).ok);
        model().restoreLastArea();
        QVERIFY(model().kept());
    }

    void lBringsBackTheAreaInMonitorLocalPixelsOnAMonitorNotAtTheOrigin() {
        env->setFixture("hyprctl", "monitors.json", monitorAt(100));
        env->setPointer(110, 10);
        setWindows("left", 100);
        dragOverLeft();
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_L);
        QCOMPARE(model().selection(), QRect(10, 5, 20, 20));
    }

    void lDoesNothingWhenTheFocusedAppHasNoArea() {
        dragOverLeft();
        setWindows("right");
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_L);
        QVERIFY(!model().kept());
        QVERIFY(model().active());
    }

    // The store itself: what later tickets will lean on.
    void valuesRoundTripThroughTheFile() {
        const QString path = env->scratch() + "/s/state.toml";
        {
            RememberedState s(path);
            s.setValue("magnifier.on", true);
            s.setValue("tools.size", 4);
            s.setValue("tools.opacity", 0.5);
            s.setValue("save.folder", QStringLiteral("/home/a \"b\"/c"));
        }
        RememberedState s(path);
        QCOMPARE(s.value("magnifier.on").toBool(), true);
        QCOMPARE(s.value("tools.size").toInt(), 4);
        QCOMPARE(s.value("tools.opacity").toDouble(), 0.5);
        QCOMPARE(s.value("save.folder").toString(), QStringLiteral("/home/a \"b\"/c"));
        QCOMPARE(s.value("nope", 7).toInt(), 7);
    }
};

QTEST_MAIN(StateTest)
#include "state_test.moc"
