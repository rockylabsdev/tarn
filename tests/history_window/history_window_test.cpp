// The History window's list model (what the grid binds to) and the
// `tarn history` command. The model's rows are this window's one shaped output
// that isn't pixels, so tests read them the way QML does: through the roles.
#include "test_env.h"

#include "history/history.h"
#include "history_window/history_window.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using tarn::History;
using tarn::HistoryModel;
using tarn::HistoryWindow;
using tarn::test::TestEnv;

class HistoryWindowTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    History &history() { return env->daemon().service<History>(); }
    HistoryWindow &window() { return env->daemon().service<HistoryWindow>(); }
    HistoryModel &model() { return window().model(); }
    QString historyDir() const { return env->scratch() + "/data/tarn/history"; }

    static QByteArray png(int w, int h) {
        QImage image(w, h, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    }
    // An entry as an earlier capture would have left it on disk.
    void plant(const QString &id, const QString &when, const QString &kind = "area", const QString &app = {}, const QString &title = {},
               int w = 40, int h = 30) {
        QDir().mkpath(historyDir());
        QFile image(historyDir() + "/" + id + ".png");
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.write(png(w, h));
        QJsonObject d{{"captureTime", when}, {"kind", kind}};
        if (!app.isEmpty())
            d["app"] = app;
        if (!title.isEmpty())
            d["title"] = title;
        QFile json(historyDir() + "/" + id + ".json");
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(QJsonDocument(d).toJson());
    }
    // The model the way QML reads it: one string per row.
    QStringList rows() {
        QStringList out;
        for (int r = 0; r < model().rowCount(); ++r) {
            const auto index = model().index(r);
            out << (index.data(HistoryModel::HeadingRole).toBool() ? "# " + index.data(HistoryModel::DayRole).toString()
                                                                    : index.data(HistoryModel::IdRole).toString());
        }
        return out;
    }
    QString field(int row, HistoryModel::Role role) { return model().index(row).data(role).toString(); }

private slots:
    void init() { env = std::make_unique<TestEnv>(); } // the clock reads 2026-11-03 09:30:15
    void cleanup() { env.reset(); }

    void entriesComeNewestFirstUnderDayHeadings() {
        plant("a", "2026-11-01T18:00:00");
        plant("b", "2026-11-03T08:00:00");
        plant("c", "2026-11-02T23:59:00");
        plant("d", "2026-11-03T09:10:00");
        plant("e", "2026-10-20T12:00:00");
        window().open();
        QCOMPARE(rows(), (QStringList{"# Today", "d", "b", "# Yesterday", "c", "# Sunday 1 November", "a", "# Tuesday 20 October", "e"}));
        QCOMPARE(model().captureCount(), 5);
    }

    void aTileShowsAppTimeTitleAndSize() {
        plant("w", "2026-11-03T09:10:00", "window", "firefox", "Docs", 640, 480);
        window().open();
        QCOMPARE(field(1, HistoryModel::AppRole), QString("firefox"));
        QCOMPARE(field(1, HistoryModel::TimeRole), QString("09:10"));
        QCOMPARE(field(1, HistoryModel::TitleRole), QString("Docs"));
        QCOMPARE(field(1, HistoryModel::WidthRole), QString("640"));
        QCOMPARE(field(1, HistoryModel::HeightRole), QString("480"));
    }

    void everyKindOfImageCaptureIsListed() {
        plant("a", "2026-11-03T01:00:00", "area");
        plant("w", "2026-11-03T02:00:00", "window", "kitty", "zsh");
        plant("f", "2026-11-03T03:00:00", "fullscreen");
        window().open();
        QCOMPARE(model().captureCount(), 3);
    }

    void emptyHistoryHasNoRows() {
        window().open();
        QCOMPARE(model().rowCount(), 0);
        QCOMPARE(model().captureCount(), 0);
    }

    void aNewCaptureAppearsAtOnceWhileTheWindowIsOpen() {
        plant("old", "2026-11-02T10:00:00");
        window().open();
        QSignalSpy changed(&model(), &HistoryModel::changed);
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        history().flush();
        QVERIFY(changed.count() > 0);
        QCOMPARE(rows(), (QStringList{"# Today", "2026-11-03_09-30-15", "# Yesterday", "old"}));
        QVERIFY(model().index(1).data(HistoryModel::WidthRole).toInt() > 0);
    }

    // Delete (and a new capture) change only their own rows: no model reset, so the open
    // grid keeps every other tile and its picture instead of rebuilding them all.
    void deletingAnEntryRemovesOnlyItsRowsWithoutAReset() {
        plant("a", "2026-11-03T09:00:00");
        plant("b", "2026-11-03T08:00:00");
        plant("c", "2026-11-02T10:00:00");
        window().open();
        QCOMPARE(rows(), (QStringList{"# Today", "a", "b", "# Yesterday", "c"}));
        QSignalSpy reset(&model(), &QAbstractItemModel::modelReset);
        QSignalSpy removed(&model(), &QAbstractItemModel::rowsRemoved);
        QVERIFY(history().trash("b"));
        QCOMPARE(rows(), (QStringList{"# Today", "a", "# Yesterday", "c"}));
        QVERIFY(history().trash("c")); // the last of its day: its heading goes too
        QCOMPARE(rows(), (QStringList{"# Today", "a"}));
        QCOMPARE(reset.count(), 0);
        QVERIFY(removed.count() >= 2);
        QCOMPARE(field(1, HistoryModel::IdRole), QString("a"));
    }

    void aNewCaptureInsertsItsRowsWithoutAReset() {
        plant("old", "2026-11-02T10:00:00");
        window().open();
        QSignalSpy reset(&model(), &QAbstractItemModel::modelReset);
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        history().flush();
        QCOMPARE(rows(), (QStringList{"# Today", "2026-11-03_09-30-15", "# Yesterday", "old"}));
        QCOMPARE(reset.count(), 0);
    }

    void theSweepRemovesEntriesFromTheOpenWindow() {
        plant("ancient", "2020-01-01T10:00:00");
        plant("new", "2026-11-03T08:00:00");
        window().open();
        QCOMPARE(model().captureCount(), 2);
        QCOMPARE(history().sweep(), 1);
        QCOMPARE(rows(), (QStringList{"# Today", "new"}));
    }

    void deletingAThumbnailRemovesItsEntryAndRestoreBringsItBack() {
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        history().flush();
        window().open();
        QCOMPARE(model().captureCount(), 1);
        const QString id = history().ids().first();
        QVERIFY(history().trash(id));
        QCOMPARE(model().captureCount(), 0);
        QVERIFY(history().restore(id));
        QCOMPARE(rows(), (QStringList{"# Today", id}));
    }

    void theModelFollowsEventsAndNeverTheDisk() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        plant("b", "2026-11-03T09:00:00"); // appears on disk with no event
        QCOMPARE(rows(), (QStringList{"# Today", "a"}));
    }

    void theSummaryDescribesTheSelection() {
        plant("w", "2026-11-03T09:10:00", "window", "firefox", "Docs", 640, 480);
        plant("a", "2026-11-03T09:00:00", "area", {}, {}, 100, 50);
        window().open();
        QCOMPARE(model().summary(), QString("Select a capture"));
        model().select("w");
        QCOMPARE(model().summary(), QString("firefox · Docs · 640 × 480"));
        model().select("a");
        QCOMPARE(model().summary(), QString("100 × 50"));
        model().clearSelection();
        QCOMPARE(model().summary(), QString("Select a capture"));
    }

    void arrowKeysMoveTheSelectionThroughTheGrid() {
        // Today: t5 t4 t3 t2 t1 (newest first), Yesterday: y2 y1. Three tiles across.
        for (int i = 1; i <= 5; ++i)
            plant(QString("t%1").arg(i), QString("2026-11-03T0%1:00:00").arg(i));
        plant("y1", "2026-11-02T10:00:00");
        plant("y2", "2026-11-02T11:00:00");
        window().open();
        model().move("down", 3); // nothing selected: the first capture
        QCOMPARE(model().selectedId(), QString("t5"));
        model().move("right", 3);
        QCOMPARE(model().selectedId(), QString("t4"));
        model().move("down", 3); // t5 t4 t3 / t2 t1: below t4 is t1
        QCOMPARE(model().selectedId(), QString("t1"));
        model().move("down", 3); // into Yesterday, same column: y1 (y2 y1 -> col 1)
        QCOMPARE(model().selectedId(), QString("y1"));
        model().move("up", 3);
        QCOMPARE(model().selectedId(), QString("t1"));
        model().move("left", 3);
        QCOMPARE(model().selectedId(), QString("t2"));
        model().move("up", 3);
        QCOMPARE(model().selectedId(), QString("t5"));
        model().move("up", 3); // already at the top
        QCOMPARE(model().selectedId(), QString("t5"));
        model().select("y1");
        model().move("right", 3); // the last capture stays
        QCOMPARE(model().selectedId(), QString("y1"));
        model().move("down", 3);
        QCOMPARE(model().selectedId(), QString("y1"));
    }

    void removingTheSelectedEntryClearsTheSelection() {
        plant("a", "2026-11-03T08:00:00");
        window().open();
        model().select("a");
        QSignalSpy spy(&model(), &HistoryModel::selectionChanged);
        history().entryRemoved("a");
        QCOMPARE(model().selectedId(), QString());
        QVERIFY(spy.count() > 0);
    }

    // ---- `tarn history` ----

    void tarnHistoryOpensTheWindow() {
        QSignalSpy opened(&window(), &HistoryWindow::opened);
        QVERIFY(env->request({"history"}).ok);
        QVERIFY(window().isOpen());
        QCOMPARE(opened.count(), 1);
    }

    // Hyprland ignores a window's resizes after it maps, so a window taller than a small
    // monitor stays clipped: the size is fitted to the monitor before the first show.
    void theWindowOpensNoBiggerThanTheMonitorUnderThePointer() {
        env->setFixture("hyprctl", "monitors.json",
                        R"([{"id":0,"name":"A","x":0,"y":0,"width":1280,"height":720,"scale":1.0,"transform":0},)"
                        R"({"id":1,"name":"B","x":1280,"y":0,"width":1920,"height":1080,"scale":2.0,"transform":0}])");
        env->setPointer(300, 300);
        QCOMPARE(window().openingSize(), QSize(1200, 640));
        env->setPointer(1500, 300); // 960 x 540 logical
        QCOMPARE(window().openingSize(), QSize(880, 460));
    }

    void aBigMonitorGetsTheUsualSize() {
        QCOMPARE(window().openingSize(), QSize(1200, 780)); // the stub's monitor is 2560 x 1440
    }

    void whenOpenButNotFocusedItBringsTheWindowForward() {
        QVERIFY(env->request({"history"}).ok);
        env->setFixture("hyprctl", "activewindow.json", R"({"title":"some other window"})");
        QSignalSpy opened(&window(), &HistoryWindow::opened);
        QVERIFY(env->request({"history"}).ok);
        QVERIFY(window().isOpen());
        QCOMPARE(opened.count(), 0); // only ever one window
        const auto calls = env->calls("hyprctl");
        QVERIFY(!calls.isEmpty());
        const QStringList last = calls.last();
        QCOMPARE(last.value(0), QString("eval"));
        QVERIFY(last.value(1).contains("focus"));
        QVERIFY(last.value(1).contains("Tarn history"));
    }

    void whenFocusedItClosesTheWindow() {
        QVERIFY(env->request({"history"}).ok);
        env->setFixture("hyprctl", "activewindow.json", R"({"title":"Tarn history"})");
        QSignalSpy closed(&window(), &HistoryWindow::closed);
        QVERIFY(env->request({"history"}).ok);
        QVERIFY(!window().isOpen());
        QCOMPARE(closed.count(), 1);
        QVERIFY(env->request({"history"}).ok); // and again opens it
        QVERIFY(window().isOpen());
    }

    void openingReadsHistoryAfresh() {
        QVERIFY(env->request({"history"}).ok);
        window().close();
        plant("a", "2026-11-03T08:00:00");
        QVERIFY(env->request({"history"}).ok);
        QCOMPARE(model().captureCount(), 1);
    }

    // ---- Filters and search ----
    void setText(const QString &id, const QString &text) {
        const QString path = historyDir() + "/" + id + ".json";
        auto d = QJsonDocument::fromJson(TestEnv::readFile(path)).object();
        d["text"] = text;
        QFile json(path);
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(QJsonDocument(d).toJson());
    }
    void plantProject(const QString &id, const QStringList &annotationTexts) {
        QJsonArray annotations;
        for (const QString &t : annotationTexts)
            annotations.append(QJsonObject{{"id", 1}, {"kind", "text"}, {"data", QJsonObject{{"text", t}}}});
        annotations.append(QJsonObject{{"id", 2}, {"kind", "rectangle"}, {"data", QJsonObject{{"x", 1}}}});
        QFile f(historyDir() + "/" + id + ".edit.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{{"annotations", annotations}}).toJson());
    }
    QStringList shown() {
        QStringList out;
        for (const QString &r : rows())
            if (!r.startsWith("# "))
                out << r;
        return out;
    }
    void plantMixed() {
        plant("a", "2026-11-03T08:00:00", "area");
        plant("b", "2026-11-03T08:10:00", "window", "firefox", "Docs");
        plant("c", "2026-11-03T08:20:00", "window", "kitty", "zsh");
        plant("d", "2026-11-03T08:30:00", "fullscreen");
        plant("e", "2026-11-02T08:30:00", "window", "firefox", "Mail");
        window().open();
    }

private slots:
    void kindAndAppFiltersCombine() {
        plantMixed();
        model().setKindFilter("window");
        QCOMPARE(shown(), (QStringList{"c", "b", "e"}));
        model().setAppFilter("firefox");
        QCOMPARE(shown(), (QStringList{"b", "e"}));
        model().setKindFilter("area");
        QCOMPARE(shown(), QStringList{});
        model().setKindFilter("");
        model().setAppFilter("");
        QCOMPARE(shown().size(), 5);
    }

    void aV1TimerEntryCountsAsAnAreaShot() { // Descriptions are never migrated
        plant("a", "2026-11-03T09:00:00", "timer");
        plant("b", "2026-11-03T08:00:00", "fullscreen");
        window().open();
        model().setKindFilter("area");
        QCOMPARE(shown(), QStringList{"a"});
        QCOMPARE(history().captureName("a").kind, QString("area"));
    }

    void theAppListIsMostUsedFirstWithCounts() {
        plantMixed();
        const auto apps = model().apps();
        QCOMPARE(apps.size(), 2);
        QCOMPARE(apps[0].toMap()["name"].toString(), QString("firefox"));
        QCOMPARE(apps[0].toMap()["count"].toInt(), 2);
        QCOMPARE(apps[1].toMap()["name"].toString(), QString("kitty"));
        QCOMPARE(apps[1].toMap()["count"].toInt(), 1);
    }

    void searchMatchesEveryWordCaseInsensitivelyInsideWords() {
        plant("a", "2026-11-03T08:00:00");
        plant("b", "2026-11-03T08:10:00");
        window().open();
        setText("a", "Invoice number 4711 for Acme");
        setText("b", "Invoice paid");
        model().reload();
        model().setSearch("invoice");
        QCOMPARE(shown(), (QStringList{"b", "a"}));
        model().setSearch("INVOICE acme");
        QCOMPARE(shown(), QStringList{"a"});
        model().setSearch("voic cme"); // inside longer words
        QCOMPARE(shown(), QStringList{"a"});
        model().setSearch("invoise"); // nothing fuzzy
        QCOMPARE(shown(), QStringList{});
        model().setSearch("acme paid"); // every word, any entry: none has both
        QCOMPARE(shown(), QStringList{});
    }

    void searchAlsoReadsTitleAppAndTextAnnotations() {
        plant("t", "2026-11-03T08:00:00", "window", "firefox", "Quarterly Report");
        plant("p", "2026-11-03T08:10:00");
        plant("n", "2026-11-03T08:20:00");
        plantProject("p", {"Look HERE", "Callout: ship it"});
        setText("n", "plain");
        window().open();
        model().setSearch("report");
        QCOMPARE(shown(), QStringList{"t"});
        model().setSearch("FIREFOX");
        QCOMPARE(shown(), QStringList{"t"});
        model().setSearch("look here");
        QCOMPARE(shown(), QStringList{"p"});
        model().setSearch("ship");
        QCOMPARE(shown(), QStringList{"p"});
        model().setSearch("rectangle"); // only Text annotations' text, not kinds
        QCOMPARE(shown(), QStringList{});
    }

    void aCalloutsTextIsSearchableAndAnEditedProjectIsReread() {
        plant("c", "2026-11-03T08:00:00");
        QFile f(historyDir() + "/c.edit.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        // A real Callout as CalloutAnnotation::data() writes it.
        f.write(QJsonDocument(QJsonObject{{"annotations", QJsonArray{QJsonObject{
            {"id", 3}, {"kind", "callout"},
            {"data", QJsonObject{{"counter", 2}, {"origin", QJsonObject{{"x", 5}, {"y", 6}}}, {"text", "Tighten this margin"}}}}}}}).toJson());
        f.close();
        window().open();
        model().setSearch("margin");
        QCOMPARE(shown(), QStringList{"c"});
        // Edit from History saves the Project file again and History says the entry changed.
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(QJsonObject{{"annotations", QJsonArray{QJsonObject{
            {"id", 3}, {"kind", "callout"}, {"data", QJsonObject{{"counter", 2}, {"text", "Fixed spacing"}}}}}}}).toJson());
        f.close();
        const auto description = QJsonDocument::fromJson(TestEnv::readFile(historyDir() + "/c.json")).object();
        emit env->daemon().service<History>().entryChanged("c", description);
        QCOMPARE(shown(), QStringList{});
        model().setSearch("spacing");
        QCOMPARE(shown(), QStringList{"c"});
    }

    void searchCombinesWithFilters() {
        plantMixed();
        model().setSearch("o"); // firefox, but not kitty/zsh
        model().setKindFilter("window");
        model().setAppFilter("firefox");
        QCOMPARE(shown(), (QStringList{"b", "e"}));
        model().setSearch("mail");
        QCOMPARE(shown(), QStringList{"e"});
    }

    void escapeClearsTheSearchBeforeClosingAndTheFieldStartsEmpty() {
        plantMixed();
        model().setSearch("docs");
        window().escape();
        QCOMPARE(model().search(), QString());
        QVERIFY(window().isOpen());
        QCOMPARE(shown().size(), 5);
        window().escape();
        QVERIFY(!window().isOpen());
        model().setSearch("docs");
        window().open();
        QCOMPARE(model().search(), QString());
    }

    void theCountReadsReadingMWhileUnreadEntriesRemain() {
        plant("a", "2026-11-03T08:00:00");
        plant("b", "2026-11-03T08:10:00");
        plant("c", "2026-11-03T08:20:00");
        setText("a", "hello world");
        window().open();
        QCOMPARE(model().countText(), QString("3 captures"));
        model().setSearch("hello");
        QCOMPARE(model().countText(), QString("1 capture · reading 2"));
        // the Text reader finishes b: matches appear live
        history().setText("b", "hello again");
        history().flush();
        QTRY_COMPARE(shown(), (QStringList{"b", "a"}));
        QCOMPARE(model().countText(), QString("2 captures · reading 1"));
        history().setText("c", "");
        history().flush();
        QTRY_COMPARE(model().countText(), QString("2 captures"));
    }

    void withoutTesseractThereIsNoReadingCountAndThePlaceholderSaysSo() {
        plant("a", "2026-11-03T08:00:00", "window", "kitty", "zsh");
        window().open();
        QCOMPARE(model().searchPlaceholder(), QString("Search"));
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", "/nonexistent");
        model().setSearch("zsh");
        QCOMPARE(model().searchPlaceholder(), QString("Install tesseract to search text in pictures"));
        QCOMPARE(shown(), QStringList{"a"}); // titles and apps still match
        model().setSearch("nope");
        QCOMPARE(model().countText(), QString("0 captures"));
        qputenv("PATH", path);
    }

    void nothingMatchesNamesTheSearchAndShowEverythingClears() {
        plantMixed();
        QVERIFY(!model().nothingMatches());
        model().setKindFilter("area");
        model().setSearch("zzz");
        QVERIFY(model().nothingMatches());
        QCOMPARE(model().nothingMatchesText(), QString::fromUtf8("Nothing matches \xe2\x80\x9czzz\xe2\x80\x9d"));
        model().showEverything();
        QVERIFY(!model().nothingMatches());
        QCOMPARE(model().search(), QString());
        QCOMPARE(model().kindFilter(), QString());
        QCOMPARE(shown().size(), 5);
    }

    void aFilteredOutSelectionIsDroppedAndArrowsWalkOnlyWhatIsShown() {
        plantMixed();
        model().setKindFilter("window");
        model().move("right", 3);
        QCOMPARE(model().selectedId(), QString("c"));
        model().move("right", 3);
        QCOMPARE(model().selectedId(), QString("b"));
        model().setKindFilter("area");
        QCOMPARE(model().selectedId(), QString());
    }
};

QTEST_MAIN(HistoryWindowTest)
#include "history_window_test.moc"
