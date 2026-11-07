// The History window (QML, compiled in) loads and binds to the list model,
// off-screen. Behavior (order, groups, live updates, selection) is asserted in
// tests/history_window; needs-eyes: the opening size and tiles across on the
// real monitor, floating, centering and no animation under Hyprland.
#include "test_env.h"

#include "history/history.h"
#include "history_window/history_window.h"
#include "history_windows.h"
#include "palette/palette.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>

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

void collect(QQuickItem *from, const QString &objectName, QList<QQuickItem *> &out) {
    if (from->objectName() == objectName)
        out << from;
    for (QQuickItem *child : from->childItems())
        collect(child, objectName, out);
}

struct Fixture {
    TestEnv env;
    tarn::HistoryWindow &service = env.daemon().service<tarn::HistoryWindow>();
    tarn::HistoryWindows windows{service, env.daemon().service<tarn::Palette>()};

    void plant(const QString &id, const QString &when, const QString &app = {}) {
        const QString dir = env.scratch() + "/data/tarn/history";
        QDir().mkpath(dir);
        QImage image(64, 40, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        image.save(dir + "/" + id + ".png");
        QJsonObject d{{"captureTime", when}, {"kind", app.isEmpty() ? "area" : "window"}};
        if (!app.isEmpty())
            d["app"] = app;
        QFile json(dir + "/" + id + ".json");
        if (json.open(QIODevice::WriteOnly))
            json.write(QJsonDocument(d).toJson());
    }
    QQuickView *open() {
        service.open();
        QQuickView *view = windows.view();
        if (!view || view->status() != QQuickView::Ready || !QTest::qWaitForWindowExposed(view))
            return nullptr;
        QTest::qWait(50);
        return view;
    }
};

} // namespace

class HistoryWindowUiTest : public QObject {
    Q_OBJECT
private slots:
    // Super+Q closes the window without asking Tarn. The History window then counts as
    // closed, so the next `tarn history` shows it again instead of focusing a hidden window.
    void aWindowClosedByTheCompositorClosesTheHistoryAndTheNextOpenShowsIt() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        view->close(); // Hyprland's Super+Q
        QVERIFY(!f.service.isOpen());
        QVERIFY(f.open());
        QVERIFY(f.service.isOpen());
        QVERIFY(f.windows.view()->isVisible());
    }

    void theWindowIsFramelessTitledForTheWindowRuleAndOpensAtTheFixedSize() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        QCOMPARE(view->title(), QString("Tarn history")); // what tarn.lua's window rule matches
        QVERIFY(view->flags() & Qt::FramelessWindowHint);
        QCOMPARE(view->size(), QSize(1200, 780));
    }

    void theBarsAre52AndEmptyHistoryExplainsItself() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QCOMPARE(findItem(root, "topBar")->height(), 52.0);
        QCOMPARE(findItem(root, "bottomBar")->height(), 52.0);
        QVERIFY(findItem(root, "emptyState")->isVisible());
        QVERIFY(!findItem(root, "grid")->isVisible());
        QCOMPARE(findItem(root, "summary")->property("text").toString(), QString("Select a capture"));
    }

    void theGridBindsToTheModelWithHeadingsAndTiles() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00", "firefox");
        f.plant("b", "2026-11-03T08:00:00");
        f.plant("c", "2026-11-02T08:00:00");
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QList<QQuickItem *> headings, tiles;
        collect(root, "dayHeading", headings);
        collect(root, "tile", tiles);
        QCOMPARE(headings.size(), 2);
        QCOMPARE(tiles.size(), 3);
        QVERIFY(!findItem(root, "emptyState")->isVisible());
        QCOMPARE(findItem(root, "countLabel")->property("text").toString(), QString("3 captures"));
        // At 1200 px wide, 200 px tiles with 14 px gaps: five across, each at least 200 px.
        QVERIFY(tiles.first()->width() >= 200);
        QCOMPARE(root->property("columns").toInt(), 5);
    }

    // The app dropdown's rows light up under the pointer, like the right-click menu's.
    void theAppListRowsHighlightOnHover() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00", "firefox");
        f.plant("b", "2026-11-03T08:00:00", "kitty");
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QQuickItem *button = findItem(root, "appFilter");
        QTest::mouseClick(view, Qt::LeftButton, {}, button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QTRY_VERIFY(findItem(root, "appList")->isVisible());
        QQuickItem *row = findItem(root, "appRow-1");
        QVERIFY(row);
        const QColor before = row->property("color").value<QColor>();
        QTest::mouseMove(view, row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint());
        const QColor hover = view->rootContext()->contextProperty("theme").value<QObject *>()->property("hover").value<QColor>();
        QTRY_COMPARE(row->property("color").value<QColor>(), hover);
        QVERIFY(before != hover);
    }

    // Closing History closes its app list and right-click menu: the next window opens clean.
    void theAppListAndMenuAreClosedWhenHistoryOpensAgain() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00", "firefox");
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QQuickItem *button = findItem(root, "appFilter");
        QTest::mouseClick(view, Qt::LeftButton, {}, button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QTRY_VERIFY(findItem(root, "appList")->isVisible());
        f.service.close();
        QTRY_VERIFY(!view->isVisible());
        QQuickView *again = f.open();
        QVERIFY(again);
        QVERIFY(!findItem(again->rootObject(), "appList")->isVisible());
    }

    void arrowKeysMoveTheRingAndTheBottomBarDescribesTheSelection() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00", "firefox");
        f.plant("b", "2026-11-03T08:00:00");
        QQuickView *view = f.open();
        QVERIFY(view);
        QTest::keyClick(view, Qt::Key_Right);
        QCOMPARE(f.service.model().selectedId(), QString("a"));
        QTest::qWait(30);
        QCOMPARE(findItem(view->rootObject(), "summary")->property("text").toString(), QString("firefox · 64 × 40"));
        QTest::keyClick(view, Qt::Key_Right);
        QCOMPARE(f.service.model().selectedId(), QString("b"));
        QTest::qWait(30);
        QList<QQuickItem *> rings;
        collect(view->rootObject(), "ring", rings);
        int shown = 0;
        for (auto *ring : rings)
            shown += ring->isVisible();
        QCOMPARE(shown, 1);
    }

    void escapeClosesTheWindow() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(!f.service.isOpen());
        QVERIFY(!view->isVisible());
    }

    void ctrlFAndSlashFocusTheSearchFieldTypingFiltersAndEscClearsThenCloses() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00", "firefox");
        f.plant("b", "2026-11-03T08:00:00");
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *field = findItem(view->rootObject(), "searchField");
        QVERIFY(field);
        QVERIFY(!field->hasActiveFocus());
        QTest::keyClick(view, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(field->hasActiveFocus());
        QTest::keyClick(view, Qt::Key_Escape); // nothing typed: Esc in the field closes
        QVERIFY(!f.service.isOpen());
        QQuickView *again = f.open();
        QVERIFY(again);
        QTest::keyClick(again, Qt::Key_Slash);
        QVERIFY(field->hasActiveFocus());
        for (const char c : QByteArray("fire"))
            QTest::keyClick(again, c);
        QCOMPARE(f.service.model().search(), QString("fire"));
        QCOMPARE(f.service.model().shownCount(), 1);
        QTest::qWait(30);
        QCOMPARE(findItem(again->rootObject(), "countLabel")->property("text").toString(), QString("1 capture"));
        QTest::keyClick(again, Qt::Key_Escape);
        QVERIFY(f.service.isOpen());
        QCOMPARE(f.service.model().search(), QString());
        QCOMPARE(f.service.model().shownCount(), 2);
        QTest::keyClick(again, Qt::Key_Escape);
        QVERIFY(!f.service.isOpen());
    }

    void nothingMatchesOffersShowEverything() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00", "firefox");
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        f.service.model().setSearch("zzz");
        QTest::qWait(30);
        QVERIFY(findItem(root, "nothingMatches")->isVisible());
        QVERIFY(!findItem(root, "grid")->isVisible());
        QVERIFY(!findItem(root, "emptyState")->isVisible());
        QMetaObject::invokeMethod(&f.service.model(), "showEverything");
        QTest::qWait(30);
        QVERIFY(!findItem(root, "nothingMatches")->isVisible());
        QVERIFY(findItem(root, "grid")->isVisible());
    }

    void reopeningShowsTheSameSingleWindow() {
        Fixture f;
        QQuickView *first = f.open();
        QVERIFY(first);
        f.service.close();
        QQuickView *second = f.open();
        QCOMPARE(second, first);
    }
};

QTEST_MAIN(HistoryWindowUiTest)
#include "history_window_ui_test.moc"
