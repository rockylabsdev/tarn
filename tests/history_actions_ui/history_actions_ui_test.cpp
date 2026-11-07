// The History window's QML actions, driven with real key events and button
// clicks off-screen. Behavior is asserted in tests/history_actions; here: the keys
// reach the actions, the bottom bar shows `N selected` and grays single-entry
// actions, and the window stays open. needs-eyes: hover buttons and the right-click
// menu with a real pointer.
#include "test_env.h"

#include "history/history.h"
#include "history_window/history_window.h"
#include "history_windows.h"
#include "palette/palette.h"
#include "pin/pins.h"
#include "thumbnails/thumbnail_stack.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
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

struct Fixture {
    TestEnv env;
    tarn::HistoryWindow &service = env.daemon().service<tarn::HistoryWindow>();
    tarn::HistoryWindows windows{service, env.daemon().service<tarn::Palette>()};

    void plant(const QString &id, const QString &when) {
        const QString dir = env.scratch() + "/data/tarn/history";
        QDir().mkpath(dir);
        QImage image(64, 40, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        image.save(dir + "/" + id + ".png");
        QFile json(dir + "/" + id + ".json");
        if (json.open(QIODevice::WriteOnly))
            json.write(QJsonDocument(QJsonObject{{"captureTime", when}, {"kind", "area"}}).toJson());
    }
    QQuickView *open() {
        service.open();
        QQuickView *view = windows.view();
        if (!view || view->status() != QQuickView::Ready || !QTest::qWaitForWindowExposed(view))
            return nullptr;
        QTest::qWait(50);
        return view;
    }
    bool enabled(QQuickView *view, const QString &name) { return findItem(view->rootObject(), name)->isEnabled(); }
    void click(QQuickView *view, const QString &name) {
        QQuickItem *item = findItem(view->rootObject(), name);
        const QPointF p = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, p.toPoint());
    }
};

} // namespace

class HistoryActionsUiTest : public QObject {
    Q_OBJECT
private slots:
    void ctrlCCopiesPPinsAndTheWindowStaysOpen() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00");
        QQuickView *view = f.open();
        QVERIFY(view);
        f.service.model().select("a");
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(f.env.calls("tarn-clip").size(), 1);
        QTest::keyClick(view, Qt::Key_P);
        QCOMPARE(f.env.daemon().service<tarn::Pins>().ids().size(), 1);
        QVERIFY(f.service.isOpen());
        QVERIFY(view->isVisible());
    }

    // A copy or Pin that worked shows the Thumbnail's check on that tile's button for
    // a moment, from the keys and menu too (the tile's buttons show while it does).
    // Watched on the window's root: the grid may rebuild its tiles at any moment (the
    // folder watcher), so a tile found earlier can be a dead copy.
    void aCopyOrPinThatWorkedShowsACheckOnTheTile() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00");
        QQuickView *view = f.open();
        QVERIFY(view);
        QQuickItem *root = view->rootObject();
        f.service.model().select("a");
        QTRY_VERIFY(view->activeFocusItem()); // keys reach History only once it has focus
        QSignalSpy confirmed(root, SIGNAL(confirmedActionChanged()));
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
        QTRY_VERIFY(!confirmed.isEmpty());
        QCOMPARE(root->property("confirmedId").toString(), QString("a"));
        QTRY_COMPARE(root->property("confirmedAction").toString(), QString()); // and goes back by itself
        QTest::keyClick(view, Qt::Key_P);
        QTRY_COMPARE(root->property("confirmedAction").toString(), QString("pin"));

        // The tile shows it: the check on that button, and the buttons without a hover.
        QMetaObject::invokeMethod(root, "confirm", Q_ARG(QVariant, "a"), Q_ARG(QVariant, "copy"));
        QQuickItem *tile = findItem(root, "tile"); // not the day heading
        QVERIFY(tile);
        QVERIFY(findItem(tile, "hoverCopy")->property("done").toBool());
        QVERIFY(!findItem(tile, "hoverPin")->property("done").toBool());
        QVERIFY(findItem(tile, "hoverCopy")->isVisible());
    }

    void aCopyThatFailedShowsNoCheck() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00");
        QQuickView *view = f.open();
        QVERIFY(view);
        f.env.setMode("tarn-clip", "fail");
        f.service.model().select("a");
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
        QTest::qWait(50);
        QCOMPARE(view->rootObject()->property("confirmedAction").toString(), QString());
    }

    // Delete takes only its own tile away: the others stay as they are (no rebuild, so no
    // reloading every picture, which took seconds with edited tiles).
    void deleteKeepsTheOtherTilesAsTheyAre() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00");
        f.plant("b", "2026-11-03T08:00:00");
        QQuickView *view = f.open();
        QVERIFY(view);
        QPointer<QQuickItem> first = findItem(view->rootObject(), "tile"); // "a", the newest
        QVERIFY(first);
        f.service.model().select("b");
        QTRY_VERIFY(view->activeFocusItem());
        QTest::keyClick(view, Qt::Key_Delete);
        QCOMPARE(f.service.model().captureCount(), 1);
        QTest::qWait(50);
        QVERIFY2(first, "the remaining tile was rebuilt");
    }

    void rRestoresAndDeleteSendsToTheTrash() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00");
        f.plant("b", "2026-11-03T08:00:00");
        QQuickView *view = f.open();
        QVERIFY(view);
        f.service.model().select("a");
        QTest::keyClick(view, Qt::Key_R);
        QCOMPARE(f.env.daemon().service<tarn::ThumbnailStack>().totalCount(), 1);
        QTest::keyClick(view, Qt::Key_Delete);
        QCOMPARE(f.service.model().captureCount(), 1);
        QVERIFY(QFile::exists(f.env.scratch() + "/data/Trash/files/a.png"));
        QVERIFY(f.service.isOpen());
    }

    void ctrlAThenOnlyRestoreAndDeleteAreLive() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00");
        f.plant("b", "2026-11-03T08:00:00");
        QQuickView *view = f.open();
        QVERIFY(view);
        QVERIFY(!f.enabled(view, "actCopy")); // nothing selected
        QVERIFY(!f.enabled(view, "actRestore"));
        QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(f.service.model().selectedCount(), 2);
        QTest::qWait(30);
        QCOMPARE(findItem(view->rootObject(), "summary")->property("text").toString(), QString("2 selected"));
        QVERIFY(!f.enabled(view, "actCopy"));
        QVERIFY(!f.enabled(view, "actPin"));
        QVERIFY(!f.enabled(view, "actSaveAs"));
        QVERIFY(f.enabled(view, "actRestore"));
        QVERIFY(f.enabled(view, "actDelete"));
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier); // grayed: nothing happens
        QTest::keyClick(view, Qt::Key_P);
        QVERIFY(f.env.calls("tarn-clip").isEmpty());
        QVERIFY(f.env.daemon().service<tarn::Pins>().ids().isEmpty());
    }

    void theBottomBarButtonsActOnTheSelection() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00");
        QQuickView *view = f.open();
        QVERIFY(view);
        f.service.model().select("a");
        QTest::qWait(30);
        QVERIFY(f.enabled(view, "actCopy"));
        f.click(view, "actCopy");
        QCOMPARE(f.env.calls("tarn-clip").size(), 1);
        f.click(view, "actDelete");
        QCOMPARE(f.service.model().captureCount(), 0);
        QVERIFY(f.service.isOpen());
    }

    void escapeStillClosesWhenNoMenuIsOpen() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(!f.service.isOpen());
    }
};

QTEST_MAIN(HistoryActionsUiTest)
#include "history_actions_ui_test.moc"
