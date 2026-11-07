// Edit from History. A round trip through the list model (what the QML
// calls), the Editor service and the files: Edit reopens an entry on its Project
// file, closing writes it and returns focus to the History window, and no Thumbnail
// is ever hidden or added. Also Revert to original and the sweep's rule about
// entries in use. Asserted on files, the Trash, hyprctl's calls, the clipboard
// helper's file and the stack.
#include "test_env.h"

#include "document/capture_document.h"
#include "editor/editor.h"
#include "history/history.h"
#include "history_window/history_window.h"
#include "pin/pins.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using tarn::CaptureDocument;
using tarn::DragGesture;
using tarn::Editor;
using tarn::EditorSession;
using tarn::History;
using tarn::HistoryModel;
using tarn::HistoryWindow;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

class EditFromHistoryTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    History &history() { return env->daemon().service<History>(); }
    HistoryWindow &window() { return env->daemon().service<HistoryWindow>(); }
    HistoryModel &model() { return window().model(); }
    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    Editor &editor() { return env->daemon().service<Editor>(); }
    QString historyDir() const { return env->scratch() + "/data/tarn/history"; }
    QString trashFiles() const { return env->scratch() + "/data/Trash/files"; }
    int focusCalls() {
        int n = 0;
        for (const QStringList &call : env->calls("hyprctl")) {
            const QString text = call.join(' ');
            if (text.contains("focus") && text.contains("Tarn history"))
                ++n;
        }
        return n;
    }

    static QByteArray png() {
        QImage image(96, 64, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    }
    void plant(const QString &id, const QString &when) {
        QDir().mkpath(historyDir());
        QFile image(historyDir() + "/" + id + ".png");
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.write(png());
        QFile json(historyDir() + "/" + id + ".json");
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(QJsonDocument(QJsonObject{{"captureTime", when}, {"kind", "area"}}).toJson());
    }
    static DragGesture drag(const QPointF &from, const QPointF &to) {
        DragGesture g;
        g.from = from;
        g.to = to;
        g.path = {from, (from + to) / 2, to};
        return g;
    }
    // Gives an entry an Annotation, as an earlier Editor visit would have.
    void edit(const QString &id) {
        CaptureDocument document(QImage::fromData(TestEnv::readFile(historyDir() + "/" + id + ".png"), "PNG"));
        document.draw("rect", drag({16, 12}, {80, 52}));
        history().saveProject(id, document);
        history().flush();
    }
    bool hasProject(const QString &id) {
        history().flush();
        return QFile::exists(history().projectPath(id));
    }

private slots:
    void init() { env = std::make_unique<TestEnv>(); } // the clock reads 2026-11-03 09:30:15
    void cleanup() { env.reset(); }

    // ---- Edit reopens on the Project file ----

    void editReopensWithEveryAnnotationStillEditable() {
        plant("a", "2026-11-03T08:00:00");
        edit("a");
        window().open();
        model().select("a");
        QVERIFY(model().edit());
        EditorSession *s = editor().sessionForHistory("a");
        QVERIFY(s);
        QCOMPARE(s->annotationCount(), 1);
        QVERIFY(!s->canUndo()); // empty undo
        const QImage before = s->document().flatten();
        s->selectAll();
        s->nudge(10, 0);
        QVERIFY(s->document().flatten() != before); // it moved: not baked into the picture
    }

    void editNeedsExactlyOneSelectedEntry() {
        plant("a", "2026-11-03T08:00:00");
        plant("b", "2026-11-03T07:00:00");
        window().open();
        QVERIFY(!model().edit()); // nothing selected
        model().selectAll();
        QVERIFY(!model().edit());
        QVERIFY(!editor().sessionForHistory("a"));
    }

    void anEntryWithoutEditsOpensOnTheOriginal() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        QVERIFY(model().edit());
        QCOMPARE(editor().sessionForHistory("a")->annotationCount(), 0);
        QCOMPARE(editor().sessionForHistory("a")->document().size(), QSize(96, 64));
    }

    void editOnAnEntryOpenInAThumbnailsEditorBringsThatEditorForward() {
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        history().flush();
        const int thumb = stack().ids().first();
        const QString id = stack().historyId(thumb);
        QVERIFY(!id.isEmpty());
        EditorSession *onStack = editor().open(thumb);
        QVERIFY(onStack);
        QSignalSpy opened(&editor(), &Editor::opened);
        QSignalSpy forward(&editor(), &Editor::broughtForward);
        window().open();
        model().select(id);
        QVERIFY(model().edit());
        QCOMPARE(opened.size(), 0); // no second Editor
        QCOMPARE(forward.size(), 1);
        QCOMPARE(forward.first().first().value<EditorSession *>(), onStack);
        QVERIFY(!editor().sessionForHistory(id));
    }

    void editOnAnEntryThatIsAnUnopenedThumbnailUsesTheThumbnailsEditor() {
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        history().flush();
        const int thumb = stack().ids().first();
        const QString id = stack().historyId(thumb);
        window().open();
        model().select(id);
        QVERIFY(model().edit());
        QVERIFY(editor().session(thumb)); // the Thumbnail's own Editor
        QVERIFY(!editor().sessionForHistory(id));
        QVERIFY(model().edit()); // and again: still the one
        QCOMPARE(editor().session(thumb), editor().open(thumb));
    }

    void aThumbnailRestoredWhileTheEntryIsOpenFromHistoryOpensThatEditor() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        QVERIFY(model().edit());
        EditorSession *fromHistory = editor().sessionForHistory("a");
        QVERIFY(fromHistory);
        QCOMPARE(stack().restoreFromHistory({"a"}), 1);
        const int thumb = stack().ids().first();
        QSignalSpy opened(&editor(), &Editor::opened);
        QCOMPARE(editor().open(thumb), fromHistory); // the Thumbnail's Edit: no second Editor
        QCOMPARE(opened.size(), 0);
        QVERIFY(!stack().isHidden(thumb));
    }

    void aSecondEditBringsTheSameEditorForward() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        QSignalSpy opened(&editor(), &Editor::opened);
        QSignalSpy forward(&editor(), &Editor::broughtForward);
        QVERIFY(model().edit());
        EditorSession *first = editor().sessionForHistory("a");
        QVERIFY(model().edit());
        QCOMPARE(editor().sessionForHistory("a"), first);
        QCOMPARE(opened.size(), 1);
        QCOMPARE(forward.size(), 1);
        QCOMPARE(forward.first().first().value<EditorSession *>(), first);
    }

    // The Editor already open may be on another workspace: Hyprland is asked to focus
    // that very window (its own title), which takes you there, like Alt+Print for History.
    void aSecondEditAsksHyprlandToFocusThatEditorsWindow() {
        plant("a", "2026-11-03T08:00:00");
        plant("b", "2026-11-03T07:00:00");
        window().open();
        model().select("b");
        QVERIFY(model().edit());
        model().select("a");
        QVERIFY(model().edit());
        EditorSession *a = editor().sessionForHistory("a");
        EditorSession *b = editor().sessionForHistory("b");
        QVERIFY(a && b);
        QVERIFY(a->windowTitle() != b->windowTitle());
        QVERIFY(a->windowTitle().startsWith("Tarn editor")); // Tarn's Hyprland rule matches ^(Tarn editor.*)$
        const int before = env->calls("hyprctl").size();
        model().select("b");
        QVERIFY(model().edit());
        const auto calls = env->calls("hyprctl").mid(before);
        bool focused = false;
        for (const QStringList &c : calls)
            focused |= c.value(0) == "eval" && c.value(1).contains("hl.dsp.focus") && c.value(1).contains(b->windowTitle() + "$");
        QVERIFY2(focused, qPrintable(calls.isEmpty() ? QString("no hyprctl call") : calls.last().join(' ')));
    }

    // ---- closing ----

    void doneWritesTheProjectFileFocusesTheHistoryWindowAndAddsNoThumbnail() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        QVERIFY(model().edit());
        QCOMPARE(stack().totalCount(), 0); // opening hid and added nothing
        EditorSession *s = editor().sessionForHistory("a");
        s->document().draw("rect", drag({16, 12}, {80, 52}));
        const int focusBefore = focusCalls();
        s->done();
        QVERIFY(hasProject("a"));
        QVERIFY(focusCalls() > focusBefore);
        QCOMPARE(stack().totalCount(), 0);
        QVERIFY(!editor().sessionForHistory("a"));
        QVERIFY(window().isOpen());
        // ...and it reopens with the Annotation.
        QVERIFY(model().edit());
        QCOMPARE(editor().sessionForHistory("a")->annotationCount(), 1);
    }

    void escWritesNothingAndAddsNoThumbnail() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        QVERIFY(model().edit());
        EditorSession *s = editor().sessionForHistory("a");
        s->document().draw("rect", drag({16, 12}, {80, 52}));
        s->cancel();
        QVERIFY(!hasProject("a"));
        QCOMPARE(stack().totalCount(), 0);
        QVERIFY(!editor().sessionForHistory("a"));
    }

    void doneWithNothingLeftDeletesTheProjectFile() {
        plant("a", "2026-11-03T08:00:00");
        edit("a");
        window().open();
        model().select("a");
        QVERIFY(model().edit());
        EditorSession *s = editor().sessionForHistory("a");
        s->selectAll();
        s->deleteSelection();
        s->done();
        QVERIFY(!hasProject("a"));
    }

    void theKeepingClosesGiveTheFinishedPictureWriteTheFileAndAddNoThumbnail() {
        for (const QString &way : {"copy", "save", "saveAs", "pin"}) {
            plant(way, "2026-11-03T08:00:00");
            window().open();
            model().reload();
            model().select(way);
            QVERIFY(model().edit());
            EditorSession *s = editor().sessionForHistory(way);
            s->document().draw("rect", drag({16, 12}, {80, 52}));
            const QByteArray finished = s->document().toPng();
            QVERIFY(finished != png());
            if (way == "copy") {
                QVERIFY(s->copyAndClose());
                QCOMPARE(TestEnv::readFile(env->calls("tarn-clip").last().last()), finished);
            } else if (way == "save") {
                QVERIFY(s->saveAndClose());
                QVERIFY(!TestEnv::filesIn(env->screenshots()).isEmpty());
            } else if (way == "saveAs") {
                QVERIFY(s->saveAs(env->scratch() + "/elsewhere.png"));
                QCOMPARE(QImage(env->scratch() + "/elsewhere.png"), QImage::fromData(finished, "PNG"));
            } else {
                QVERIFY(s->pin());
                QCOMPARE(env->daemon().service<tarn::Pins>().ids().size(), 1);
            }
            QVERIFY2(hasProject(way), qPrintable(way));
            QCOMPARE(stack().totalCount(), 0);
            QVERIFY(!editor().sessionForHistory(way));
        }
    }

    // ---- the Edited mark follows the Project file, live ----

    void editedFollowsTheProjectFileAsEditsAreSavedAndReverted() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        QVERIFY(!model().isEdited("a"));
        QVERIFY(!model().summary().contains("Edited"));
        QVERIFY(model().edit());
        EditorSession *s = editor().sessionForHistory("a");
        s->document().draw("rect", drag({16, 12}, {80, 52}));
        s->done();
        history().flush(); // delivers the change event
        QVERIFY(model().isEdited("a"));
        QVERIFY(model().summary().endsWith("Edited"));
        QVERIFY(model().selectedEdited());
        QVERIFY(model().revertToOriginal());
        QVERIFY(!model().isEdited("a"));
        QVERIFY(!model().summary().contains("Edited"));
        QVERIFY(!model().selectedEdited());
    }

    void plantedEditsShowAsEditedWhenTheWindowOpens() {
        plant("a", "2026-11-03T08:00:00");
        plant("b", "2026-11-03T07:00:00");
        edit("a");
        window().open();
        QVERIFY(model().isEdited("a"));
        QVERIFY(!model().isEdited("b"));
    }

    // ---- Revert to original ----

    void revertSendsOnlyTheProjectFileToTheTrash() {
        plant("a", "2026-11-03T08:00:00");
        edit("a");
        window().open();
        model().select("a");
        QVERIFY(model().revertToOriginal());
        QVERIFY(!hasProject("a"));
        QCOMPARE(TestEnv::filesIn(trashFiles()), (QStringList{"a.edit.json"}));
        QVERIFY(QFile::exists(history().pngPath("a")));
        QVERIFY(QFile::exists(history().descriptionPath("a")));
        QCOMPARE(model().captureCount(), 1);
    }

    void revertRefusesAnEntryWithoutEditsOrASelectionOfSeveral() {
        plant("a", "2026-11-03T08:00:00");
        plant("b", "2026-11-03T07:00:00");
        edit("a");
        window().open();
        model().select("b");
        QVERIFY(!model().revertToOriginal()); // b has no edits
        model().select("a");
        QVERIFY(model().revertToOriginal());
        QVERIFY(!model().revertToOriginal()); // nothing left to revert
        QVERIFY(TestEnv::filesIn(trashFiles()).size() == 1);
    }

    // ---- the sweep ----

    void theSweepSkipsAnEntryOpenInAnEditorAndTakesItAfterItCloses() {
        plant("old", "2025-01-01T00:00:00");
        window().open();
        model().select("old");
        QVERIFY(model().edit());
        QCOMPARE(history().sweep(), 0);
        QVERIFY(QFile::exists(history().pngPath("old")));
        editor().sessionForHistory("old")->cancel();
        QCOMPARE(history().sweep(), 1);
        QVERIFY(!QFile::exists(history().pngPath("old")));
    }

    void theSweepSkipsAnEntryOnTheStackAndTakesItAfterItCloses() {
        plant("old", "2025-01-01T00:00:00");
        window().open();
        model().select("old");
        QCOMPARE(model().restore(), 1);
        QCOMPARE(history().sweep(), 0);
        QVERIFY(QFile::exists(history().pngPath("old")));
        stack().closeAll();
        QCOMPARE(history().sweep(), 1);
        QVERIFY(!QFile::exists(history().pngPath("old")));
    }

    void aSkippedEntryKeepsItsRetentionRuleUntilItIsTaken() {
        // Shortening the retention sends newly expired entries to the Trash. One that was
        // open at that moment is skipped, and must still go to the Trash (not for good)
        // on the first sweep after it closes.
        plant("mid", "2026-10-25T00:00:00"); // 9 days old: inside 1 month, outside 1 week
        window().open();
        model().select("mid");
        QVERIFY(model().edit());
        QDir().mkpath(env->scratch() + "/config/tarn");
        QFile config(env->scratch() + "/config/tarn/config.toml");
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
        config.write("keep_history_for = \"1 week\"\n");
        config.close();
        QTest::qWait(300); // the Setting's change runs a sweep, which skips the open entry
        QVERIFY(QFile::exists(history().pngPath("mid")));
        editor().sessionForHistory("mid")->cancel();
        QCOMPARE(history().sweep(), 1);
        QVERIFY(QFile::exists(trashFiles() + "/mid.png"));
    }
};

QTEST_MAIN(EditFromHistoryTest)
#include "edit_from_history_test.moc"
