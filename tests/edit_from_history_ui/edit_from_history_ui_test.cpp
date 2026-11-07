// The History window's QML for Edit from History, driven off-screen with real
// key events and clicks: Enter and double-click open the Editor, the pencil disc and
// the finished picture appear on edited tiles, the bottom bar has Edit, and
// Revert to original is in the menu of edited entries only. Behavior is asserted in
// tests/edit_from_history. needs-eyes: the hover Edit button and the right-click menu
// with a real pointer, and the Editor window coming forward.
#include "test_env.h"

#include "document/capture_document.h"
#include "editor/editor.h"
#include "history/history.h"
#include "history_window/history_window.h"
#include "history_windows.h"
#include "palette/palette.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QQuickImageProvider>
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
    void edit(const QString &id) {
        auto &history = env.daemon().service<tarn::History>();
        tarn::CaptureDocument document(QImage(history.pngPath(id)));
        tarn::DragGesture g;
        g.from = {8, 8};
        g.to = {40, 30};
        g.path = {g.from, g.to};
        document.draw("rect", g);
        history.saveProject(id, document);
        history.flush();
    }
    QQuickView *open() {
        service.open();
        QQuickView *view = windows.view();
        if (!view || view->status() != QQuickView::Ready || !QTest::qWaitForWindowExposed(view))
            return nullptr;
        QTest::qWait(50);
        return view;
    }
    bool editorOpenOn(const QString &id) { return env.daemon().service<tarn::Editor>().sessionForHistory(id) != nullptr; }
    // The first VISIBLE item of that name (a day heading row has an invisible tile area too).
    QPoint visibleCenter(QQuickView *view, const QString &name) {
        QList<QQuickItem *> todo{view->rootObject()};
        while (!todo.isEmpty()) {
            QQuickItem *item = todo.takeFirst();
            if (item->objectName() == name && item->isVisible())
                return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
            todo += item->childItems();
        }
        return {};
    }
    QPoint center(QQuickView *view, const QString &name) {
        QQuickItem *item = findItem(view->rootObject(), name);
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    }
};

} // namespace

class EditFromHistoryUiTest : public QObject {
    Q_OBJECT
private slots:
    void enterOpensTheEditorOnTheSelectedEntry() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00");
        QQuickView *view = f.open();
        QVERIFY(view);
        QTest::keyClick(view, Qt::Key_Return); // nothing selected: nothing happens
        QVERIFY(!f.editorOpenOn("a"));
        f.service.model().select("a");
        QTest::keyClick(view, Qt::Key_Return);
        QVERIFY(f.editorOpenOn("a"));
        QVERIFY(f.service.isOpen()); // the window stays
    }

    void doubleClickOpensTheEditor() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00");
        QQuickView *view = f.open();
        QVERIFY(view);
        const QPoint p = f.visibleCenter(view, "tileArea");
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, p);
        QCOMPARE(f.service.model().selectedId(), QString("a"));
        QVERIFY(!f.editorOpenOn("a")); // one click selects only
        QTest::mouseDClick(view, Qt::LeftButton, Qt::NoModifier, p);
        QVERIFY(f.editorOpenOn("a"));
    }

    void theBottomBarHasEditInAccentAndGraysItOnASelectionOfSeveral() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00");
        f.plant("b", "2026-11-03T08:00:00");
        QQuickView *view = f.open();
        QVERIFY(view);
        QQuickItem *edit = findItem(view->rootObject(), "actEdit");
        QVERIFY(edit);
        QVERIFY(edit->property("primary").toBool());
        QVERIFY(!edit->isEnabled()); // nothing selected
        f.service.model().select("a");
        QVERIFY(edit->isEnabled());
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, f.center(view, "actEdit"));
        QVERIFY(f.editorOpenOn("a"));
        f.service.model().selectAll();
        QVERIFY(!edit->isEnabled());
    }

    void editedTilesShowThePencilDiscAndTheEditedSummary() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00");
        f.plant("b", "2026-11-03T08:00:00");
        f.edit("a");
        QQuickView *view = f.open();
        QVERIFY(view);
        int marks = 0;
        QList<QQuickItem *> stack{view->rootObject()};
        while (!stack.isEmpty()) {
            QQuickItem *item = stack.takeLast();
            if (item->objectName() == "editedMark" && item->isVisible())
                ++marks;
            stack += item->childItems();
        }
        QCOMPARE(marks, 1); // only the edited entry
        f.service.model().select("a");
        QTest::qWait(30);
        QVERIFY(findItem(view->rootObject(), "summary")->property("text").toString().endsWith("Edited"));
        f.service.model().select("b");
        QTest::qWait(30);
        QVERIFY(!findItem(view->rootObject(), "summary")->property("text").toString().contains("Edited"));
    }

    void editedTilesDrawTheFinishedPicture() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00");
        f.edit("a");
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *provider = static_cast<QQuickImageProvider *>(view->engine()->imageProvider("historypicture"));
        QVERIFY(provider);
        QSize size;
        const QImage tile = provider->requestImage("a/0", &size, QSize());
        QCOMPARE(tile, QImage::fromData(f.env.daemon().service<tarn::History>().editedPng("a"), "PNG"));
        QVERIFY(tile != QImage(f.env.daemon().service<tarn::History>().pngPath("a")));
    }

    void revertToOriginalIsInTheMenuOfEditedEntriesOnly() {
        Fixture f;
        f.plant("a", "2026-11-03T09:00:00");
        f.plant("b", "2026-11-03T08:00:00");
        f.edit("a");
        QQuickView *view = f.open();
        QVERIFY(view);
        QQuickItem *menu = findItem(view->rootObject(), "historyMenu");
        QVERIFY(menu);
        f.service.model().select("b");
        QTest::qWait(30);
        QVERIFY(!findItem(menu, "menu_revert"));
        f.service.model().select("a");
        QTest::qWait(30);
        QQuickItem *revert = findItem(menu, "menu_revert");
        QVERIFY(revert);
        QVERIFY(findItem(menu, "menu_edit"));
        // The row works through the one door the QML uses.
        QMetaObject::invokeMethod(menu, "chosen", Q_ARG(QString, "revert"));
        QTest::qWait(30);
        QVERIFY(!f.service.model().isEdited("a"));
        QVERIFY(!QFile::exists(f.env.daemon().service<tarn::History>().projectPath("a")));
    }
};

QTEST_MAIN(EditFromHistoryUiTest)
#include "edit_from_history_ui_test.moc"
