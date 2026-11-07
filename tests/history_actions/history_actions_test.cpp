// Acting on History entries from the History window. Every action goes through
// the list model (what the QML calls) and is asserted on what is visible from outside:
// the clipboard helper's call and file, files written, the Trash, hyprctl's Pin rule,
// the Thumbnail stack. The window stays open after every action.
#include "test_env.h"

#include "document/capture_document.h"
#include "history/history.h"
#include "history_window/history_window.h"
#include "pin/pins.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using tarn::CaptureDocument;
using tarn::Frame;
using tarn::History;
using tarn::HistoryModel;
using tarn::HistoryWindow;
using tarn::Pins;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

class HistoryActionsTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    History &history() { return env->daemon().service<History>(); }
    HistoryWindow &window() { return env->daemon().service<HistoryWindow>(); }
    HistoryModel &model() { return window().model(); }
    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    Pins &pins() { return env->daemon().service<Pins>(); }
    QString historyDir() const { return env->scratch() + "/data/tarn/history"; }
    QString trashFiles() const { return env->scratch() + "/data/Trash/files"; }
    QString shared() const { return env->scratch() + "/cache/tarn/shared"; }
    int notifications() const { return env->calls("notify-send").size(); }

    static QByteArray png(const QColor &color = Qt::red) {
        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(color);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    }
    void plant(const QString &id, const QString &when, const QString &app = {}) {
        QDir().mkpath(historyDir());
        QFile image(historyDir() + "/" + id + ".png");
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.write(png());
        QJsonObject d{{"captureTime", when}, {"kind", app.isEmpty() ? "area" : "window"}};
        if (!app.isEmpty())
            d["app"] = app;
        QFile json(historyDir() + "/" + id + ".json");
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(QJsonDocument(d).toJson());
    }
    // Gives an entry edits (a Frame), so its finished picture differs from the original.
    void edit(const QString &id) {
        CaptureDocument document(QImage::fromData(TestEnv::readFile(historyDir() + "/" + id + ".png"), "PNG"));
        Frame f;
        f.background.kind = "gradient";
        f.background.params = {{"stops", QVariantList{"#ffff0000", "#ff0000ff"}}, {"angle", 90}};
        f.padding = 0.2;
        document.setFrame(f);
        history().saveProject(id, document);
        history().flush();
    }
    QStringList stackHistoryIds() {
        QStringList out;
        for (int id : stack().ids())
            out << stack().historyId(id);
        return out;
    }
    QString inFiles(const QString &dir) { return TestEnv::filesIn(dir).join(","); }

private slots:
    void init() { env = std::make_unique<TestEnv>(); } // the clock reads 2026-11-03 09:30:15
    void cleanup() { env.reset(); }

    // ---- Copy ----

    void copyPutsTheSelectedEntrysPictureOnTheClipboardAndTheWindowStaysOpen() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        QVERIFY(model().copy());
        const auto calls = env->calls("tarn-clip");
        QCOMPARE(calls.size(), 1);
        QCOMPARE(TestEnv::readFile(calls.first().last()), png());
        QVERIFY(window().isOpen());
        QCOMPARE(model().selectedId(), QString("a")); // still selected, still listed
        QCOMPARE(model().captureCount(), 1);
        QCOMPARE(notifications(), 0);
    }

    void copyGivesTheFinishedPictureOfAnEditedEntry() {
        plant("a", "2026-11-03T08:00:00");
        edit("a");
        window().open();
        model().select("a");
        QVERIFY(model().copy());
        const QByteArray copied = TestEnv::readFile(env->calls("tarn-clip").first().last());
        QVERIFY(copied != png());
        const QImage finished = QImage::fromData(copied, "PNG");
        QVERIFY(finished.width() > 40); // the Frame's padding grew the picture
        QCOMPARE(copied, history().editedPng("a"));
    }

    void copyFailureNotifiesAndKeepsTheWindow() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        env->setMode("tarn-clip", "fail");
        QVERIFY(!model().copy());
        QCOMPARE(notifications(), 1);
        QVERIFY(window().isOpen());
    }

    // ---- Pin ----

    void pinOpensAFreshPinEveryTime() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        QVERIFY(model().pin());
        QCOMPARE(pins().ids().size(), 1);
        QVERIFY(model().pin()); // the same entry again: another Pin, never the old one
        QCOMPARE(pins().ids().size(), 2);
        QCOMPARE(pins().image(pins().ids().first()).size(), QSize(40, 30));
        QVERIFY(window().isOpen());
        QCOMPARE(stack().totalCount(), 0); // a Pin from History adds no Thumbnail
        bool opened = false;
        for (const auto &call : env->calls("hyprctl"))
            opened = opened || (call.value(0) == "eval" && call.value(1).contains("Tarn pin"));
        QVERIFY(opened);
    }

    void pinUsesTheFinishedPicture() {
        plant("a", "2026-11-03T08:00:00");
        edit("a");
        window().open();
        model().select("a");
        QVERIFY(model().pin());
        QCOMPARE(pins().png(pins().ids().first()), history().editedPng("a"));
    }

    // ---- Save as ----

    void saveAsWritesTheFinishedPictureRemembersTheFolderAndRecordsThePath() {
        plant("a", "2026-11-03T08:00:00");
        edit("a");
        window().open();
        model().select("a");
        QCOMPARE(model().saveAsFolder(), env->screenshots()); // nothing remembered yet
        const QString target = env->scratch() + "/elsewhere/mine.png";
        QVERIFY(model().saveAs(target));
        QCOMPARE(TestEnv::readFile(target), history().editedPng("a"));
        QCOMPARE(model().saveAsFolder(), env->scratch() + "/elsewhere");
        history().flush();
        const QJsonObject d = QJsonDocument::fromJson(TestEnv::readFile(historyDir() + "/a.json")).object();
        QVERIFY(d.value("savedPaths").toArray().contains(target));
        QVERIFY(window().isOpen());
    }

    void saveAsPicksTheFormatFromTheExtension() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        const QString target = env->scratch() + "/out/shot.jpg";
        QVERIFY(model().saveAs(target));
        QVERIFY(TestEnv::readFile(target).startsWith("\xff\xd8"));
    }

    void saveAsFailureNotifies() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        QVERIFY(!model().saveAs(env->scratch() + "/out/shot.bmp")); // not a format Tarn writes
        QCOMPARE(notifications(), 1);
    }

    // ---- Show in Files ----

    void showInFilesOpensTheHistoryFolderInTheFileManager() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        QVERIFY(model().showInFiles());
        QTRY_COMPARE(env->calls("xdg-open"), (QList<QStringList>{{historyDir()}}));
        QVERIFY(window().isOpen());
    }

    // ---- Multi-select rules ----

    void singleEntryActionsRefuseAnythingButOneSelectedEntry() {
        plant("a", "2026-11-03T08:00:00");
        plant("b", "2026-11-03T09:00:00");
        window().open();
        QVERIFY(!model().copy()); // nothing selected
        model().select("a");
        model().toggle("b");
        QCOMPARE(model().selectedCount(), 2);
        QVERIFY(!model().singleSelected());
        QCOMPARE(model().selectedId(), QString());
        QVERIFY(!model().copy());
        QVERIFY(!model().pin());
        QVERIFY(!model().saveAs(env->scratch() + "/x.png"));
        QVERIFY(!model().showInFiles());
        QVERIFY(env->calls("tarn-clip").isEmpty());
        QVERIFY(pins().ids().isEmpty());
        QVERIFY(!QFile::exists(env->scratch() + "/x.png"));
        QCOMPARE(notifications(), 0);
    }

    void ctrlClickShiftClickAndSelectAll() {
        for (int i = 1; i <= 5; ++i)
            plant(QString("e%1").arg(i), QString("2026-11-03T0%1:00:00").arg(i));
        window().open(); // grid order: e5 e4 e3 e2 e1
        model().select("e4");
        model().toggle("e2");
        QCOMPARE(model().selectedIds(), (QStringList{"e4", "e2"}));
        QCOMPARE(model().summary(), QString("2 selected"));
        model().toggle("e4"); // Ctrl-click again takes it out
        QCOMPARE(model().selectedIds(), (QStringList{"e2"}));
        QVERIFY(model().singleSelected());
        QCOMPARE(model().selectedId(), QString("e2"));

        model().select("e5");
        model().extendTo("e3"); // Shift-click: the range from the anchor
        QCOMPARE(model().selectedIds(), (QStringList{"e5", "e4", "e3"}));
        model().extendTo("e1");
        QCOMPARE(model().selectedIds(), (QStringList{"e5", "e4", "e3", "e2", "e1"}));
        model().extendTo("e4"); // shrinks again, anchor unchanged
        QCOMPARE(model().selectedIds(), (QStringList{"e5", "e4"}));

        model().select("e3");
        model().selectAll();
        QCOMPARE(model().selectedCount(), 5);
        QCOMPARE(model().summary(), QString("5 selected"));
        QCOMPARE(model().index(1).data(HistoryModel::SelectedRole).toBool(), true);

        model().move("down", 3); // an arrow key collapses to one entry
        QCOMPARE(model().selectedCount(), 1);
        model().clearSelection();
        QCOMPARE(model().selectedCount(), 0);
        QCOMPARE(model().summary(), QString("Select a capture"));
    }

    // ---- Restore ----

    void restoreBringsTheSelectedEntriesBackAsThumbnails() {
        plant("a", "2026-11-03T08:00:00", "firefox");
        plant("b", "2026-11-03T09:00:00");
        window().open();
        model().select("a");
        model().toggle("b");
        QCOMPARE(model().restore(), 2);
        QCOMPARE(stackHistoryIds(), (QStringList{"a", "b"})); // oldest first: the newest is nearest the corner
        QCOMPARE(stack().count(), 2);
        QCOMPARE(stack().originalImage(stack().ids().first()).size(), QSize(40, 30));
        QVERIFY(window().isOpen());
        QCOMPARE(model().captureCount(), 2); // History keeps them
        QVERIFY(env->calls("tarn-clip").isEmpty()); // no automatic Copy for a restored Thumbnail
    }

    void restoreBringsBackAtMostFour() {
        for (int i = 1; i <= 6; ++i)
            plant(QString("e%1").arg(i), QString("2026-11-03T0%1:00:00").arg(i));
        window().open();
        model().selectAll();
        QCOMPARE(model().restore(), 4);
        QCOMPARE(stackHistoryIds(), (QStringList{"e3", "e4", "e5", "e6"})); // the four newest
        QCOMPARE(stack().totalCount(), 4);
    }

    void restoreSkipsEntriesAlreadyOnTheStackAndFollowsOverflow() {
        for (int i = 1; i <= 6; ++i)
            plant(QString("e%1").arg(i), QString("2026-11-03T0%1:00:00").arg(i));
        window().open();
        model().select("e1");
        QCOMPARE(model().restore(), 1);
        QCOMPARE(model().restore(), 0); // already on the stack: nothing comes back twice
        QCOMPARE(stack().totalCount(), 1);

        model().selectAll();
        QCOMPARE(model().restore(), 4); // e1 is skipped: the next four newest take its place
        // Five wanted room on a stack of four: the oldest (e1) was auto-saved and closed.
        QCOMPARE(stackHistoryIds(), (QStringList{"e3", "e4", "e5", "e6"}));
        QCOMPARE(TestEnv::filesIn(env->screenshots()).size(), 1);
    }

    void restoreSkipsEntriesBeingEdited() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        QCOMPARE(model().restore(), 1);
        stack().setHidden(stack().ids().first(), true); // its Editor is open
        QCOMPARE(model().restore(), 0);
        QCOMPARE(stack().totalCount(), 1);
    }

    void restoredEditedEntriesComeBackAsTheFinishedPicture() {
        plant("a", "2026-11-03T08:00:00");
        edit("a");
        window().open();
        model().select("a");
        QCOMPARE(model().restore(), 1);
        const int id = stack().ids().first();
        QCOMPARE(stack().edited(id), history().editedPng("a"));
        QCOMPARE(stack().originalImage(id).size(), QSize(40, 30)); // the Editor still gets the original
        QCOMPARE(stack().historyId(id), QString("a"));
    }

    // ---- Delete ----

    void deleteMovesTheWholeGroupOfEverySelectedEntryToTheTrashWithoutAsking() {
        plant("a", "2026-11-03T08:00:00");
        plant("b", "2026-11-03T09:00:00");
        plant("c", "2026-11-03T09:10:00");
        edit("a");
        window().open();
        model().select("a");
        model().toggle("b");
        QCOMPARE(model().trashSelected(), 2);
        QCOMPARE(TestEnv::filesIn(trashFiles()), (QStringList{"a.edit.json", "a.json", "a.png", "b.json", "b.png"}));
        QVERIFY(!QFile::exists(historyDir() + "/a.png"));
        QVERIFY(!QFile::exists(historyDir() + "/a.json"));
        QVERIFY(!QFile::exists(historyDir() + "/a.edit.json"));
        QVERIFY(QFile::exists(historyDir() + "/c.png")); // the one not selected stays
        QCOMPARE(model().captureCount(), 1);
        QCOMPARE(model().selectedCount(), 0);
        QVERIFY(window().isOpen());
        QCOMPARE(notifications(), 0);
    }

    void deleteWithNothingSelectedDoesNothing() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        QCOMPARE(model().trashSelected(), 0);
        QCOMPARE(model().captureCount(), 1);
    }

    void aDeletedEntryComesBackFromTheTrashWithItsEdits() {
        plant("a", "2026-11-03T08:00:00");
        edit("a");
        window().open();
        model().select("a");
        QCOMPARE(model().trashSelected(), 1);
        QVERIFY(history().restore("a"));
        QVERIFY(history().hasProject("a"));
        QCOMPARE(model().captureCount(), 1);
    }
};

QTEST_MAIN(HistoryActionsTest)
#include "history_actions_test.moc"
