// Saved colors. Core: the list rules, the Editor's + / remove / Eyedropper,
// and the state file across a restart. QML: the color popover and the Frame panel
// load, show the same list and bind to it.
#include "test_env.h"

#include "editor/editor.h"
#include "editor_windows.h"
#include "palette/palette.h"
#include "saved_colors/saved_colors.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>

using tarn::EditorSession;
using tarn::SavedColors;
using tarn::test::TestEnv;

namespace {
QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}
QQuickItem *findItem(QQuickItem *from, const QString &objectName) {
    if (from->objectName() == objectName)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = findItem(child, objectName))
            return hit;
    return nullptr;
}
void collectNamed(QQuickItem *from, const QString &name, QList<QQuickItem *> &out) {
    if (from->objectName() == name)
        out << from;
    for (QQuickItem *child : from->childItems())
        collectNamed(child, name, out);
}
QPoint centerOf(QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }
QColor gray(int n) { return QColor(n, n, n); }
} // namespace

class SavedColorsTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    int shots = 0;

    SavedColors &saved() { return env->daemon().service<SavedColors>(); }
    tarn::ThumbnailStack &stack() { return env->daemon().service<tarn::ThumbnailStack>(); }
    tarn::Editor &editor() { return env->daemon().service<tarn::Editor>(); }
    int addShot() {
        QImage image(96, 64, QImage::Format_RGB32);
        image.fill(qRgb(30, 90, 150));
        return stack().add(pngOf(image), env->daemon().now().addSecs(shots++));
    }
    QString stateFile() const { return env->scratch() + "/state/tarn/state.toml"; }
    QString stateText() const {
        QFile f(stateFile());
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        shots = 0;
    }
    void cleanup() { env.reset(); }

    void startsEmpty() { QVERIFY(saved().colors().isEmpty()); }

    void newestIsOnTheLeft() {
        saved().save(gray(10));
        saved().save(gray(20));
        QCOMPARE(saved().colors(), (QList<QColor>{gray(20), gray(10)}));
    }

    void atMostEightAndTheOldestIsDropped() {
        for (int i = 1; i <= 10; ++i)
            saved().save(gray(i * 10));
        QCOMPARE(saved().colors().size(), 8);
        QCOMPARE(saved().colors().first(), gray(100));
        QCOMPARE(saved().colors().last(), gray(30));
    }

    void anEqualColorMovesToTheFrontInsteadOfRepeating() {
        saved().save(gray(10));
        saved().save(gray(20));
        saved().save(gray(30));
        saved().save(gray(10));
        QCOMPARE(saved().colors(), (QList<QColor>{gray(10), gray(30), gray(20)}));
    }

    void sameRgbWithAnotherOpacityIsADifferentColor() {
        saved().save(QColor(255, 0, 0, 255));
        saved().save(QColor(255, 0, 0, 128));
        QCOMPARE(saved().colors().size(), 2);
        QCOMPARE(saved().colors().first().alpha(), 128);
    }

    void removeTakesOneOut() {
        saved().save(gray(10));
        saved().save(gray(20));
        saved().save(gray(30));
        saved().remove(1);
        QCOMPARE(saved().colors(), (QList<QColor>{gray(30), gray(10)}));
        saved().remove(7); // out of range: nothing
        QCOMPARE(saved().colors().size(), 2);
    }

    void theListSurvivesARestartWithOpacity() {
        saved().save(QColor(255, 0, 0, 128));
        saved().save(gray(40));
        QVERIFY(stateText().contains("[colors]"));
        env->restartDaemon();
        QCOMPARE(saved().colors(), (QList<QColor>{gray(40), QColor(255, 0, 0, 128)}));
    }

    void removingTheLastOneClearsTheFile() {
        saved().save(gray(10));
        saved().remove(0);
        env->restartDaemon();
        QVERIFY(saved().colors().isEmpty());
        QVERIFY(!stateText().contains("saved"));
    }

    void aGarbledValueIsIgnoredPerEntryAndDuplicatesCollapse() {
        saved().save(gray(10));
        QFile f(stateFile());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[colors]\nsaved = \"nonsense,#ff112233,#ff112233\"\n");
        f.close();
        env->restartDaemon();
        QCOMPARE(saved().colors(), (QList<QColor>{QColor(0x11, 0x22, 0x33)}));
    }

    // ---- through the Editor ----

    void plusInTheEditorSavesTheCurrentColorWithOpacity() {
        EditorSession *s = editor().open(addShot());
        s->setColor(QColor(10, 20, 30, 100));
        s->saveCurrentColor();
        QCOMPARE(saved().colors(), (QList<QColor>{QColor(10, 20, 30, 100)}));
        QCOMPARE(s->savedColors().size(), 1);
        QCOMPARE(s->savedColors().first().toString(), QColor(10, 20, 30, 100).name(QColor::HexArgb));
    }

    void theEyedropperNeverSaves() {
        EditorSession *s = editor().open(addShot());
        s->beginEyedropper();
        s->pointerPressed(10.5, 10.5);
        s->pointerReleased(10.5, 10.5);
        QCOMPARE(s->color(), QColor(30, 90, 150));
        QVERIFY(saved().colors().isEmpty());
    }

    void thePlusInTheFramePanelSavesThePlainColorAndIsOffOtherwise() {
        EditorSession *s = editor().open(addShot());
        QVERIFY(!s->frameIsPlain());
        s->saveFrameColor();
        QVERIFY(saved().colors().isEmpty());
        s->setFrameBackground("plain", {{"color", "#80102030"}});
        QVERIFY(s->frameIsPlain());
        s->saveFrameColor();
        QCOMPARE(saved().colors(), (QList<QColor>{QColor::fromRgba(qRgba(0x10, 0x20, 0x30, 0x80))}));
        s->setFrameBackground("none");
        QVERIFY(!s->frameIsPlain());
        s->saveFrameColor();
        QCOMPARE(saved().colors().size(), 1);
    }

    void twoEditorsShareTheRow() {
        EditorSession *a = editor().open(addShot());
        EditorSession *b = editor().open(addShot());
        a->setColor(gray(50));
        a->saveCurrentColor();
        QCOMPARE(b->savedColors().size(), 1);
        b->removeSavedColor(0);
        QVERIFY(a->savedColors().isEmpty());
    }

    // ---- QML ----

    void thePopoverAndTheFramePanelShowTheSameRowAndBind() {
        saved().save(gray(60));
        saved().save(gray(70));
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        const int id = addShot();
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && view->status() == QQuickView::Ready && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        auto *root = view->rootObject();
        EditorSession *s = editor().session(id);

        // The Frame panel's row: grayed + while the Frame is off, live on a plain Frame.
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(root, "backgroundButton")));
        QQuickItem *panel = findItem(root, "framePanel");
        QVERIFY(panel->isVisible());
        QQuickItem *panelRow = findItem(panel, "savedRow");
        QVERIFY(panelRow);
        QVERIFY(findItem(panelRow, "saved-1"));
        QVERIFY(!findItem(panelRow, "saved-2"));
        QQuickItem *plus = findItem(panelRow, "savedPlus");
        QVERIFY(plus->opacity() < 1);
        s->setFrameBackground("plain", {{"color", "#ff336699"}});
        QTest::qWait(30);
        QCOMPARE(plus->opacity(), 1.0);
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(plus));
        QCOMPARE(saved().colors().size(), 3);
        QCOMPARE(saved().colors().first(), QColor(0x33, 0x66, 0x99));
        QTest::qWait(30);
        QVERIFY(findItem(panelRow, "saved-2"));

        // The color popover's row: the same list; right-click removes at once.
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(root, "colorButton")));
        QTest::qWait(50);
        QTRY_VERIFY(findItem(view->contentItem(), "swatch-0")); // the popover lives in the window's overlay
        QList<QQuickItem *> rows;
        collectNamed(view->contentItem(), "savedRow", rows);
        QCOMPARE(rows.size(), 2);
        QQuickItem *popRow = rows[0] == panelRow ? rows[1] : rows[0];
        QVERIFY(popRow);
        QVERIFY(findItem(popRow, "saved-2"));
        QTest::mouseClick(view, Qt::RightButton, {}, centerOf(findItem(popRow, "saved-0")));
        QCOMPARE(saved().colors().size(), 2);
        QCOMPARE(saved().colors().first(), gray(70));
    }
};

QTEST_MAIN(SavedColorsTest)
#include "saved_colors_test.moc"
