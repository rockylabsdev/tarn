// The Project file. A capture's edits are saved beside its untouched
// History original (<id>.edit.json) on every Editor close that keeps edits, and
// by the auto-applied Preset at capture time; Esc never writes. The round trip
// (edit, close, load into a new document, flatten) must give the picture Copy
// gave before closing.
//
// Everything is asserted on what is visible from outside: the files in History
// and the Trash, the PNG Copy wrote, and the notifications raised.
#include "test_env.h"

#include "document/capture_document.h"
#include "document/project_file.h"
#include "editor/editor.h"
#include "history/history.h"
#include "preset/preset_store.h"
#include "thumbnails/thumbnail_stack.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using tarn::CaptureDocument;
using tarn::DragGesture;
using tarn::Editor;
using tarn::EditorSession;
using tarn::Frame;
using tarn::History;
using tarn::ThumbnailStack;
using tarn::ToolStyle;
using tarn::test::TestEnv;

namespace {

DragGesture drag(const QPointF &from, const QPointF &to, const ToolStyle &style = {}) {
    DragGesture g;
    g.from = from;
    g.to = to;
    g.style = style;
    g.path = {from, (from + to) / 2, to};
    return g;
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    f.write(bytes);
}

} // namespace

class ProjectTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    History &history() { return env->daemon().service<History>(); }
    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    Editor &editor() { return env->daemon().service<Editor>(); }
    QString historyDir() const { return env->scratch() + "/data/tarn/history"; }
    QString trashFiles() const { return env->scratch() + "/data/Trash/files"; }
    QString shared() const { return env->scratch() + "/cache/tarn/shared"; }
    int notifications() const { return env->calls("notify-send").size(); }

    // `capture fullscreen` one second after the last one; returns the Thumbnail's id.
    int capture() {
        env->setNow(env->daemon().now().addSecs(1));
        const QList<int> before = stack().ids();
        if (!env->request({"capture", "fullscreen"}).ok)
            return -1;
        for (int id : stack().ids())
            if (!before.contains(id)) {
                m_historyIds[id] = stack().historyId(id);
                return id;
            }
        return -1;
    }
    QHash<int, QString> m_historyIds; // a closed Thumbnail forgets its History id
    QString historyIdOf(int thumb) { return m_historyIds.value(thumb); }
    QString projectPath(int thumb) { return history().projectPath(historyIdOf(thumb)); }
    bool projectExists(int thumb) {
        history().flush();
        return QFile::exists(projectPath(thumb));
    }
    QImage original(int thumb) { return QImage(history().pngPath(historyIdOf(thumb))); }
    static void draw(EditorSession &s) { s.document().draw("rect", drag({16, 12}, {80, 52})); }
    QImage sharedImage() {
        const QStringList files = TestEnv::filesIn(shared());
        return files.isEmpty() ? QImage() : QImage(shared() + "/" + files.last());
    }

private slots:
    void init() { env = std::make_unique<TestEnv>(); }
    void cleanup() { env.reset(); }

    // ---- written on every close that keeps edits; Esc never writes ----

    void doneWritesTheProjectFileAndLeavesTheOriginalAlone() {
        const int id = capture();
        history().flush();
        const QByteArray pngBefore = TestEnv::readFile(history().pngPath(historyIdOf(id)));
        QVERIFY(!projectExists(id)); // nothing edited yet
        EditorSession *s = editor().open(id);
        draw(*s);
        s->done();
        QVERIFY(projectExists(id));
        QVERIFY(projectPath(id).endsWith(".edit.json"));
        QCOMPARE(TestEnv::readFile(history().pngPath(historyIdOf(id))), pngBefore);
        QCOMPARE(history().ids().size(), 1); // the Project file is part of the entry, not an entry
    }

    void everyKeepingCloseWritesIt() {
        for (const QString &way : {"copy", "save", "saveAs", "pin"}) {
            const int id = capture();
            EditorSession *s = editor().open(id);
            draw(*s);
            if (way == "copy")
                QVERIFY(s->copyAndClose());
            else if (way == "save")
                QVERIFY(s->saveAndClose());
            else if (way == "saveAs")
                QVERIFY(s->saveAs(env->scratch() + "/elsewhere.png"));
            else
                QVERIFY(s->pin());
            QVERIFY2(projectExists(id), qPrintable(way));
        }
    }

    void aFailedCopyWritesNothing() {
        const int id = capture();
        EditorSession *s = editor().open(id);
        draw(*s);
        env->setMode("tarn-clip", "fail");
        QVERIFY(!s->copyAndClose());
        QVERIFY(!projectExists(id));
    }

    void escNeverWrites() {
        const int id = capture();
        EditorSession *s = editor().open(id);
        draw(*s);
        s->cancel();
        QVERIFY(!projectExists(id));
        // ...and an existing one is left as it was.
        s = editor().open(id);
        draw(*s);
        s->done();
        QVERIFY(projectExists(id));
        const QByteArray before = TestEnv::readFile(projectPath(id));
        s = editor().open(id);
        s->document().draw("ellipse", drag({5, 5}, {30, 30}));
        s->cancel();
        QVERIFY(projectExists(id));
        QCOMPARE(TestEnv::readFile(projectPath(id)), before);
    }

    void finishingWithNothingOnThePictureDeletesTheFile() {
        const int id = capture();
        EditorSession *s = editor().open(id);
        draw(*s);
        s->done();
        QVERIFY(projectExists(id));
        s = editor().open(id);
        s->selectAll();
        s->deleteSelection();
        QCOMPARE(s->annotationCount(), 0);
        s->done();
        QVERIFY(!projectExists(id));
        QVERIFY(TestEnv::filesIn(trashFiles()).isEmpty()); // deleted, not trashed
    }

    // ---- the auto-applied Preset ----

    void theAutoAppliedPresetIsWrittenAtCaptureTime() {
        Frame f;
        f.background.kind = "gradient";
        f.background.params = {{"stops", QVariantList{"#ffff0000", "#ff0000ff"}}, {"angle", 90}};
        f.padding = 0.1;
        auto &presets = env->daemon().service<tarn::PresetStore>();
        QVERIFY(presets.save("Look", f));
        QVERIFY(presets.setAuto("Look"));
        const int id = capture();
        QVERIFY(projectExists(id));
        // History hands out what the Thumbnail shows: loading the file rebuilds the framed shot.
        auto loaded = history().loadProject(historyIdOf(id), original(id));
        QVERIFY(loaded.document);
        QCOMPARE(loaded.document->flatten(), QImage::fromData(stack().edited(id), "PNG").convertToFormat(loaded.document->flatten().format()));
        QCOMPARE(notifications(), 0);
    }

    // ---- the round trip ----

    // Every kind of edit at once. Pixelate is "black out" so exports are repeatable.
    void editCloseLoadCopyGivesTheSamePicture() {
        QImage extra(12, 10, QImage::Format_ARGB32);
        extra.fill(QColor(0, 200, 0, 180));
        const QString extraPath = env->scratch() + "/extra.png";
        QVERIFY(extra.save(extraPath));

        const int id = capture();
        EditorSession *s = editor().open(id);
        auto &d = s->document();
        d.draw("rect", drag({10, 8}, {60, 40}));
        d.draw("filled-rect", drag({20, 20}, {40, 30}));
        d.draw("ellipse", drag({30, 10}, {70, 50}));
        d.draw("line", drag({2, 70}, {90, 60}));
        d.draw("arrow", drag({5, 5}, {50, 25}));
        d.draw("pencil", drag({40, 40}, {70, 70}));
        d.draw("highlighter", drag({10, 60}, {80, 62}));
        d.draw("counter", drag({70, 20}, {70, 20}));
        d.draw("counter", drag({70, 40}, {70, 40}));
        d.draw("counter", drag({70, 60}, {70, 60}));
        d.draw("spotlight", drag({0, 0}, {60, 40}));
        ToolStyle black;
        black.choices.insert("pixelate_style", "black");
        black.choices.insert("pixelate_strength", "4");
        d.draw("pixelate", drag({90, 30}, {110, 50}, black));
        QVERIFY(s->addImageFile(extraPath, 100, 70));
        const int counted = s->annotationCount();
        QCOMPARE(counted, 13);
        // One Counter is deleted, so the order matters: the rest renumber.
        d.remove({d.items()[8].id});
        s->setFrameBackground("gradient", {{"stops", QVariantList{"#ffaa0000", "#ff0000bb"}}, {"slots", QVariantList{"accent", "canvas"}}, {"angle", 45}});
        s->setFrameValue("padding", 0.1);
        s->enterCrop();
        s->rotateCrop(1);
        s->flipCropH();
        s->setOutputScale(1.5);
        s->leaveCrop();
        const QVariantMap frameBefore = s->document().frame().toMap();
        const QList<int> orderBefore = d.order();
        const tarn::Crop cropBefore = d.crop();
        QVERIFY(s->copyAndClose());

        const QImage copied = sharedImage();
        QVERIFY(!copied.isNull());
        auto loaded = history().loadProject(historyIdOf(id), original(id));
        QCOMPARE(int(loaded.state), int(History::ProjectState::Loaded));
        CaptureDocument &doc = *loaded.document;
        QCOMPARE(doc.flatten().convertToFormat(QImage::Format_ARGB32), copied.convertToFormat(QImage::Format_ARGB32));
        QCOMPARE(doc.order(), orderBefore);
        QVERIFY(cropBefore != tarn::Crop()); // the test really cropped
        QVERIFY(doc.crop() == cropBefore);
        QCOMPARE(doc.frame().toMap()["padding"], frameBefore["padding"]);
        // Loading starts with empty undo.
        QVERIFY(!doc.history().canUndo());
        QVERIFY(!doc.history().canRedo());
        QCOMPARE(notifications(), 0);
    }

    // A bent arrow of every style is saved with its bend and loads back the same.
    void arrowBendsRoundTripInEveryStyle() {
        for (const QString &style : {"tapered", "open", "double"}) {
            const int id = capture();
            EditorSession *s = editor().open(id);
            ToolStyle st;
            st.arrowStyle = style;
            const int arrow = s->document().draw("arrow", drag({10, 40}, {80, 40}, st));
            s->setSelection({arrow});
            s->pointerPressed(45, 40, 0); // the Bend Handle, dragged up
            s->pointerMoved(45, 30, 0);
            s->pointerMoved(45, 20, 0);
            s->pointerReleased(45, 20, 0);
            QVERIFY(s->document().annotation(arrow)->bendHandle().has_value());
            QVERIFY(s->copyAndClose());
            const QImage copied = sharedImage();
            auto loaded = history().loadProject(historyIdOf(id), original(id));
            QCOMPARE(int(loaded.state), int(History::ProjectState::Loaded));
            const auto handle = loaded.document->items().front().annotation->bendHandle();
            QVERIFY(handle.has_value());
            QCOMPARE(handle->y(), 20.0);
            QCOMPARE(loaded.document->flatten().convertToFormat(QImage::Format_ARGB32), copied.convertToFormat(QImage::Format_ARGB32));
        }
    }

    void theFrameIsLockedToItsLook() {
        const int id = capture();
        EditorSession *s = editor().open(id);
        draw(*s);
        s->setFrameBackground("gradient", {{"stops", QVariantList{"#ffaa0000", "#ff0000bb"}}, {"slots", QVariantList{"accent", "canvas"}}, {"angle", 45}});
        s->done();
        QVERIFY(projectExists(id));
        const QJsonObject bg = QJsonDocument::fromJson(TestEnv::readFile(projectPath(id))).object()["frame"].toObject()["background"].toObject();
        QCOMPARE(bg["kind"].toString(), QString("gradient"));
        QCOMPARE(bg["stops"].toArray().size(), 2);
        QCOMPARE(bg["stops"].toArray()[0].toString(), QString("#ffaa0000")); // the exact colors
        QVERIFY(!bg.contains("slots"));                                      // no longer follows the Palette
    }

    void theCurrentWallpaperIsKeptAsItsActualFile() {
        const QString wall = env->scratch() + "/wall.png";
        QImage w(20, 20, QImage::Format_RGB32);
        w.fill(Qt::darkGreen);
        QVERIFY(w.save(wall));
        const int id = capture();
        EditorSession *s = editor().open(id);
        QVERIFY(s->setFrameWallpaper(wall, true));
        s->done();
        QVERIFY(projectExists(id));
        const QJsonObject bg = QJsonDocument::fromJson(TestEnv::readFile(projectPath(id))).object()["frame"].toObject()["background"].toObject();
        QCOMPARE(bg["path"].toString(), wall);
        QVERIFY(!bg.contains("current"));
    }

    void aDeletedWallpaperFallsBackToABlurredShotWithOneNotification() {
        const QString wall = env->scratch() + "/wall.png";
        QImage w(40, 40, QImage::Format_RGB32);
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 40; ++x)
                w.setPixel(x, y, qRgb(x * 6, 100, y * 6));
        QVERIFY(w.save(wall));
        const int id = capture();
        EditorSession *s = editor().open(id);
        draw(*s);
        QVERIFY(s->setFrameWallpaper(wall));
        s->setFrameValue("padding", 0.2);
        QVERIFY(s->copyAndClose());
        const QImage copied = sharedImage();

        // With the file there: the same picture, and no notification.
        auto present = history().loadProject(historyIdOf(id), original(id));
        QCOMPARE(present.document->flatten().convertToFormat(QImage::Format_ARGB32), copied.convertToFormat(QImage::Format_ARGB32));
        QCOMPARE(notifications(), 0);

        // The file is gone: a blurred copy of the shot, and one notification naming the file.
        QVERIFY(QFile::remove(wall));
        auto fallback = history().loadProject(historyIdOf(id), original(id));
        QCOMPARE(int(fallback.state), int(History::ProjectState::Loaded));
        QCOMPARE(notifications(), 1);
        QVERIFY(env->calls("notify-send").first().join(" ").contains("wall.png"));
        CaptureDocument &doc = *fallback.document;
        QCOMPARE(doc.frame().background.kind, QString("blur"));
        QCOMPARE(doc.size(), present.document->size()); // same layout, only the background differs
        QCOMPARE(doc.count(), 1);                       // the Annotation is still there
        CaptureDocument expected(original(id));
        expected.restoreFrom({}, 1, {}, doc.frame());
        QVERIFY(expected.flatten() != copied); // not the wallpaper any more
        QVERIFY(doc.flatten() != copied);
    }

    // ---- a broken Project file ----

    void aBrokenProjectFileShowsTheOriginalAndIsReplacedOnTheNextDone() {
        const int id = capture();
        history().flush();
        writeFile(projectPath(id), "{ this is not json");
        EditorSession *s = editor().open(id);
        QCOMPARE(s->annotationCount(), 0); // the original
        QCOMPARE(s->document().shot(), original(id));
        QCOMPARE(notifications(), 1);
        const QStringList call = env->calls("notify-send").first();
        QVERIFY(call.join(" ").contains(QFileInfo(projectPath(id)).fileName()));
        QCOMPARE(stack().image(id), original(id)); // handed out as the original

        draw(*s);
        s->done();
        QCOMPARE(notifications(), 1); // still just the one
        history().flush();
        // The damaged file is in the Trash; the new one is readable.
        QCOMPARE(TestEnv::filesIn(trashFiles()), QStringList{QFileInfo(projectPath(id)).fileName()});
        QCOMPARE(TestEnv::readFile(trashFiles() + "/" + QFileInfo(projectPath(id)).fileName()), QByteArray("{ this is not json"));
        auto loaded = history().loadProject(historyIdOf(id), original(id));
        QCOMPARE(int(loaded.state), int(History::ProjectState::Loaded));
        QCOMPARE(loaded.document->count(), 1);
    }

    void anUnknownKindOrDamagedAnnotationCountsAsUnreadable() {
        CaptureDocument doc{QImage(8, 8, QImage::Format_RGB32)};
        QVERIFY(!tarn::loadProject(doc, R"({"annotations":[{"id":1,"kind":"from-the-future","data":{}}]})").ok);
        QVERIFY(!tarn::loadProject(doc, R"({"annotations":[{"id":1,"kind":"rect","data":{}}]})").ok);
        QVERIFY(!tarn::loadProject(doc, R"({"nothing":1})").ok);
        QCOMPARE(doc.count(), 0); // untouched
        // Fields it does not know are ignored, and missing optional ones take their defaults.
        QVERIFY(tarn::loadProject(doc, R"({"annotations":[{"id":4,"kind":"rect","data":{"rect":{"x":1,"y":1,"w":5,"h":5},"later":true}}],"extra":1})").ok);
        QCOMPARE(doc.count(), 1);
        QCOMPARE(doc.items()[0].id, 4);
    }

    // ---- the entry's group ----

    void theTrashAndRestoreMoveTheProjectFileWithItsEntry() {
        const int id = capture();
        EditorSession *s = editor().open(id);
        draw(*s);
        s->done();
        const QString hid = historyIdOf(id);
        QVERIFY(projectExists(id));
        const QByteArray project = TestEnv::readFile(projectPath(id));
        QVERIFY(history().trash(hid));
        QVERIFY(TestEnv::filesIn(historyDir()).isEmpty());
        QVERIFY(QFile::exists(trashFiles() + "/" + hid + ".edit.json"));
        QVERIFY(history().restore(hid));
        QCOMPARE(TestEnv::readFile(projectPath(id)), project);
    }

    void theSweepTakesTheProjectFileWithItsEntry() {
        QDir().mkpath(historyDir());
        const QString old = historyDir() + "/old";
        for (const QString &suffix : {".png", ".edit.json"})
            writeFile(old + suffix, "x");
        writeFile(old + ".json", QJsonDocument(QJsonObject{{"captureTime", "2025-01-01T00:00:00"}, {"kind", "area"}}).toJson());
        writeFile(historyDir() + "/new.png", "x");
        writeFile(historyDir() + "/new.edit.json", "{}");
        writeFile(historyDir() + "/new.json",
                  QJsonDocument(QJsonObject{{"captureTime", "2026-11-02T00:00:00"}, {"kind", "area"}}).toJson());
        QCOMPARE(history().ids(), (QStringList{"old", "new"})); // a Project file is not an entry of its own
        QCOMPARE(history().sweep(), 1);
        QCOMPARE(TestEnv::filesIn(historyDir()), (QStringList{"new.edit.json", "new.json", "new.png"}));
    }

    void expiryCountsFromCaptureTimeNotFromTheLastEdit() {
        const int id = capture();
        EditorSession *s = editor().open(id);
        draw(*s);
        s->done(); // the Project file is written now, but the entry's age is its capture time
        stack().closeAll(); // (the sweep leaves entries on the stack alone)
        env->setNow(env->daemon().now().addDays(40));
        QCOMPARE(history().sweep(), 1);
        QVERIFY(TestEnv::filesIn(historyDir()).isEmpty());
    }

    void revertToOriginalSendsOnlyTheProjectFileToTheTrash() {
        const int id = capture();
        EditorSession *s = editor().open(id);
        draw(*s);
        s->done();
        QVERIFY(history().revertProject(historyIdOf(id)));
        QVERIFY(!projectExists(id));
        QVERIFY(QFile::exists(history().pngPath(historyIdOf(id))));
        QCOMPARE(TestEnv::filesIn(trashFiles()), QStringList{historyIdOf(id) + ".edit.json"});
    }
};

QTEST_MAIN(ProjectTest)
#include "project_test.moc"
