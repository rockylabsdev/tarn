// The gradient, wallpaper and blurred-shot tiles in the Background panel (QML,
// compiled in), off-screen. Wallpapers are generated into the scratch folder.
// What this cannot judge is needs-eyes: the tiles' look and the system file
// dialog behind "+".
#include "test_env.h"

#include "editor/editor.h"
#include "editor_windows.h"
#include "palette/palette.h"
#include "thumbnails/thumbnail_stack.h"
#include "wallpaper/wallpapers.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
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

// Every item below `from` with this objectName, in tree order.
void findAll(QQuickItem *from, const QString &objectName, QList<QQuickItem *> &out) {
    if (from->objectName() == objectName)
        out << from;
    for (QQuickItem *child : from->childItems())
        findAll(child, objectName, out);
}

QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

void makePicture(const QString &path, QRgb color) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage image(80, 50, QImage::Format_RGB32);
    image.fill(color);
    QVERIFY(image.save(path, "PNG"));
}

struct Fixture {
    TestEnv env;
    bool seeded;
    tarn::ThumbnailStack &stack = env.daemon().service<tarn::ThumbnailStack>();
    tarn::Editor &editor = env.daemon().service<tarn::Editor>();
    tarn::EditorWindows windows{editor, env.daemon().service<tarn::Palette>()};
    int id = [this] {
        QImage image(400, 300, QImage::Format_RGB32);
        image.fill(qRgb(40, 120, 200));
        return stack.add(pngOf(image), env.daemon().now());
    }();

    // `withPalette`: a palette file exists before the Palette is first read.
    explicit Fixture(bool withPalette = false, int wallpaperFiles = 2)
        : seeded([&] {
              for (int i = 0; i < wallpaperFiles; ++i)
                  makePicture(env.pictures() + QString("/Wallpapers/w%1.png").arg(i), qRgb(200, 40 * i, 90));
              if (withPalette) {
                  QDir().mkpath(env.scratch() + "/state/shell");
                  QFile f(env.scratch() + "/state/shell/palette.json");
                  f.open(QIODevice::WriteOnly);
                  f.write("{\"primary\":\"#fbb974\",\"on_primary\":\"#492900\",\"surface_container_low\":\"#211a14\"}");
                  f.close();
                  QFile config(env.scratch() + "/config/tarn/config.toml");
                  if (!config.open(QIODevice::WriteOnly | QIODevice::Truncate))
                      qFatal("cannot write the Settings file");
                  config.write(("palette_file = \"" + env.scratch() + "/state/shell/palette.json\"\n").toUtf8());
              }
              return true;
          }()) {}

    QQuickView *open() {
        stack.edit(id);
        QQuickView *view = windows.viewFor(id);
        if (!view || view->status() != QQuickView::Ready || !QTest::qWaitForWindowExposed(view))
            return nullptr;
        QTest::qWait(50);
        QTest::keyClick(view, Qt::Key_B); // the panel
        QTest::qWait(50);
        return view;
    }
    tarn::EditorSession *session() { return editor.session(id); }
    static QPoint centerOf(QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }
    static void click(QQuickView *view, QQuickItem *item) { QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(item)); }
};

} // namespace

class BackgroundsUiTest : public QObject {
    Q_OBJECT
private slots:
    void thePanelOffersTwentyImageGradientsAndPicksOneWithAClick() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QList<QQuickItem *> tiles;
        for (int i = 0; i < 20; ++i)
            QVERIFY2(findItem(root, "gradientTile-" + QString::number(i)), qPrintable(QString::number(i)));
        QVERIFY(!findItem(root, "gradientTile-20"));
        Fixture::click(view, findItem(root, "gradientTile-1"));
        QCOMPARE(f.session()->frameMap().value("background").toMap().value("name").toString(), QString("pearl-blush"));
        QVERIFY(f.session()->frameMap().value("on").toBool());
    }

    void everyTileLoadsItsPicture() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        QTest::qWait(100);
        QList<QQuickItem *> images;
        findAll(view->rootObject(), "tileImage", images);
        QCOMPARE(images.size(), 20 + 2 + 3); // gradients, two wallpapers, blurred tiles
        for (QQuickItem *image : images) {
            QCOMPARE(image->property("status").toInt(), 1); // Image.Ready
            QVERIFY(image->property("implicitWidth").toInt() > 0);
        }
    }

    void thePaletteGradientsAreHiddenWithoutAPaletteFileAndShownWithIt() {
        {
            Fixture f(false);
            QQuickView *view = f.open();
            QVERIFY(view);
            QVERIFY(!findItem(view->rootObject(), "paletteGradients")->isVisible());
        }
        Fixture f(true);
        QTRY_VERIFY(f.env.daemon().service<tarn::Palette>().fromFile());
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QVERIFY(findItem(root, "paletteGradients")->isVisible());
        QVERIFY(findItem(root, "paletteGradientTile-0"));
        Fixture::click(view, findItem(root, "paletteGradientTile-0"));
        QCOMPARE(f.session()->frameMap().value("background").toMap().value("slots").toStringList(), QStringList({"accent", "canvas"}));
    }

    void wallpapersAreTheCurrentOneThenTheFolderThenPlus() {
        Fixture f;
        makePicture(f.env.scratch() + "/now.png", qRgb(10, 200, 10));
        f.env.daemon().service<tarn::Wallpapers>().setCurrentSource([&] { return f.env.scratch() + "/now.png"; });
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QList<QQuickItem *> wallpapers;
        findAll(root, "wallpaperTile-0", wallpapers);
        findAll(root, "wallpaperTile-1", wallpapers);
        findAll(root, "wallpaperTile-2", wallpapers);
        QCOMPARE(wallpapers.size(), 3); // current, w0, w1
        QVERIFY(!findItem(root, "wallpaperTile-3"));
        auto *plus = findItem(root, "addWallpaperTile");
        QVERIFY(plus);
        // Order on screen: current, w0, w1, then "+".
        QVERIFY(wallpapers[0]->y() <= wallpapers[1]->y() && (wallpapers[0]->y() < wallpapers[1]->y() || wallpapers[0]->x() < wallpapers[1]->x()));
        QVERIFY(wallpapers[1]->x() < wallpapers[2]->x());
        QVERIFY(wallpapers[2]->x() < plus->x());

        Fixture::click(view, wallpapers[0]);
        const QVariantMap bg = f.session()->frameMap().value("background").toMap();
        QCOMPARE(bg.value("kind").toString(), QString("wallpaper"));
        QCOMPARE(bg.value("path").toString(), f.env.scratch() + "/now.png");
        QVERIFY(bg.value("current").toBool());
        Fixture::click(view, wallpapers[2]);
        QCOMPARE(f.session()->frameMap().value("background").toMap().value("path").toString(), f.env.pictures() + "/Wallpapers/w1.png");
    }

    void plusAsksForAnyImage() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        QSignalSpy asked(f.session(), &tarn::EditorSession::backgroundImageRequested);
        f.session()->chooseBackgroundImage(); // what the "+" tile calls (the system dialog opens in QML)
        QCOMPARE(asked.size(), 1);
        QVERIFY(findItem(view->rootObject(), "addWallpaperTile"));
    }

    void threeBlurredTilesPickAStyle() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        for (int i = 0; i < 3; ++i)
            QVERIFY(findItem(root, "blurTile-" + QString::number(i)));
        QVERIFY(!findItem(root, "blurTile-3"));
        Fixture::click(view, findItem(root, "blurTile-2"));
        QCOMPARE(f.session()->frameMap().value("background").toMap().value("style").toString(), QString("dark"));
    }

    void aDeletedWallpaperShowsAQuietMark() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QVERIFY(!findItem(root, "wallpaperMissing")->isVisible());
        const QString path = f.env.pictures() + "/Wallpapers/w0.png";
        QVERIFY(f.session()->setFrameWallpaper(path));
        QVERIFY(QFile::remove(path));
        f.session()->refreshBackgrounds();
        f.session()->setFrameValue("padding", 0.2); // any change refreshes the frame map
        QTest::qWait(60);
        QVERIFY(findItem(root, "wallpaperMissing")->isVisible());
        QVERIFY(f.env.calls("notify-send").isEmpty());
    }
};

QTEST_MAIN(BackgroundsUiTest)
#include "backgrounds_ui_test.moc"
