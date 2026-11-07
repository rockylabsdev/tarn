// Pin. Everything below the QML window: a Pin from a Thumbnail or from the
// Editor, where and how big it opens (the per-window Hyprland rule Tarn adds),
// resize and opacity, and Copy / Save as… / Edit / Close all pins. What it
// cannot check is needs-eyes: that Hyprland really keeps the window on top on
// every workspace, and how scroll resizing feels.
#include "test_env.h"

#include "editor/editor.h"
#include "pin/pins.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

using tarn::Editor;
using tarn::Pins;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

namespace {

QImage testShot(int w = 200, int h = 120) {
    QImage image(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            image.setPixel(x, y, qRgb(30 + x % 200, 60 + y % 150, 120));
    return image;
}

QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

} // namespace

class PinTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    QDateTime when{QDate(2026, 11, 3), QTime(9, 30, 0)};

    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    Pins &pins() { return env->daemon().service<Pins>(); }
    Editor &editor() { return env->daemon().service<Editor>(); }
    // A 200 x 120 px capture of a 100 x 60 area (monitor scale 2) at layout (300, 200).
    int addShot(const QRect &origin = QRect(300, 200, 100, 60)) {
        const int id = stack().add(pngOf(testShot()), when);
        stack().setOrigin(id, origin);
        return id;
    }
    // The Lua text of hyprctl eval call n (0-based).
    QString evalCall(int n) const {
        QList<QStringList> evals;
        for (const auto &call : env->calls("hyprctl"))
            if (!call.isEmpty() && call.first() == "eval")
                evals << call;
        return n < evals.size() ? evals.at(n).value(1) : QString();
    }
    int evalCount() const {
        int n = 0;
        for (const auto &call : env->calls("hyprctl"))
            n += !call.isEmpty() && call.first() == "eval";
        return n;
    }
    QString shared() const { return env->scratch() + "/cache/tarn/shared"; }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        pins(); // started with the daemon in the app: listens for Pin on a Thumbnail
    }
    void cleanup() { env.reset(); }

    // ---- opening ----

    void pinOnAThumbnailTakesItOffTheStackAndFloatsThatImage() {
        const int id = addShot();
        QSignalSpy opened(&pins(), &Pins::opened);
        stack().requestPin(id); // the corner button
        QCOMPARE(opened.size(), 1);
        const int pin = opened.first().first().toInt();
        QCOMPARE(pins().ids(), QList<int>{pin});
        QCOMPARE(stack().count(), 0);
        QCOMPARE(pins().png(pin), pngOf(testShot()));
        QCOMPARE(pins().opacity(pin), 1.0);
    }

    void aPinOpensWhereItWasCapturedAtItsCapturedSize() {
        const int pin = pins().pin(addShot(QRect(300, 200, 100, 60)));
        QCOMPARE(pins().openingRect(pin), QRect(300, 200, 100, 60)); // 200 px over 100 logical px: scale 2
        // The per-window rule: this title, monitor-local position, logical size.
        const QString rule = evalCall(0);
        QVERIFY2(rule.contains("hl.window_rule("), qPrintable(rule));
        QVERIFY2(rule.contains("title=\"^Tarn pin " + QString::number(pin) + "$\""), qPrintable(rule));
        // Values go in as strings, the form the prototype's rules used.
        QVERIFY2(rule.contains("move={\"300\", \"200\"}"), qPrintable(rule));
        QVERIFY2(rule.contains("size={\"100\", \"60\"}"), qPrintable(rule));
    }

    void pinRulesReuseAFixedPoolOfNamesSoTheyDoNotAccumulate() {
        QSet<QString> names;
        for (int i = 0; i < 12; ++i) {
            pins().pin(addShot(QRect(10 * i, 20, 100, 60)));
            const QString rule = evalCall(i);
            QRegularExpressionMatch m = QRegularExpression("name=\"(tarn-pin-slot-\\d)\"").match(rule);
            QVERIFY2(m.hasMatch(), qPrintable(rule));
            names << m.captured(1);
            // The rule that last held the slot is retired first.
            QVERIFY2(rule.contains("set_enabled(false)"), qPrintable(rule));
        }
        QCOMPARE(names.size(), 4);
        QCOMPARE(evalCount(), 12);
    }

    void thePositionIsLocalToTheMonitorTheCaptureWasOn() {
        env->setFixture("hyprctl", "monitors.json",
                        "[{\"name\":\"DP-1\",\"x\":0,\"y\":0,\"width\":2560,\"height\":1440,\"scale\":1.0,\"transform\":0},"
                        "{\"name\":\"DP-2\",\"x\":2560,\"y\":0,\"width\":1920,\"height\":1080,\"scale\":1.0,\"transform\":0}]");
        pins().pin(addShot(QRect(2560 + 40, 100, 100, 60)));
        QVERIFY2(evalCall(0).contains("move={\"40\", \"100\"}"), qPrintable(evalCall(0)));
    }

    void anEditedImageKeepsItsPixelDensity() {
        const int id = addShot(QRect(300, 200, 100, 60));
        stack().setEdited(id, pngOf(testShot(300, 120))); // the canvas grew
        const int pin = pins().pin(id);
        QCOMPARE(pins().openingRect(pin), QRect(300, 200, 150, 60));
        QCOMPARE(pins().png(pin), pngOf(testShot(300, 120)));
    }

    void aCaptureWithoutAPlaceOpensAtTheCenterOfTheMonitorUnderThePointer() {
        env->setPointer(100, 100);
        const int id = stack().add(pngOf(testShot(200, 100)), when); // no setOrigin
        const int pin = pins().pin(id);
        QCOMPARE(pins().openingRect(pin), QRect(1280 - 100, 720 - 50, 200, 100));
    }

    // A History entry has no place on the desktop. On a scaled monitor its pixels are not logical
    // pixels: a 1920 x 1080 shot would open at twice the size of the 960 x 540 monitor it came from.
    void aCaptureWithoutAPlaceOpensAtItsPixelsOverTheMonitorScale() {
        env->setFixture("hyprctl", "monitors.json",
                        R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":1920,"height":1080,"scale":2.0,"transform":0}])");
        env->setPointer(100, 100);
        const int id = stack().add(pngOf(testShot(200, 100)), when); // no setOrigin
        const int pin = pins().pin(id);
        QCOMPARE(pins().openingRect(pin), QRect(480 - 50, 270 - 25, 100, 50));
    }

    void aRecordingHasNoPin() {
        const int id = stack().addRecording(pngOf(testShot()), when, env->scratch() + "/x.mp4");
        QVERIFY(!stack().pinnable(id));
        stack().requestPin(id);
        QCOMPARE(pins().pin(id), -1);
        QCOMPARE(stack().totalCount(), 1); // still there
        QVERIFY(pins().ids().isEmpty());
    }

    void aPinnedThumbnailIsTheOneRestoreBringsBack() {
        const int pin = pins().pin(addShot());
        QVERIFY(pin > 0);
        QVERIFY(stack().canRestore());
    }

    void tarnLuaFloatsAndPinsAnythingTitledLikeAPin() {
        QFile file(TARN_HYPR_LUA);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString lua = QString::fromUtf8(file.readAll());
        const int at = lua.indexOf("name = \"tarn-pin\"");
        QVERIFY(at > 0);
        const QString rule = lua.mid(at, lua.indexOf("})", at) - at);
        QVERIFY(rule.contains("float = true"));
        QVERIFY(rule.contains("pin = true"));
        QVERIFY(rule.contains("no_anim = true"));
        QVERIFY(rule.contains("keep_aspect_ratio = true"));
        const auto m = QRegularExpression("match = \\{ title = \"([^\"]+)\"").match(rule);
        QVERIFY(m.hasMatch());
        const QRegularExpression title(m.captured(1));
        QVERIFY(title.match(Pins::title(1)).hasMatch());
        QVERIFY(title.match(Pins::title(137)).hasMatch());
        QVERIFY(!title.match("Tarn editor").hasMatch());
        QVERIFY(!title.match("Tarn pin").hasMatch());
    }

    // ---- from the Editor ----

    void pinFromTheEditorFloatsTheEditedImage() {
        const int id = addShot(QRect(300, 200, 100, 60));
        auto *session = editor().open(id);
        QVERIFY(session);
        session->pointerPressed(20, 20);
        session->pointerMoved(100, 70);
        session->pointerReleased(100, 70);
        QCOMPARE(session->annotationCount(), 1);
        const QByteArray exported = session->document().toPng();
        QVERIFY(exported != pngOf(testShot())); // something was drawn

        QVERIFY(session->pin()); // Ctrl+P
        QCOMPARE(pins().ids().size(), 1);
        const int pin = pins().ids().first();
        QCOMPARE(pins().png(pin), exported);
        QCOMPARE(pins().openingRect(pin), QRect(300, 200, 100, 60));
        QCOMPARE(stack().totalCount(), 0);   // the Thumbnail went with it
        QVERIFY(!editor().session(id));      // and the Editor closed
    }

    void pinFromTheEditorWithNothingDrawnFloatsTheCapture() {
        const int id = addShot();
        auto *session = editor().open(id);
        QVERIFY(session->pin());
        QCOMPARE(pins().png(pins().ids().first()), pngOf(testShot()));
    }

    // ---- adjusting ----

    void scrollResizesAboutTheCenterKeepingTheAspect() {
        const int pin = pins().pin(addShot(QRect(300, 200, 100, 60)));
        const int before = evalCount();
        pins().resize(pin, 120); // one notch up: a tenth bigger
        QTRY_COMPARE(evalCount(), before + 1);
        const QString code = evalCall(before);
        QVERIFY2(code.contains("hl.dsp.window.resize({window=\"title:^Tarn pin 1$\", x=110, y=66, relative=false})"), qPrintable(code));
        QVERIFY2(code.contains("hl.dsp.window.move({window=\"title:^Tarn pin 1$\", x=295, y=197, relative=false})"), qPrintable(code));
    }

    void resizeStartsFromWhereTheUserDraggedIt() {
        const int pin = pins().pin(addShot(QRect(300, 200, 100, 60)));
        env->setFixture("hyprctl", "clients.json", "[{\"title\":\"Tarn pin 1\",\"at\":[500,400],\"size\":[100,60]}]");
        const int before = evalCount();
        pins().resize(pin, -120); // a notch down
        QTRY_COMPARE(evalCount(), before + 1);
        const QString code = evalCall(before);
        QVERIFY2(code.contains("x=91, y=55,"), qPrintable(code)); // 100 / 1.1 = 90.9, 54.5
        QVERIFY2(code.contains("x=505, y=403,"), qPrintable(code)); // about the center (550, 430)
    }

    void resizeStopsAtTenPercentAndEightTimes() {
        const int pin = pins().pin(addShot(QRect(300, 200, 100, 60)));
        const int start = evalCount();
        for (int i = 0; i < 80; ++i)
            pins().resize(pin, -120);
        QTRY_COMPARE(evalCount(), start + 1);
        QString last = evalCall(evalCount() - 1);
        QVERIFY2(last.contains("x=10, y=6,"), qPrintable(last));
        pins().resize(pin, -120); // already at the smallest: nothing more is asked of Hyprland
        QTest::qWait(150);
        QCOMPARE(evalCount(), start + 1);
        for (int i = 0; i < 80; ++i)
            pins().resize(pin, 120);
        QTRY_COMPARE(evalCount(), start + 2);
        last = evalCall(evalCount() - 1);
        QVERIFY2(last.contains("x=800, y=480,"), qPrintable(last));
    }

    // Applies one notch against a Hyprland that obeys the last resize; returns the size asked for.
    QSize notch(int pin, int &evals, const QSize &current, qreal delta) {
        env->setFixture("hyprctl", "clients.json",
                        QString("[{\"title\":\"Tarn pin %1\",\"at\":[100,100],\"size\":[%2,%3]}]").arg(pin).arg(current.width()).arg(current.height()).toUtf8());
        pins().resize(pin, delta);
        QTest::qWait(60);
        if (evalCount() == evals)
            return current;
        evals = evalCount();
        const QString code = evalCall(evals - 1);
        const auto m = QRegularExpression("resize\\(\\{window=[^,]+, x=(\\d+), y=(\\d+),").match(code);
        return m.hasMatch() ? QSize(m.captured(1).toInt(), m.captured(2).toInt()) : current;
    }

    void scrollResizesKeepThePicturesShapeWithinOnePixelOverManyNotches() {
        // 301 x 173: a shape that does not round out evenly.
        const int shot = stack().add(pngOf(testShot(301, 173)), when);
        stack().setOrigin(shot, QRect(0, 0, 301, 173));
        const int pin = pins().pin(shot);
        QSize current(301, 173);
        int evals = evalCount();
        const qreal ratio = 301.0 / 173.0;
        for (int i = 0; i < 60; ++i) {
            current = notch(pin, evals, current, i % 7 < 4 ? 120 : -120);
            QVERIFY2(qAbs(current.width() / ratio - current.height()) <= 1.0,
                     qPrintable(QString("%1 x %2 at notch %3").arg(current.width()).arg(current.height()).arg(i)));
        }
    }

    void aMouseResizeThatArrivesOffShapeIsSnappedBack() {
        const int pin = pins().pin(addShot(QRect(300, 200, 100, 60)));
        env->setFixture("hyprctl", "clients.json", "[{\"title\":\"Tarn pin 1\",\"at\":[300,200],\"size\":[150,60]}]");
        const int before = evalCount();
        pins().windowResized(pin);
        pins().windowResized(pin); // a drag sends many events: one fix
        QTRY_COMPARE(evalCount(), before + 1);
        QTest::qWait(100);
        QCOMPARE(evalCount(), before + 1);
        const QString code = evalCall(before);
        QVERIFY2(code.contains("x=150, y=90,"), qPrintable(code)); // the width is kept, the height follows
        QVERIFY2(code.contains("x=300, y=200,"), qPrintable(code));
    }

    void aResizeOnShapeIsLeftAlone() {
        const int pin = pins().pin(addShot(QRect(300, 200, 100, 60)));
        env->setFixture("hyprctl", "clients.json", "[{\"title\":\"Tarn pin 1\",\"at\":[300,200],\"size\":[151,91]}]");
        const int before = evalCount();
        pins().windowResized(pin);
        QTest::qWait(100);
        QCOMPARE(evalCount(), before);
    }

    void aRunOfScrollNotchesIsOneHyprctlResizeAndOneLookup() {
        const int pin = pins().pin(addShot(QRect(300, 200, 100, 60)));
        const int evals = evalCount();
        int clients = 0;
        for (const auto &call : env->calls("hyprctl"))
            clients += call.value(0) == "clients";
        for (int i = 0; i < 5; ++i)
            pins().resize(pin, 120);
        QTRY_COMPARE(evalCount(), evals + 1);
        QTest::qWait(150);
        QCOMPARE(evalCount(), evals + 1);
        int after = 0;
        for (const auto &call : env->calls("hyprctl"))
            after += call.value(0) == "clients";
        QCOMPARE(after, clients + 1);
        // 100 * 1.1^5 = 161 wide, 97 high.
        QVERIFY2(evalCall(evals).contains("x=161, y=97,"), qPrintable(evalCall(evals)));
        // A second run later is again its own call.
        pins().resize(pin, 120);
        QTRY_COMPARE(evalCount(), evals + 2);
    }

    void ctrlScrollChangesTheOpacityInStepsWithinTenPercentAndOpaque() {
        const int pin = pins().pin(addShot());
        QSignalSpy changed(&pins(), &Pins::opacityChanged);
        pins().changeOpacity(pin, -120);
        QCOMPARE(changed.size(), 1);
        QVERIFY(qFuzzyCompare(pins().opacity(pin), 0.95));
        for (int i = 0; i < 40; ++i)
            pins().changeOpacity(pin, -120);
        QVERIFY(qFuzzyCompare(pins().opacity(pin), Pins::kMinOpacity));
        for (int i = 0; i < 40; ++i)
            pins().changeOpacity(pin, 120);
        QCOMPARE(pins().opacity(pin), 1.0);
        QCOMPARE(evalCount(), 1); // opacity is the window's own: Hyprland is only asked to place it
    }

    // ---- the right-click menu and Ctrl+C ----

    void copyGoesThroughTheSameClipboardPathAsAThumbnailsCopy() {
        const int thumb = addShot();
        const int pin = pins().pin(addShot());
        // The Thumbnail's Copy (the reference path), then the Pin's.
        QVERIFY(stack().copy(thumb));
        QCOMPARE(env->calls("tarn-clip").size(), 1);
        const QString viaThumbnail = env->calls("tarn-clip").first().first();

        QVERIFY(pins().copy(pin));
        QCOMPARE(env->calls("tarn-clip").size(), 2);
        const QString viaPin = env->calls("tarn-clip").last().first();
        QCOMPARE(QFileInfo(viaPin).absolutePath(), shared());
        QCOMPARE(QFileInfo(viaThumbnail).absolutePath(), shared());
        QCOMPARE(TestEnv::readFile(viaPin), TestEnv::readFile(viaThumbnail)); // the same bytes
        QCOMPARE(TestEnv::readFile(viaPin), pngOf(testShot()));
        QVERIFY(env->calls("wl-copy").isEmpty());
        QVERIFY(pins().has(pin)); // a Pin stays after Copy
        QVERIFY(env->calls("notify-send").isEmpty());
    }

    void copyFromAPinOfAnEditedImageCopiesTheEditedImage() {
        const int id = addShot();
        auto *session = editor().open(id);
        session->pointerPressed(20, 20);
        session->pointerMoved(100, 70);
        session->pointerReleased(100, 70);
        const QByteArray exported = session->document().toPng();
        session->pin();
        QVERIFY(pins().copy(pins().ids().first()));
        QCOMPARE(TestEnv::readFile(env->calls("tarn-clip").first().first()), exported);
    }

    void aFailedCopyNotifiesAndKeepsThePin() {
        const int pin = pins().pin(addShot());
        env->setMode("tarn-clip", "fail");
        QVERIFY(!pins().copy(pin));
        QCOMPARE(env->calls("notify-send").size(), 1);
        QVERIFY(pins().has(pin));
    }

    void saveAsWritesTheFormatTheExtensionNamesAndTheFolderIsRemembered() {
        const int pin = pins().pin(addShot());
        const QString dir = env->scratch() + "/out";
        QVERIFY(pins().saveAs(pin, dir + "/mine.png"));
        QCOMPARE(TestEnv::readFile(dir + "/mine.png"), pngOf(testShot()));
        QVERIFY(pins().saveAs(pin, dir + "/mine.jpg"));
        QVERIFY(QImage(dir + "/mine.jpg").width() == 200);
        QCOMPARE(pins().saveAsFolder(), dir);
        QVERIFY(pins().has(pin)); // a Pin stays after Save as…
    }

    void saveAsWithABadExtensionNotifiesAndWritesNothing() {
        const int pin = pins().pin(addShot());
        QVERIFY(!pins().saveAs(pin, env->scratch() + "/out/mine.bmp"));
        QCOMPARE(env->calls("notify-send").size(), 1);
        QVERIFY(!QFile::exists(env->scratch() + "/out/mine.bmp"));
    }

    void editClosesThePinAndOpensTheEditorOnItsImage() {
        const int pin = pins().pin(addShot());
        QSignalSpy editorOpened(&editor(), &Editor::opened);
        pins().edit(pin);
        QVERIFY(!pins().has(pin));
        QCOMPARE(editorOpened.size(), 1);
        auto *session = editorOpened.first().first().value<tarn::EditorSession *>();
        QVERIFY(session);
        QCOMPARE(session->document().shot(), testShot());
        QCOMPARE(stack().count(), 0);       // hidden while editing
        QCOMPARE(stack().totalCount(), 1);
        QVERIFY(env->calls("tarn-clip").isEmpty()); // adopting is no automatic Copy / Save
    }

    void editOfAnEditedPinOpensOnTheEditedImage() {
        const int id = addShot();
        auto *first = editor().open(id);
        first->pointerPressed(20, 20);
        first->pointerMoved(100, 70);
        first->pointerReleased(100, 70);
        const QImage drawn = first->document().flatten();
        first->pin();
        QSignalSpy editorOpened(&editor(), &Editor::opened);
        pins().edit(pins().ids().first());
        auto *session = editorOpened.first().first().value<tarn::EditorSession *>();
        QCOMPARE(session->document().shot(), drawn);
    }

    void closeAndCloseAllPins() {
        const int a = pins().pin(addShot()), b = pins().pin(addShot()), c = pins().pin(addShot());
        QSignalSpy closed(&pins(), &Pins::closed);
        pins().close(b);
        QCOMPARE(pins().ids(), (QList<int>{a, c}));
        QCOMPARE(closed.size(), 1);
        pins().closeAll();
        QVERIFY(pins().ids().isEmpty());
        QCOMPARE(closed.size(), 3);
    }

    // ---- Locked ----

    // Model the user's windows: Pin n is at address 0xa<n>.
    void windowsFor(const QList<int> &ids) {
        QStringList clients;
        for (int id : ids)
            clients << QString(R"({"address":"0xa%1","title":"Tarn pin %1","at":[0,0],"size":[100,60]})").arg(id);
        env->setFixture("hyprctl", "clients.json", ("[" + clients.join(",") + "]").toUtf8());
    }
    // The `hyprctl eval` Lua that sets a window property. Hyprland 0.56+ reads
    // `hyprctl dispatch` arguments as Lua, so the old `dispatch setprop ...` form is
    // refused; the user checked this form on Hyprland 0.57.1 (2026-11-04).
    QStringList setprops() const {
        QStringList out;
        for (const auto &call : env->calls("hyprctl"))
            if (call.size() == 2 && call.first() == "eval" && call.last().contains("set_prop"))
                out << call.last();
        return out;
    }
    static QString noFocus(int pin, int value) {
        return QString(R"(hl.dispatch(hl.dsp.window.set_prop({window="address:0xa%1", prop="no_focus", value="%2"})))")
            .arg(pin)
            .arg(value);
    }
    // Moves and resizes only: what a lock must never send.
    int moveEvalCount() const { return evalCount() - setprops().size(); }

    void lockingAsksHyprlandForNoFocusOnThePinsWindowAndUnlockingClearsIt() {
        const int a = pins().pin(addShot());
        windowsFor({a});
        QSignalSpy changed(&pins(), &Pins::lockedChanged);
        QVERIFY(pins().lock(a));
        QVERIFY(pins().isLocked(a));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(setprops(), QStringList{noFocus(a, 1)});
        QVERIFY(pins().unlock(a));
        QVERIFY(!pins().isLocked(a));
        QCOMPARE(setprops().last(), noFocus(a, 0));
    }

    void lockingKeepsPositionSizeAndOpacity() {
        const int a = pins().pin(addShot());
        windowsFor({a});
        pins().changeOpacity(a, 120);
        const qreal opacity = pins().opacity(a);
        const QRect opening = pins().openingRect(a);
        const int evals = moveEvalCount();
        pins().lock(a);
        QCOMPARE(pins().opacity(a), opacity);
        QCOMPARE(pins().openingRect(a), opening);
        QCOMPARE(moveEvalCount(), evals); // no move or resize was sent
    }

    void lockFailsAndStaysUnlockedWhenHyprlandHasNoSuchWindow() {
        const int a = pins().pin(addShot()); // clients.json is empty
        QVERIFY(!pins().lock(a));
        QVERIFY(!pins().isLocked(a));
        QVERIFY(setprops().isEmpty());
    }

    void lockToggleLocksAllWhenAnyIsUnlockedAndUnlocksAllWhenAllAreLocked() {
        const int a = pins().pin(addShot()), b = pins().pin(addShot());
        windowsFor({a, b});
        pins().lock(a);
        QVERIFY(env->request({"pin", "lock-toggle"}).ok);
        QVERIFY(pins().isLocked(a) && pins().isLocked(b)); // one was unlocked: lock all
        QVERIFY(env->request({"pin", "lock-toggle"}).ok);
        QVERIFY(!pins().isLocked(a) && !pins().isLocked(b)); // all locked: unlock all
        QVERIFY(env->request({"pin", "lock-toggle"}).ok);
        QVERIFY(pins().isLocked(a) && pins().isLocked(b));
        QVERIFY(setprops().contains(noFocus(b, 0)));
    }

    void lockToggleWithNoPinsIsHarmless() {
        QVERIFY(env->request({"pin", "lock-toggle"}).ok);
        QVERIFY(setprops().isEmpty());
    }

    void closeAllPinsClosesLockedOnesToo() {
        const int a = pins().pin(addShot()), b = pins().pin(addShot());
        windowsFor({a, b});
        pins().lock(a);
        pins().closeAll();
        QVERIFY(pins().ids().isEmpty());
        QVERIFY(!pins().isLocked(a));
    }

    void tarnLuaBindsSuperShiftPToLockToggle() {
        QFile file(TARN_HYPR_LUA);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(file.readAll()).contains(R"(hl.bind("SUPER + SHIFT + P", hl.dsp.exec_cmd("tarn pin lock-toggle"))"));
    }

    void tarnLuaTurnsHyprlandsBlurOffBehindPins() {
        // A Pin at lowered opacity is see-through, and Hyprland blurs whatever shows
        // through a see-through window, so what is behind the Pin couldn't be read.
        QFile file(TARN_HYPR_LUA);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString lua = QString::fromUtf8(file.readAll());
        const qsizetype rule = lua.indexOf(R"(name = "tarn-pin",)");
        QVERIFY(rule >= 0);
        QVERIFY(lua.mid(rule, lua.indexOf("})", rule) - rule).contains("no_blur = true"));
    }

    // ---- GIF Pins ----

    QByteArray gifBytes() const { return TestEnv::readFile(QStringLiteral(TARN_STUB_DIR "/fixtures/gifski/sample.gif")); }
    // A GIF Recording's Thumbnail: the GIF in the kept folder, the MP4 beside it, recorded over `origin`.
    int addGifRecording(const QRect &origin = QRect(300, 200, 100, 60)) {
        QDir().mkpath(env->keptRecordings());
        const QString gif = env->keptRecordings() + "/2026-11-03_09-30-15.gif";
        const QString mp4 = env->keptRecordings() + "/2026-11-03_09-30-15.mp4";
        QFile f(gif);
        f.open(QIODevice::WriteOnly);
        f.write(gifBytes());
        f.close();
        QFile m(mp4);
        m.open(QIODevice::WriteOnly);
        m.write("mp4");
        m.close();
        const int id = stack().addRecording(pngOf(testShot()), when, gif, 3000, mp4);
        stack().setOrigin(id, origin);
        return id;
    }

    void pinIsOfferedOnAGifRecordingButNotAnMp4OneAndTheThumbnailStays() {
        const int gif = addGifRecording();
        QVERIFY(stack().pinnable(gif));
        QSignalSpy opened(&pins(), &Pins::opened);
        stack().requestPin(gif);
        QCOMPARE(opened.size(), 1);
        const int pin = opened.first().first().toInt();
        QVERIFY(pins().isGif(pin));
        QCOMPARE(stack().count(), 1); // the Thumbnail stays
        QVERIFY(stack().ids().contains(gif));

        const int mp4 = stack().addRecording(pngOf(testShot()), when, env->scratch() + "/x.mp4");
        QVERIFY(!stack().pinnable(mp4));
    }

    void aGifPinOpensWhereTheGifWasRecordedAtFullSize() {
        const int pin = pins().pin(addGifRecording(QRect(300, 200, 100, 60)));
        QCOMPARE(pins().openingRect(pin), QRect(300, 200, 100, 60));
        const QString rule = evalCall(0);
        QVERIFY2(rule.contains("move={\"300\", \"200\"}"), qPrintable(rule));
        QVERIFY2(rule.contains("size={\"100\", \"60\"}"), qPrintable(rule));
    }

    void aGifPinPlaysItsOwnCopyOfTheFile() {
        const int pin = pins().pin(addGifRecording());
        const QString path = pins().gifPath(pin);
        QVERIFY(path.endsWith(".gif"));
        QVERIFY(!path.startsWith(env->keptRecordings()));
        QCOMPARE(TestEnv::readFile(path), gifBytes());
    }

    void theGifPinsCopySurvivesDeletingTheRecordingAndGoesOnClose() {
        const int rec = addGifRecording();
        const int pin = pins().pin(rec);
        const QString path = pins().gifPath(pin);
        // Delete (or Trim) takes the Recording's file away.
        QFile::remove(stack().videoPath(rec));
        stack().closeAll();
        QVERIFY(QFileInfo::exists(path));
        QCOMPARE(TestEnv::readFile(path), gifBytes());
        pins().close(pin);
        QVERIFY(!QFileInfo::exists(path));
    }

    void closeAllPinsRemovesEveryGifCopy() {
        const int a = pins().pin(addGifRecording());
        const QString path = pins().gifPath(a);
        pins().closeAll();
        QVERIFY(!QFileInfo::exists(path));
    }

    void aGifPinChangesOpacityAndResizesAndLocksLikeAnImagePin() {
        const int pin = pins().pin(addGifRecording());
        QCOMPARE(evalCount(), 1);
        pins().changeOpacity(pin, -120);
        QVERIFY(qFuzzyCompare(pins().opacity(pin), 0.95));
        windowsFor({pin});
        pins().resize(pin, 120);
        QTRY_COMPARE(evalCount(), 2);
        QVERIFY(pins().lock(pin));
        QVERIFY(pins().isLocked(pin));
        QVERIFY(pins().lockToggle());
        QVERIFY(!pins().isLocked(pin));
    }

    void aGifPinCopiesTheGifFileToTheClipboardAndStays() {
        const int pin = pins().pin(addGifRecording());
        QVERIFY(pins().copy(pin));
        QCOMPARE(env->calls("tarn-clip").size(), 1);
        const QString file = env->calls("tarn-clip").first().last(); // ("--file-only", path)
        QCOMPARE(QFileInfo(file).absolutePath(), shared());
        QVERIFY(file.endsWith(".gif"));
        QCOMPARE(TestEnv::readFile(file), gifBytes());
        QVERIFY(pins().has(pin));
        // What the clipboard holds outlives the Pin.
        pins().close(pin);
        QCOMPARE(TestEnv::readFile(file), gifBytes());
    }

    void aGifPinSavesAsAGifAndRemembersTheFolder() {
        const int pin = pins().pin(addGifRecording());
        const QString dir = env->scratch() + "/out";
        QVERIFY(pins().saveAs(pin, dir + "/mine.gif"));
        QCOMPARE(TestEnv::readFile(dir + "/mine.gif"), gifBytes());
        QCOMPARE(pins().saveAsFolder(), dir);
        QVERIFY(pins().has(pin));
        QVERIFY(!pins().saveAs(pin, dir + "/mine.png")); // a GIF is only saved as a GIF
        QVERIFY(!QFile::exists(dir + "/mine.png"));
    }

    void aGifPinHasNoEdit() {
        const int pin = pins().pin(addGifRecording());
        QSignalSpy editorOpened(&editor(), &Editor::opened);
        pins().edit(pin);
        QVERIFY(pins().has(pin));
        QCOMPARE(editorOpened.size(), 0);
    }

    void aGifRecordingWhoseFileIsGoneCannotBePinned() {
        const int rec = addGifRecording();
        QFile::remove(stack().videoPath(rec));
        QCOMPARE(pins().pin(rec), -1);
        QVERIFY(pins().ids().isEmpty());
    }

    void aFailedPlacementNotifiesButTheStillPinOpens() {
        env->setMode("hyprctl", "fail");
        QVERIFY(pins().pin(addShot()) > 0);
        QCOMPARE(env->calls("notify-send").size(), 1);
        QCOMPARE(pins().ids().size(), 1);
    }
};

QTEST_MAIN(PinTest)
#include "pin_test.moc"
