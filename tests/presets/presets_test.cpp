// Presets (saved Frames in presets.toml) and the auto-applied Preset.
//
// Two halves. The file: save, load, apply, hand edits survive (comments, other
// presets, key order), a bad file is reported once and never written over, a form
// Tarn can't edit safely is refused. The capture path: the marked Preset frames
// every capture that goes through the Thumbnail stack (fullscreen, area, window,
// repeat, Timer), never a Text capture or a Recording; B skips it once; Copy,
// Save, auto Copy / Save and the overflow auto-save all carry the Frame; a
// Palette gradient follows the wallpaper's colors and a deleted wallpaper never
// breaks a capture. Everything is written under the scratch XDG folders.
#include "test_env.h"

#include "document/capture_document.h"
#include "editor/editor.h"
#include "frame/frame.h"
#include "frame/gradient_background.h"
#include "frame/image_gradient_background.h"
#include "history/history.h"
#include "overlay/overlay.h"
#include "palette/palette.h"
#include "preset/preset_store.h"
#include "settings/settings.h"
#include "text/text_capture.h"
#include "thumbnails/thumbnail_stack.h"
#include "timer/countdown.h"
#include "wallpaper/wallpapers.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTest>

using tarn::EditorSession;
using tarn::Frame;
using tarn::Overlay;
using tarn::OverlayModel;
using tarn::PresetStore;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

namespace {

QImage shotImage() {
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

// One 128 x 80 monitor at scale 2: 64 x 40 logical pixels.
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0,"activeWorkspace":{"id":1},"specialWorkspace":{"id":0}}])";
const QByteArray kClients = // (the address lets activewindow name it)
    R"([{"address":"0xterm","mapped":true,"hidden":false,"at":[4,4],"size":[40,24],"workspace":{"id":1},"class":"term","title":"t","floating":false,"fullscreen":0,"focusHistoryID":0}])";

Frame gradientFrame(const QStringList &stops, int angle = 135) {
    Frame f;
    f.background.kind = "gradient";
    QVariantList list;
    for (const QString &s : stops)
        list << s;
    f.background.params = {{"stops", list}, {"angle", angle}};
    return f;
}

Frame plainFrame(const QString &color) {
    Frame f;
    f.background.kind = "plain";
    f.background.params = {{"color", color}};
    return f;
}

// A palette file a shell would write (the same shape the Background tests use).
QByteArray paletteFile(const QString &primary) {
    return QString("{\"primary\":\"%1\",\"on_primary\":\"#492900\",\"surface_container_low\":\"#211a14\",\"surface_container_lowest\":\"#130d07\","
                   "\"surface_container\":\"#251e17\",\"surface_container_high\":\"#302921\",\"surface_container_highest\":\"#3c332c\","
                   "\"surface_bright\":\"#403830\",\"on_surface\":\"#eee0d5\",\"on_surface_variant\":\"#d5c3b5\",\"outline\":\"#9d8e81\","
                   "\"outline_variant\":\"#50453a\"}")
        .arg(primary)
        .toUtf8();
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        qFatal("cannot write %s", qPrintable(path));
    f.write(bytes);
}

QRgb pixelOf(const QByteArray &png, int x, int y) { return QImage::fromData(png, "PNG").pixel(x, y); }

} // namespace

class PresetsTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    PresetStore &presets() { return env->daemon().service<PresetStore>(); }
    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    tarn::Editor &editor() { return env->daemon().service<tarn::Editor>(); }
    QString file() const { return env->scratch() + "/config/tarn/presets.toml"; }
    QString shellDir() const { return env->scratch() + "/state/shell"; }
    QString sharedDir() const { return env->scratch() + "/cache/tarn/shared"; }
    QByteArray text() const { return TestEnv::readFile(file()); }
    void writePresets(const QByteArray &bytes) { writeFile(file(), bytes); }
    void writeConfig(const QByteArray &bytes) { writeFile(env->scratch() + "/config/tarn/config.toml", bytes); }
    int notifications() const { return env->calls("notify-send").size(); }
    void later() { env->setNow(env->daemon().now().addSecs(2)); }

    // The marked Preset: a flat red gradient, padding 0.1 (8 px on the 80 px shot).
    void markRed() {
        Frame f = gradientFrame({"#ffff0000", "#ffff0000"});
        f.padding = 0.1;
        QVERIFY(presets().save("Red", f));
        QVERIFY(presets().setAuto("Red"));
    }
    // `capture fullscreen`; returns the new Thumbnail's id, or -1.
    int fullscreen() {
        const QList<int> before = stack().ids();
        if (!env->request({"capture", "fullscreen"}).ok)
            return -1;
        for (int id : stack().ids())
            if (!before.contains(id))
                return id;
        return -1;
    }
    int lastId() { return stack().ids().last(); }
    void drag(int x1 = 10, int y1 = 5, int x2 = 30, int y2 = 25) {
        model().pointerPressed(x1, y1, Qt::LeftButton);
        model().pointerMoved(x2, y2);
        model().pointerReleased(x2, y2, Qt::LeftButton);
    }
    bool isFramed(int id) { return !stack().edited(id).isEmpty(); }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setFixture("grim", "output.png", pngOf(shotImage()));
        env->setPointer(10, 10);
        env->setNow(QDateTime(QDate(2026, 11, 3), QTime(9, 30, 0)));
    }
    void cleanup() { env.reset(); }

    // ---- the file ----

    void aSavedPresetIsReadableTextAndLoadsAgainAfterARestart() {
        Frame f = gradientFrame({"#ffff9a5a", "#ff7b3fe4"}, 120);
        f.padding = 0.12;
        f.autoBalance = true;
        f.ratio = "16:9";
        f.alignH = -1;
        QVERIFY(presets().save("Sunset card", f));
        const QString t = QString::fromUtf8(text());
        QVERIFY(t.startsWith("# Tarn presets")); // a header explaining the file
        QVERIFY(t.contains("[preset.\"Sunset card\"]"));
        QVERIFY(t.contains("[preset.\"Sunset card\".background]"));
        QVERIFY(t.contains("kind = \"gradient\""));
        QVERIFY(t.contains("ratio = \"16:9\""));
        QVERIFY(t.contains("#ffff9a5a"));
        QCOMPARE(notifications(), 0);

        env->restartDaemon();
        QCOMPARE(presets().names(), QStringList{"Sunset card"});
        const Frame loaded = *presets().preset("Sunset card");
        QCOMPARE(loaded.background.kind, QString("gradient"));
        QCOMPARE(loaded.background.params.value("stops").toStringList(), (QStringList{"#ffff9a5a", "#ff7b3fe4"}));
        QCOMPARE(loaded.background.params.value("angle").toInt(), 120);
        QCOMPARE(loaded.padding, 0.12);
        QVERIFY(loaded.autoBalance);
        QCOMPARE(loaded.ratio, QString("16:9"));
        QCOMPARE(loaded.alignH, -1);
    }

    void everyKindOfBackgroundRoundTripsThroughTheFile() {
        Frame wallpaper;
        wallpaper.background.kind = "wallpaper";
        wallpaper.background.params = {{"path", "/home/me/Pictures/a b.jpg"}, {"current", true}};
        Frame blur;
        blur.background.kind = "blur";
        blur.background.params = {{"style", "dark"}};
        Frame palette = gradientFrame({"#ffaaaaaa", "#ff111111"});
        palette.background.params.insert("slots", QVariantList{"accent", "canvas"});
        palette.background.params.insert("id", "accent-canvas");
        Frame positioned;
        positioned.background.kind = "gradient";
        const QVariantMap stop1{{"color", "#ffff0000"}, {"pos", 0.0}}, stop2{{"color", "#ff0000ff"}, {"pos", 0.25}};
        positioned.background.params = {{"stops", QVariantList{stop1, stop2}}, {"angle", 90}};
        const Frame transparent = plainFrame("#00000000");
        const QList<QPair<QString, Frame>> all = {{"wall", wallpaper}, {"blur", blur}, {"pal", palette}, {"pos", positioned}, {"clear", transparent}};
        for (const auto &[name, frame] : all)
            QVERIFY2(presets().save(name, frame), qPrintable(name));
        env->restartDaemon();
        for (const auto &[name, frame] : all) {
            const auto loaded = presets().preset(name);
            QVERIFY2(loaded.has_value(), qPrintable(name));
            QCOMPARE(loaded->background.kind, frame.background.kind);
            QCOMPARE(loaded->background.params, frame.background.params);
        }
    }

    void aHandEditAppliesAtOnceWithoutARestart() {
        QVERIFY(presets().save("Mine", plainFrame("#ff00ff00")));
        QString t = QString::fromUtf8(text());
        t.replace("padding = 0.08", "padding = 0.2");
        writePresets(t.toUtf8());
        QCOMPARE(presets().preset("Mine")->padding, 0.2);
        // Whole presets typed by hand work too, bare keys and all.
        writePresets("[preset.hand]\npadding = 0.05\n[preset.hand.background]\nkind = \"plain\"\ncolor = \"#ff112233\"\n");
        QCOMPARE(presets().names(), QStringList{"hand"});
        QCOMPARE(presets().preset("hand")->background.params.value("color").toString(), QString("#ff112233"));
        QCOMPARE(notifications(), 0);
    }

    void savingAndDeletingLeaveEveryCommentAndOtherPresetAlone() {
        const QByteArray mine =
            "# My presets. Do not touch!\n"
            "auto = \"Keep\"   # the one I use\n"
            "\n"
            "# the plain one\n"
            "[preset.Keep]\n"
            "padding = 0.1   # roomy\n"
            "# a comment between keys\n"
            "inset = 0.02\n"
            "[preset.Keep.background]\n"
            "kind = \"plain\"\n"
            "color = \"#ff102030\"\n"
            "\n"
            "# my other one, written by hand\n"
            "[preset.Other]\n"
            "shadow = 0.05\n"
            "[preset.Other.background]\n"
            "kind = \"blur\"\n"
            "style = \"light\"\n"
            "\n"
            "# the end\n";
        writePresets(mine);
        QCOMPARE(presets().names(), (QStringList{"Keep", "Other"}));
        QCOMPARE(presets().autoName(), QString("Keep"));

        QVERIFY(presets().save("New", plainFrame("#ff445566")));
        QString t = QString::fromUtf8(text());
        QVERIFY(t.startsWith(QString::fromUtf8(mine))); // appended; everything before is byte for byte
        QVERIFY(t.contains("[preset.\"New\"]"));

        // Re-saving a preset replaces only that preset's own tables.
        Frame changed = plainFrame("#ff778899");
        changed.padding = 0.15;
        QVERIFY(presets().save("Other", changed));
        t = QString::fromUtf8(text());
        QVERIFY(t.contains("# My presets. Do not touch!"));
        QVERIFY(t.contains("auto = \"Keep\"   # the one I use"));
        QVERIFY(t.contains("# the plain one\n[preset.Keep]\npadding = 0.1   # roomy\n# a comment between keys\ninset = 0.02"));
        QVERIFY(t.contains("# my other one, written by hand\n[preset.\"Other\"]"));
        QVERIFY(t.contains("# the end"));
        QCOMPARE(presets().preset("Other")->padding, 0.15);
        QCOMPARE(presets().preset("Other")->background.kind, QString("plain"));
        QCOMPARE(presets().preset("Keep")->padding, 0.1);
        QCOMPARE(presets().preset("Keep")->inset, 0.02);

        // Saving the very same Frame again changes nothing at all.
        const QByteArray before = text();
        QVERIFY(presets().save("Other", changed));
        QCOMPARE(text(), before);

        // Deleting removes just that preset; the comment above it and the rest stay.
        QVERIFY(presets().remove("Other"));
        t = QString::fromUtf8(text());
        QVERIFY(!t.contains("[preset.Other"));
        QVERIFY(t.contains("# my other one, written by hand")); // a comment is the user's, not the preset's
        QVERIFY(t.contains("# the end"));
        QVERIFY(t.contains("[preset.Keep]\npadding = 0.1   # roomy"));
        QVERIFY(t.contains("[preset.\"New\"]"));
        QCOMPARE(presets().names(), (QStringList{"Keep", "New"}));
        QCOMPARE(notifications(), 0);
    }

    void theAutoLineIsAddedChangedAndRemovedInPlace() {
        writePresets("# hello\n\n[preset.A]\n[preset.A.background]\nkind = \"plain\"\ncolor = \"#ff000000\"\n");
        QVERIFY(presets().setAuto("A"));
        QCOMPARE(QString::fromUtf8(text()),
                 QString("# hello\nauto = \"A\"\n\n[preset.A]\n[preset.A.background]\nkind = \"plain\"\ncolor = \"#ff000000\"\n"));
        QVERIFY(presets().save("B", plainFrame("#ff111111")));
        QVERIFY(presets().setAuto("B"));
        QVERIFY(QString::fromUtf8(text()).contains("# hello\nauto = \"B\"\n"));
        QCOMPARE(presets().autoName(), QString("B"));
        QVERIFY(presets().setAuto(""));
        QVERIFY(!QString::fromUtf8(text()).contains("\nauto ="));
        QVERIFY(QString::fromUtf8(text()).startsWith("# hello\n"));
        // A name that is not a preset is refused and the file stays as it was.
        const QByteArray before = text();
        QVERIFY(!presets().setAuto("Nope"));
        QCOMPARE(text(), before);
    }

    void deletingTheAutoAppliedPresetClearsTheAutoLine() {
        markRed();
        QVERIFY(QString::fromUtf8(text()).contains("\nauto = \"Red\""));
        QVERIFY(presets().remove("Red"));
        QVERIFY(!QString::fromUtf8(text()).contains("\nauto = "));
        QVERIFY(!presets().hasAuto());
    }

    void aFileWithASyntaxErrorIsReportedOnceKeepsTheLastGoodReadingAndIsNeverWrittenOver() {
        QVERIFY(presets().save("Good", plainFrame("#ff00ff00")));
        QVERIFY(presets().setAuto("Good"));
        QByteArray broken = text();
        broken.replace("padding = 0.08", "padding = = 0.08");
        writePresets(broken);
        QCOMPARE(presets().names(), QStringList{"Good"}); // the last good reading
        QCOMPARE(presets().names(), QStringList{"Good"});
        QCOMPARE(notifications(), 1); // ONE notification, not one per look
        const QString body = env->calls("notify-send").first().join(' ');
        QVERIFY2(body.contains("line "), qPrintable(body));
        QVERIFY(!presets().save("Other", plainFrame("#ff111111")));
        QVERIFY(!presets().setAuto(""));
        QVERIFY(!presets().remove("Good"));
        QCOMPARE(text(), broken); // untouched
        // Fixing it by hand works again.
        writePresets(broken.replace("padding = = 0.08", "padding = 0.08"));
        const int calls = notifications();
        QVERIFY(presets().save("Other", plainFrame("#ff111111")));
        QCOMPARE(notifications(), calls);
    }

    void aBadPresetIsSkippedAndTheOthersStillApply() {
        writePresets(
            "[preset.Fine]\n[preset.Fine.background]\nkind = \"plain\"\ncolor = \"#ff000000\"\n"
            "[preset.NoBackground]\npadding = 0.1\n"
            "[preset.Odd]\n[preset.Odd.background]\nkind = \"hologram\"\n");
        QCOMPARE(presets().names(), QStringList{"Fine"});
        QCOMPARE(notifications(), 1);
        const QString body = env->calls("notify-send").first().join(' ');
        QVERIFY(body.contains("NoBackground") || body.contains("Odd"));
        QVERIFY(body.contains("1 more"));
    }

    void aFormTarnCannotEditSafelyIsRefusedAndNothingIsWritten() {
        const QByteArray dotted = "preset.Dot.padding = 0.1\npreset.Dot.background.kind = \"plain\"\npreset.Dot.background.color = \"#ff000000\"\n";
        writePresets(dotted);
        QCOMPARE(presets().names(), QStringList{"Dot"}); // readable all the same
        QVERIFY(!presets().save("Dot", plainFrame("#ff123456")));
        QVERIFY(!presets().remove("Dot"));
        QCOMPARE(text(), dotted);
        QVERIFY(notifications() >= 1);
    }

    void anAutoNameWithNoPresetMeansNoAutoPresetAndSaysSo() {
        writePresets("auto = \"Gone\"\n");
        QVERIFY(!presets().hasAuto());
        QCOMPARE(notifications(), 1);
        const int id = fullscreen();
        QVERIFY(id > 0);
        QVERIFY(!isFramed(id));
    }

    // ---- the auto-applied Preset reaches every capture path ----

    void aFullScreenCaptureIsFramedAndHistoryKeepsTheOriginal() {
        markRed();
        const int id = fullscreen();
        QVERIFY(id > 0);
        QVERIFY(isFramed(id));
        const QImage shown = stack().image(id);
        QVERIFY(shown.width() > 128 && shown.height() > 80);
        QCOMPARE(QColor(pixelOf(stack().edited(id), 1, 1)), QColor(255, 0, 0));
        // History's file is the untouched grim image.
        auto &history = env->daemon().service<tarn::History>();
        history.flush();
        QCOMPARE(QImage(history.pngPath(history.ids().first())), shotImage());
    }

    void noAutoPresetMeansNoChangeAtAll() {
        const int id = fullscreen();
        QVERIFY(id > 0);
        QVERIFY(!isFramed(id));
        QCOMPARE(stack().image(id).size(), QSize(128, 80));
    }

    void anAreaCaptureIsFramedAroundTheCrop() {
        markRed();
        QVERIFY(env->request({"capture", "area"}).ok);
        drag(); // 20 x 20 logical = 40 x 40 px
        const int id = lastId();
        QVERIFY(isFramed(id));
        // padding 0.1 of 40 px = 4 px on each side.
        QCOMPARE(stack().image(id).size(), QSize(48, 48));
        QCOMPARE(QColor(pixelOf(stack().edited(id), 0, 0)), QColor(255, 0, 0));
    }

    void aWindowCaptureIsFramed() {
        env->setFixture("hyprctl", "clients.json", kClients);
        markRed();
        QVERIFY(env->request({"capture", "area"}).ok);
        model().pointerMoved(10, 10);
        QVERIFY(model().hasHighlight());
        model().pointerPressed(10, 10, Qt::LeftButton);
        model().pointerReleased(10, 10, Qt::LeftButton);
        QCOMPARE(stack().count(), 1);
        QVERIFY(isFramed(lastId()));
    }

    void repeatingTheLastAreaIsFramed() {
        env->setFixture("hyprctl", "clients.json", kClients); // Last area is filed per app
        env->setFixture("hyprctl", "activewindow.json", kClients.mid(1, kClients.size() - 2));
        QVERIFY(env->request({"capture", "area"}).ok);
        drag();
        QVERIFY(!isFramed(lastId())); // no Preset marked yet
        markRed();
        QVERIFY(env->request({"capture", "repeat"}).ok);
        QCOMPARE(stack().count(), 2);
        QVERIFY(isFramed(lastId()));
    }

    void aTimerCaptureIsFramed() {
        markRed();
        QVERIFY(env->request({"capture", "timer"}).ok);
        model().keyPressed(Qt::Key_F);
        auto &countdown = env->daemon().service<tarn::Countdown>();
        QVERIFY(countdown.active());
        env->setNow(env->daemon().now().addSecs(4));
        countdown.tick();
        QVERIFY(!countdown.active());
        QCOMPARE(stack().count(), 1);
        QVERIFY(isFramed(lastId()));
    }

    void aTextCaptureAndARecordingAreNeverFramed() {
        markRed();
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QTRY_VERIFY(!env->daemon().service<tarn::TextCapture>().busy()); // Tesseract runs in the background
        QCOMPARE(stack().count(), 0); // Text capture makes no Thumbnail at all
        const int rec = stack().addRecording(TestEnv::defaultGrimPng(), env->daemon().now(), "/tmp/x.mp4");
        QVERIFY(rec > 0);
        QVERIFY(!isFramed(rec));
        QVERIFY(!stack().autoFrame(rec).has_value());
    }

    // ---- the B skip ----

    void bSkipsThePresetForThisCaptureOnlyAndShowsThePill() {
        markRed();
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(!model().noFrame());
        QSignalSpy changed(&model(), &OverlayModel::noFrameChanged);
        model().keyPressed(Qt::Key_B);
        QVERIFY(model().noFrame());
        QCOMPARE(changed.count(), 1);
        drag();
        QVERIFY(!isFramed(lastId()));
        QCOMPARE(stack().image(lastId()).size(), QSize(40, 40));
        // The next capture is framed again.
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(!model().noFrame());
        drag();
        QVERIFY(isFramed(lastId()));
    }

    void bAgainTakesTheSkipBack() {
        markRed();
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_B);
        model().keyPressed(Qt::Key_B);
        QVERIFY(!model().noFrame());
        drag();
        QVERIFY(isFramed(lastId()));
    }

    void bSkipsAWindowCaptureAndATimerCaptureToo() {
        env->setFixture("hyprctl", "clients.json", kClients);
        markRed();
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_B);
        model().pointerMoved(10, 10);
        model().pointerPressed(10, 10, Qt::LeftButton);
        model().pointerReleased(10, 10, Qt::LeftButton);
        QVERIFY(!isFramed(lastId()));

        QVERIFY(env->request({"capture", "timer"}).ok);
        model().keyPressed(Qt::Key_B);
        model().keyPressed(Qt::Key_F);
        env->setNow(env->daemon().now().addSecs(4));
        env->daemon().service<tarn::Countdown>().tick();
        QCOMPARE(stack().count(), 2);
        QVERIFY(!isFramed(lastId()));
    }

    void bDoesNothingWithoutAnAutoPresetOrInTextModeOrWithCtrl() {
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_B);
        QVERIFY(!model().noFrame()); // nothing to skip, so no pill
        model().cancel();
        markRed();
        QVERIFY(env->request({"capture", "text"}).ok);
        model().keyPressed(Qt::Key_B);
        QVERIFY(!model().noFrame());
        model().cancel();
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_B, Qt::ControlModifier); // Ctrl+B is not the skip
        QVERIFY(!model().noFrame());
    }

    // ---- what the Frame reaches ----

    void copyAndSaveOnTheThumbnailIncludeTheFrame() {
        markRed();
        const int a = fullscreen();
        QVERIFY(stack().copy(a));
        const QStringList shared = TestEnv::filesIn(sharedDir());
        QCOMPARE(shared.size(), 1);
        QVERIFY(QImage(sharedDir() + "/" + shared.first()).width() > 128);
        later();
        const int b = fullscreen();
        const QString path = stack().save(b);
        QVERIFY(!path.isEmpty());
        QVERIFY(QImage(path).width() > 128);
        QCOMPARE(QColor(QImage(path).pixel(1, 1)), QColor(255, 0, 0));
    }

    void autoCopyAndAutoSaveIncludeTheFrame() {
        markRed();
        writeConfig("copy_automatically = true\nsave_automatically = true\n");
        QTRY_VERIFY(env->daemon().service<tarn::Settings>().copyAutomatically());
        const int id = fullscreen();
        QVERIFY(id > 0);
        const QStringList saved = TestEnv::filesIn(env->screenshots());
        QCOMPARE(saved.size(), 1);
        const QImage savedImage(env->screenshots() + "/" + saved.first());
        QCOMPARE(savedImage.size(), stack().image(id).size());
        QCOMPARE(QColor(savedImage.pixel(1, 1)), QColor(255, 0, 0));
        const QStringList shared = TestEnv::filesIn(sharedDir());
        QCOMPARE(shared.size(), 1);
        QCOMPARE(QImage(sharedDir() + "/" + shared.first()), savedImage);
        // A skipped capture is saved plain.
        later();
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_B);
        drag();
        const QStringList both = TestEnv::filesIn(env->screenshots());
        QCOMPARE(both.size(), 2);
        QCOMPARE(QImage(env->screenshots() + "/" + both.last()).size(), QSize(40, 40));
    }

    void theOverflowAutoSaveIncludesTheFrame() {
        markRed();
        for (int i = 0; i < ThumbnailStack::kMaxThumbnails + 1; ++i) {
            later();
            QVERIFY(env->request({"capture", "fullscreen"}).ok);
        }
        QCOMPARE(stack().count(), ThumbnailStack::kMaxThumbnails);
        const QStringList saved = TestEnv::filesIn(env->screenshots());
        QCOMPARE(saved.size(), 1);
        const QImage image(env->screenshots() + "/" + saved.first());
        QVERIFY(image.width() > 128);
        QCOMPARE(QColor(image.pixel(1, 1)), QColor(255, 0, 0));
    }

    // ---- following the wallpaper, and the fallbacks ----

    void aPaletteGradientPresetFollowsTheWallpapersColors() {
        writeFile(shellDir() + "/palette.json", paletteFile("#fbb974"));
        writeConfig(("palette_file = \"" + shellDir() + "/palette.json\"\n").toUtf8());
        auto &palette = env->daemon().service<tarn::Palette>();
        QTRY_VERIFY(palette.fromFile());
        Frame f;
        f.background.kind = "gradient";
        f.background.params = tarn::paletteGradientParams("accent-canvas", palette);
        f.padding = 0.1;
        QVERIFY(presets().save("Palette", f));
        QVERIFY(presets().setAuto("Palette"));
        const int first = fullscreen();
        const QRgb before = pixelOf(stack().edited(first), 1, 1);
        // The wallpaper (and so the Palette) changes: the next capture is different.
        writeFile(shellDir() + "/palette.json", paletteFile("#3070e0"));
        QTRY_COMPARE(palette.accent(), QColor("#3070e0"));
        later();
        const int second = fullscreen();
        QVERIFY(second > 0);
        QVERIFY(pixelOf(stack().edited(second), 1, 1) != before);
        // The stored preset still names slots, so it keeps following.
        QVERIFY(QString::fromUtf8(text()).contains("slots"));
    }

    void withoutAPaletteAPaletteGradientFallsBackToTheClosestImageGradient() {
        QVERIFY(!env->daemon().service<tarn::Palette>().fromFile()); // no palette_file Setting
        Frame f;
        f.background.kind = "gradient";
        f.background.params = {{"id", "accent-canvas"},
                               {"stops", QVariantList{"#fffbb974", "#ff130d07"}},
                               {"slots", QVariantList{"accent", "canvas"}},
                               {"angle", 135}};
        QVERIFY(presets().save("Pal", f));
        const Frame resolved = *presets().resolved("Pal");
        QCOMPARE(resolved.background.kind, QString("image-gradient"));
        QCOMPARE(resolved.background.params, tarn::imageGradientParams(tarn::closestImageGradientForLegacy("dusk"))); // accent-canvas's fallback
        QVERIFY(!resolved.background.params.contains("slots"));
        // A hand-written one that names no known gradient picks by color.
        writePresets("[preset.Warm]\n[preset.Warm.background]\nkind = \"gradient\"\nslots = [\"accent\", \"text\"]\nstops = [\"#ffff9a5a\", \"#ffff8f8f\"]\n");
        const QString warm = tarn::closestImageGradient(QVariantList{"#ffff9a5a", "#ffff8f8f"});
        QCOMPARE(presets().resolved("Warm")->background.kind, QString("image-gradient"));
        QCOMPARE(presets().resolved("Warm")->background.params.value("name").toString(), warm);
        // And it still frames a capture.
        QVERIFY(presets().setAuto("Warm"));
        QVERIFY(isFramed(fullscreen()));
    }

    // Presets store an image gradient by its name.
    void anImageGradientPresetIsStoredByNameAndComesBackAsWritten() {
        Frame f;
        f.background.kind = "image-gradient";
        f.background.params = tarn::imageGradientParams("moonlit-ink");
        f.padding = 0.1;
        QVERIFY(presets().save("Ink", f));
        const QString t = QString::fromUtf8(text());
        QVERIFY(t.contains("kind = \"image-gradient\""));
        QVERIFY(t.contains("name = \"moonlit-ink\""));
        const Frame back = *presets().resolved("Ink");
        QCOMPARE(back.background.kind, QString("image-gradient"));
        QCOMPARE(back.background.params, tarn::imageGradientParams("moonlit-ink"));
        QVERIFY(!presets().isMissing("Ink"));
        QVERIFY(presets().setAuto("Ink"));
        QVERIFY(isFramed(fullscreen()));
    }

    void aPresetNamingAnOldBuiltInGradientResolvesToTheClosestImageGradient() {
        // As an older Tarn wrote it: kind gradient, the old id, its stops and angle.
        writePresets("[preset.Old]\npadding = 0.1\n[preset.Old.background]\nkind = \"gradient\"\nid = \"sunset\"\n"
                     "stops = [\"#ffff9a5a\", \"#ffff4f7b\", \"#ff7b3fe4\"]\nangle = 135\n");
        const Frame resolved = *presets().resolved("Old");
        QCOMPARE(resolved.background.kind, QString("image-gradient"));
        QCOMPARE(resolved.background.params,
                 tarn::imageGradientParams(tarn::closestImageGradient(QVariantList{"#ffff9a5a", "#ffff4f7b", "#ff7b3fe4"})));
        QVERIFY(!presets().isMissing("Old"));
        // The file itself is not rewritten.
        QVERIFY(QString::fromUtf8(text()).contains("id = \"sunset\""));
        // And an old Preset still frames a capture.
        QVERIFY(presets().setAuto("Old"));
        QVERIFY(isFramed(fullscreen()));
        // An old name with no stops written goes by the colors it used to have.
        writePresets("[preset.Bare]\n[preset.Bare.background]\nkind = \"gradient\"\nid = \"graphite\"\n");
        QCOMPARE(presets().resolved("Bare")->background.params, tarn::imageGradientParams(tarn::closestImageGradientForLegacy("graphite")));
    }

    void theCurrentWallpaperPresetFollowsTheWallpaperAndADeletedOneFallsBackQuietly() {
        const QString a = env->scratch() + "/wall/a.png", b = env->scratch() + "/wall/b.png";
        QDir().mkpath(env->scratch() + "/wall");
        QImage red(64, 40, QImage::Format_RGB32), blue(64, 40, QImage::Format_RGB32);
        red.fill(QColor(220, 20, 20));
        blue.fill(QColor(20, 20, 220));
        QVERIFY(red.save(a, "PNG") && blue.save(b, "PNG"));
        QString current = a;
        env->daemon().service<tarn::Wallpapers>().setCurrentSource([&current] { return current; });

        Frame f;
        f.background.kind = "wallpaper";
        f.background.params = {{"path", a}, {"current", true}};
        f.padding = 0.1;
        QVERIFY(presets().save("Wall", f));
        QVERIFY(presets().setAuto("Wall"));
        const int one = fullscreen();
        QCOMPARE(QColor(pixelOf(stack().edited(one), 1, 1)), QColor(220, 20, 20));

        current = b; // the shell applied another wallpaper
        later();
        const int two = fullscreen();
        QCOMPARE(QColor(pixelOf(stack().edited(two), 1, 1)), QColor(20, 20, 220));
        QVERIFY(!presets().isMissing("Wall"));

        // Both files are deleted: the capture still happens, framed in a quiet dark fallback.
        QFile::remove(a);
        QFile::remove(b);
        current = QString();
        later();
        const int three = fullscreen();
        QVERIFY(three > 0);
        QVERIFY(isFramed(three));
        const QColor corner(pixelOf(stack().edited(three), 1, 1));
        QVERIFY(corner.red() < 100 && corner.green() < 100 && corner.blue() < 100);
        QVERIFY(presets().isMissing("Wall")); // the panel's quiet mark
        QCOMPARE(notifications(), 0);         // and no notification
        env->daemon().service<tarn::Wallpapers>().setCurrentSource({});
    }

    void aFixedWallpaperPathThatIsGoneAlsoFallsBackQuietly() {
        Frame f;
        f.background.kind = "wallpaper";
        f.background.params = {{"path", env->scratch() + "/gone.jpg"}};
        QVERIFY(presets().save("Gone", f));
        QVERIFY(presets().setAuto("Gone"));
        QVERIFY(isFramed(fullscreen()));
        QVERIFY(presets().isMissing("Gone"));
        QCOMPARE(notifications(), 0);
    }

    // ---- the Editor ----

    void theEditorOpensAFramedCaptureOnItsOriginalWithTheFrameInTheDocument() {
        markRed();
        const int id = fullscreen();
        QVERIFY(id > 0);
        EditorSession *s = editor().open(id);
        QVERIFY(s);
        QCOMPARE(s->document().shot().size(), QSize(128, 80)); // the picture as captured
        QVERIFY(s->document().frame().active());
        QCOMPARE(s->document().flatten(), QImage::fromData(stack().edited(id), "PNG").convertToFormat(s->document().flatten().format()));
        // Done with no change keeps the Frame.
        const QSize size = stack().image(id).size();
        s->done();
        QVERIFY(isFramed(id));
        QCOMPARE(stack().image(id).size(), size);
    }

    void removingTheFrameInTheEditorTakesItOffTheCapture() {
        markRed();
        const int id = fullscreen();
        EditorSession *s = editor().open(id);
        QVERIFY(s);
        s->setFrameBackground("none");
        s->done();
        QVERIFY(!isFramed(id));
        QCOMPARE(stack().image(id).size(), QSize(128, 80));
        // ...and opening it again does not bring the auto Frame back.
        EditorSession *again = editor().open(id);
        QVERIFY(again);
        QVERIFY(!again->document().frame().active());
    }

    void thePanelSavesAppliesMarksAndDeletesPresets() {
        const int id = fullscreen();
        EditorSession *s = editor().open(id);
        QVERIFY(s);
        QVERIFY(!s->savePreset("Nothing yet")); // no Frame on the shot
        s->setFrameGradient("dusk-haze");
        s->setFrameValue("padding", 0.2);
        QVERIFY(s->savePreset("Sunset"));
        QCOMPARE(s->presets().size(), 1);
        QCOMPARE(s->presets().first().toMap().value("name").toString(), QString("Sunset"));
        QVERIFY(!s->presets().first().toMap().value("auto").toBool());
        s->setFrameBackground("none");
        QVERIFY(!s->document().frame().active());
        QVERIFY(s->applyPreset("Sunset"));
        QCOMPARE(s->document().frame().background.params.value("name").toString(), QString("dusk-haze"));
        QCOMPARE(s->document().frame().padding, 0.2);
        s->undo(); // applying is one undo step
        QVERIFY(!s->document().frame().active());
        QVERIFY(s->setAutoPreset("Sunset"));
        QVERIFY(s->presets().first().toMap().value("auto").toBool());
        QVERIFY(s->deletePreset("Sunset"));
        QVERIFY(s->presets().isEmpty());
        QVERIFY(!presets().hasAuto());
    }
};

QTEST_MAIN(PresetsTest)
#include "presets_test.moc"
