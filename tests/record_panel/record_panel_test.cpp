// The Record panel's effect on the recorder. Core: the recorder's arguments
// for each audio/format/pointer combination, W x H resizing, remembered toggles,
// the software-pointer case (through the stubbed hyprctl getoption). UI: the
// panel's fields and toggles in the off-screen Overlay surface.
#include "test_env.h"

#include "hyprland/hyprland_bridge.h"
#include "overlay/overlay.h"
#include "overlay_surface.h"
#include "palette/palette.h"
#include "recording/record_options.h"
#include "recording/recording_session.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

using tarn::OverlayModel;
using tarn::RecordingSession;
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
QPoint centerOf(QQuickItem *item) {
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}
constexpr int Enter = Qt::Key_Return;
} // namespace

class RecordPanelTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    RecordingSession &session() { return env->daemon().service<RecordingSession>(); }
    tarn::RecordOptions &options() { return env->daemon().service<tarn::RecordOptions>(); }
    OverlayModel &model() { return env->daemon().service<tarn::Overlay>().model(); }

    // A 400 x 300 logical monitor at scale 2.
    void monitor(int w = 400, int h = 300) {
        env->setFixture("hyprctl", "monitors.json",
                        QStringLiteral(R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":%1,"height":%2,"scale":2.0,"transform":0}])")
                            .arg(w * 2).arg(h * 2).toUtf8());
        QImage img(w * 2, h * 2, QImage::Format_RGB32);
        img.fill(Qt::gray);
        QByteArray png;
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        img.save(&b, "PNG");
        env->setFixture("grim", "output.png", png);
        env->setPointer(10, 10);
    }
    void writeConfig(const QByteArray &text) {
        QDir().mkpath(env->scratch() + "/config/tarn");
        QFile f(env->scratch() + "/config/tarn/config.toml");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text);
    }
    // Ctrl+Print, a drag (10,5)-(30,25) logical (40 x 40 device px).
    void pick() {
        QVERIFY(env->request({"capture", "record"}).ok);
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerMoved(30, 25);
        model().pointerReleased(30, 25, Qt::LeftButton);
    }
    QStringList recorderCall() {
        [&] { QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000); }();
        const auto calls = env->calls("gpu-screen-recorder");
        return calls.isEmpty() ? QStringList{} : calls.first();
    }
    static QString after(const QStringList &args, const QString &flag) {
        const int i = args.indexOf(flag);
        return i < 0 ? QString() : args.value(i + 1);
    }
    // Opens the Record mode Overlay in a surface and keeps a 300 x 240 Selection.
    void openPanel(tarn::OverlaySurface &surface) {
        QVERIFY(env->request({"capture", "record"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        QTest::mousePress(&surface.view(), Qt::LeftButton, {}, QPoint(20, 10));
        QTest::mouseMove(&surface.view(), QPoint(320, 250));
        QTest::mouseRelease(&surface.view(), Qt::LeftButton, {}, QPoint(320, 250));
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        monitor();
    }
    void cleanup() { env.reset(); }

    // ---- recorder arguments ----

    void noAudioByDefault() {
        pick();
        model().keyPressed(Enter);
        QVERIFY(!recorderCall().contains("-a"));
        QVERIFY(!session().micOn());
    }

    void micAloneIsTheDefaultInput() {
        options().setMicOn(true);
        pick();
        model().keyPressed(Enter);
        QCOMPARE(after(recorderCall(), "-a"), QString("default_input"));
        QVERIFY(session().micOn()); // the bar's mic glyph
    }

    void systemAudioAloneIsTheDefaultOutput() {
        options().setSystemAudio(true);
        pick();
        model().keyPressed(Enter);
        QCOMPARE(after(recorderCall(), "-a"), QString("default_output"));
        QVERIFY(!session().micOn());
    }

    void bothAreMixedIntoOneTrack() {
        options().setMicOn(true);
        options().setSystemAudio(true);
        pick();
        model().keyPressed(Enter);
        const QStringList args = recorderCall();
        QCOMPARE(args.count("-a"), 1);
        QCOMPARE(after(args, "-a"), QString("default_output|default_input"));
        QCOMPARE(args.at(args.size() - 2), QString("-o")); // the file stays last
    }

    void aGifHasNoSoundWhateverIsToggled() {
        options().setMicOn(true);
        options().setSystemAudio(true);
        pick();
        model().confirmGif();
        const QStringList args = recorderCall();
        QVERIFY(!args.contains("-a"));
        QVERIFY(!session().micOn());
        QVERIFY(options().micOn()); // ... the choice is still there for the next video
        QVERIFY(options().systemAudio());
    }

    void argumentsForEachCombination() {
        const QRect r(0, 0, 40, 40);
        using S = RecordingSession;
        QVERIFY(!S::recorderArguments(r, true, "/o.mp4").contains("-a"));
        QCOMPARE(S::recorderArguments(r, true, "/o.mp4", true, false).mid(12, 2), (QStringList{"-a", "default_input"}));
        QCOMPARE(S::recorderArguments(r, true, "/o.mp4", false, true).mid(12, 2), (QStringList{"-a", "default_output"}));
        QCOMPARE(S::recorderArguments(r, true, "/o.mp4", true, true).mid(12, 2), (QStringList{"-a", "default_output|default_input"}));
    }

    // ---- remembered ----

    void togglesSurviveARestart() {
        options().setMicOn(true);
        options().setShowClicks(true);
        env->restartDaemon();
        QVERIFY(options().micOn());
        QVERIFY(!options().systemAudio());
        QVERIFY(options().showClicks());
        options().setSystemAudio(true);
        options().setMicOn(false);
        env->restartDaemon();
        QVERIFY(!options().micOn());
        QVERIFY(options().systemAudio());
    }

    // ---- the pointer ----

    void thePointerIsInByDefault() {
        pick();
        model().keyPressed(Enter);
        QCOMPARE(after(recorderCall(), "-cursor"), QString("yes"));
    }

    void theSettingOffLeavesThePointerOutOnAHardwarePointer() {
        writeConfig("show_pointer_in_recordings = false\n");
        env->restartDaemon();
        pick();
        model().keyPressed(Enter);
        QCOMPARE(after(recorderCall(), "-cursor"), QString("no"));
        QVERIFY(!options().softwarePointer());
    }

    void underASoftwarePointerTheSettingIsMoot_data() {
        QTest::addColumn<int>("noHardwareCursors");
        QTest::newRow("software (1)") << 1;
        QTest::newRow("auto (2)") << 2;
    }
    void underASoftwarePointerTheSettingIsMoot() {
        QFETCH(int, noHardwareCursors);
        env->setFixture("hyprctl", "no_hardware_cursors.json",
                        QStringLiteral(R"({"option":"cursor:no_hardware_cursors","int":%1,"set":true})").arg(noHardwareCursors).toUtf8());
        writeConfig("show_pointer_in_recordings = false\n");
        env->restartDaemon();
        pick(); // opening in Record mode asks Hyprland (the panel shows its note)
        QVERIFY(options().softwarePointer());
        QVERIFY(!options().pointerSetting()); // the Setting itself is untouched
        model().keyPressed(Enter);
        QCOMPARE(after(recorderCall(), "-cursor"), QString("yes")); // it is baked in anyway
        QVERIFY(env->calls("hyprctl").contains(QStringList{"getoption", "cursor:no_hardware_cursors", "-j"}));
    }

    void anUnreadableHyprlandCountsAsAHardwarePointer() {
        env->setMode("hyprctl", "fail");
        QVERIFY(!env->daemon().service<tarn::HyprlandBridge>().softwarePointer());
    }

    // ---- W x H ----

    void widthAndHeightResizeTheSelectionWithTheTopLeftFixed() {
        pick(); // (10,5) 20 x 20 logical = 40 x 40 device px
        QCOMPARE(model().pixelWidth(), 40);
        model().setPixelSize(200, 100);
        QCOMPARE(model().selection(), QRect(10, 5, 100, 50));
        QCOMPARE(model().sizeLabel(), QString("200 × 100"));
        model().setPixelSize(1, 1);
        QCOMPARE(model().selection().topLeft(), QPoint(10, 5));
        QVERIFY(!model().selection().isEmpty()); // at least one logical px
        model().setPixelSize(100000, 100000);    // never past the monitor
        QCOMPARE(model().selection(), QRect(10, 5, 390, 295));
    }

    void theResizedSelectionIsWhatIsRecorded() {
        pick();
        model().setPixelSize(200, 100);
        model().keyPressed(Enter);
        QCOMPARE(after(recorderCall(), "-region"), QString("200x100+20+10"));
    }

    void sizeFieldsDoNothingWhenNothingIsKept() {
        QVERIFY(env->request({"capture", "record"}).ok);
        model().setPixelSize(50, 50);
        QVERIFY(model().selection().isEmpty());
    }

    // ---- the panel on screen (off-screen) ----

    void thePanelHoldsTheFieldsTogglesAndMenu() {
        tarn::OverlaySurface surface(env->daemon().service<tarn::Overlay>(), env->daemon().service<tarn::Palette>());
        openPanel(surface);
        auto *root = surface.view().rootObject();
        QTRY_VERIFY(findItem(root, "recordPanel")->isVisible());
        for (const char *name : {"widthField", "heightField", "settingsButton", "micToggle", "systemAudioToggle",
                                 "showClicksToggle", "recordVideoButton", "recordGifButton"})
            QVERIFY2(findItem(root, name), name);
        // The fields show the Selection's device-pixel size.
        QCOMPARE(findItem(root, "widthField")->childItems().first()->property("text").toString(), QString("600"));
        QCOMPARE(findItem(root, "heightField")->childItems().first()->property("text").toString(), QString("480"));
    }

    void typingInTheFieldsResizesLiveAndTabMovesOn() {
        tarn::OverlaySurface surface(env->daemon().service<tarn::Overlay>(), env->daemon().service<tarn::Palette>());
        openPanel(surface);
        auto *root = surface.view().rootObject();
        auto *wField = findItem(root, "widthField");
        auto *hField = findItem(root, "heightField");
        QTRY_VERIFY(wField->isVisible());
        QTest::mouseClick(&surface.view(), Qt::LeftButton, {}, centerOf(wField));
        auto *wInput = wField->childItems().first();
        QTRY_VERIFY(wInput->hasActiveFocus());
        QTest::keyClick(&surface.view(), Qt::Key_A, Qt::ControlModifier); // select all
        for (const char c : QByteArray("300")) QTest::keyClick(&surface.view(), c);
        QCOMPARE(model().selection().width(), 150);
        QCOMPARE(model().selection().topLeft(), QPoint(20, 10));
        QTest::keyClick(&surface.view(), Qt::Key_Tab);
        auto *hInput = hField->childItems().first();
        QTRY_VERIFY(hInput->hasActiveFocus());
        QTest::keyClick(&surface.view(), Qt::Key_A, Qt::ControlModifier);
        for (const char c : QByteArray("200")) QTest::keyClick(&surface.view(), c);
        QCOMPARE(model().selection(), QRect(20, 10, 150, 100));
        // Enter hands the keys back to the Overlay (it does not start the Recording).
        QTest::keyClick(&surface.view(), Qt::Key_Return);
        QVERIFY(model().active());
        QVERIFY(!hInput->hasActiveFocus());
    }

    void togglesInThePanelAreRememberedAndAudioGraysOverRecordGif() {
        tarn::OverlaySurface surface(env->daemon().service<tarn::Overlay>(), env->daemon().service<tarn::Palette>());
        openPanel(surface);
        auto *root = surface.view().rootObject();
        auto *mic = findItem(root, "micToggle");
        QTRY_VERIFY(mic->isVisible());
        QCOMPARE(mic->opacity(), 1.0);
        QTest::mouseClick(&surface.view(), Qt::LeftButton, {}, centerOf(mic));
        QVERIFY(options().micOn());
        QTest::mouseClick(&surface.view(), Qt::LeftButton, {}, centerOf(findItem(root, "showClicksToggle")));
        QVERIFY(options().showClicks());
        // Over Record GIF the audio toggles are grayed out and do nothing.
        QTest::mouseMove(&surface.view(), centerOf(findItem(root, "recordGifButton")));
        QTRY_VERIFY(mic->opacity() < 1.0);
        QVERIFY(!mic->isEnabled());
        env->restartDaemon();
        QVERIFY(options().micOn());
    }

    void softwarePointerShowsTheNote() {
        env->setFixture("hyprctl", "no_hardware_cursors.json", R"({"option":"cursor:no_hardware_cursors","int":1,"set":true})");
        tarn::OverlaySurface surface(env->daemon().service<tarn::Overlay>(), env->daemon().service<tarn::Palette>());
        openPanel(surface);
        QTRY_VERIFY(findItem(surface.view().rootObject(), "pointerNote")->isVisible());
    }

    void hardwarePointerHasNoNote() {
        tarn::OverlaySurface surface(env->daemon().service<tarn::Overlay>(), env->daemon().service<tarn::Palette>());
        openPanel(surface);
        QVERIFY(!findItem(surface.view().rootObject(), "pointerNote")->isVisible());
    }

    void theSettingsButtonOpensTheSettingsFileAndLeavesTheOverlay() {
        tarn::OverlaySurface surface(env->daemon().service<tarn::Overlay>(), env->daemon().service<tarn::Palette>());
        openPanel(surface);
        auto *button = findItem(surface.view().rootObject(), "settingsButton");
        QTRY_VERIFY(button->isVisible());
        QTest::mouseClick(&surface.view(), Qt::LeftButton, {}, centerOf(button));
        QTRY_COMPARE(env->calls("xdg-open").size(), 1);
        QVERIFY(env->calls("xdg-open").first().first().endsWith("config.toml"));
        QVERIFY(!model().active());
    }
};

QTEST_MAIN(RecordPanelTest)
#include "record_panel_test.moc"
