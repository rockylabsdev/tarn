// Gradients, wallpapers and blurred tiles behind a framed shot.
//
// Rendering is checked against reference PNGs (a gradient, a wallpaper and each
// blurred tile) on a 96x64 shot, compared exactly; regenerate with
// TARN_UPDATE_REFERENCES=1 through ctest's environment (docs/agents/verify.md)
// and look at the images first. The rest drives the Wallpapers service and the
// Editor session the way the Background panel does. Wallpapers are generated
// into the scratch folder; nothing here reads the user's real wallpaper or
// Pictures folder.
#include "test_env.h"

#include "document/capture_document.h"
#include "editor/editor.h"
#include "frame/frame.h"
#include "frame/cover_image.h"
#include "frame/gradient_background.h"
#include "frame/image_gradient_background.h"
#include "palette/palette.h"
#include "settings/settings.h"
#include "thumbnails/thumbnail_stack.h"
#include "wallpaper/wallpapers.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

using tarn::CaptureDocument;
using tarn::EditorSession;
using tarn::Frame;
using tarn::Wallpapers;
using tarn::test::TestEnv;

#ifndef TARN_BACKGROUNDS_REFERENCE_DIR
#error "TARN_BACKGROUNDS_REFERENCE_DIR is set by tests/backgrounds/CMakeLists.txt"
#endif

namespace {

const QSize kShot(96, 64);

// A shot with some structure (a gradient and a bright block), so a blur shows.
QImage shotOf(QSize size) {
    QImage image(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x) {
            const qreal fx = qreal(x) / size.width(), fy = qreal(y) / size.height();
            image.setPixel(x, y, qRgb(int(40 + 160 * fx), int(60 + 120 * fy), int(200 - 100 * fx * fy)));
        }
    for (int y = size.height() * 3 / 8; y < size.height() * 5 / 8; ++y)
        for (int x = size.width() / 4; x < size.width() / 2; ++x)
            image.setPixel(x, y, qRgb(250, 240, 90));
    return image;
}

// Hard-edged color blocks, so light, heavy and darkened blurs look clearly different.
QImage blocksOf(QSize size) {
    QImage image(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x) {
            const bool right = x >= size.width() / 2, low = y >= size.height() / 2;
            QRgb c = right ? (low ? qRgb(250, 210, 60) : qRgb(40, 160, 90)) : (low ? qRgb(60, 70, 200) : qRgb(220, 60, 80));
            if (x * 10 / size.width() == 4 || x * 10 / size.width() == 5)
                c = qRgb(255, 255, 255);
            image.setPixel(x, y, c);
        }
    return image;
}

QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// A picture that is the same every time: diagonal stripes of three colors.
QImage stripes(QSize size) {
    QImage image(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x) {
            const int band = ((x + y) / 12) % 3;
            image.setPixel(x, y, band == 0 ? qRgb(230, 80, 60) : band == 1 ? qRgb(250, 200, 70) : qRgb(40, 90, 160));
        }
    return image;
}

void compareWithReference(const QImage &actual, const QString &name) {
    const QString path = QStringLiteral(TARN_BACKGROUNDS_REFERENCE_DIR "/") + name;
    if (qEnvironmentVariableIsSet("TARN_UPDATE_REFERENCES")) {
        QVERIFY(actual.save(path, "PNG"));
        return;
    }
    QImage expected(path);
    QVERIFY2(!expected.isNull(), qPrintable("missing reference " + path));
    QCOMPARE(actual.size(), expected.size());
    QCOMPARE(actual.convertToFormat(QImage::Format_ARGB32), expected.convertToFormat(QImage::Format_ARGB32));
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(bytes);
}

QByteArray paletteFile(const QString &primary) {
    return QString("{\"primary\":\"%1\",\"on_primary\":\"#492900\",\"surface_container_low\":\"#211a14\",\"surface_container_lowest\":\"#130d07\","
                   "\"surface_container\":\"#251e17\",\"surface_container_high\":\"#302921\",\"surface_container_highest\":\"#3c332c\","
                   "\"surface_bright\":\"#403830\",\"on_surface\":\"#eee0d5\",\"on_surface_variant\":\"#d5c3b5\",\"outline\":\"#9d8e81\","
                   "\"outline_variant\":\"#50453a\"}").arg(primary).toUtf8();
}

QStringList names(const QVariantList &list) {
    QStringList out;
    for (const QVariant &v : list)
        out << QFileInfo(v.toMap().value(v.toMap().contains("path") ? "path" : "id").toString()).fileName();
    return out;
}

} // namespace

class BackgroundsTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    int shots = 0;

    tarn::ThumbnailStack &stack() { return env->daemon().service<tarn::ThumbnailStack>(); }
    tarn::Editor &editor() { return env->daemon().service<tarn::Editor>(); }
    Wallpapers &wallpapers() { return env->daemon().service<Wallpapers>(); }
    int addShot() { return stack().add(pngOf(shotOf(kShot)), QDateTime(QDate(2026, 11, 3), QTime(9, 30, 0)).addSecs(shots++)); }
    QString wallpaperDir() const { return env->pictures() + "/Wallpapers"; }
    QString shellDir() const { return env->scratch() + "/state/shell"; }
    QString configFile() const { return env->scratch() + "/config/tarn/config.toml"; }
    QString wallpaperFile() const { return env->scratch() + "/shell/wallpaper"; }
    // The Settings file as a user writes it, then a fresh daemon so the start state is settled.
    void useWallpaperFile() {
        writeFile(configFile(), QString("wallpaper_file = \"%1\"\n").arg(wallpaperFile()).toUtf8());
        env->restartDaemon();
    }

    QString makeImage(const QString &path, QSize size = QSize(160, 90)) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QImage image = stripes(size);
        if (!image.save(path, "PNG"))
            qFatal("could not write %s", qPrintable(path));
        return path;
    }
    QImage framed(EditorSession *s) { return s->document().flatten(); }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        shots = 0;
    }
    void cleanup() { env.reset(); }

    // ---- reference PNGs: a gradient, a wallpaper, each blurred tile ----

    void aGradientMatchesItsReference() {
        CaptureDocument doc(shotOf(kShot));
        Frame f;
        // The color-stop kind lives on for Palette gradients; these are the old Sunset's stops.
        f.background = {QStringLiteral("gradient"),
                        {{"stops", QVariantList{"#ffff9a5a", "#ffff4f7b", "#ff7b3fe4"}}, {"angle", 135}}};
        f.padding = 0.22;
        doc.setFrame(f);
        compareWithReference(doc.flatten(), "gradient-sunset.png");
    }

    // An image gradient fills the background like a wallpaper, on any shot size.
    void anImageGradientMatchesItsReferenceOnTwoShotSizes() {
        for (const QSize size : {QSize(96, 64), QSize(288, 192)}) {
            CaptureDocument doc(shotOf(size));
            Frame f;
            f.background = {QStringLiteral("image-gradient"), tarn::imageGradientParams("glacial-bloom")};
            f.padding = 0.22;
            doc.setFrame(f);
            compareWithReference(doc.flatten(), QString("image-gradient-glacial-bloom-%1x%2.png").arg(size.width()).arg(size.height()));
        }
    }

    void anImageGradientIsCoverScaledAndCenteredOnAnyRatio() {
        // A 1:1 Ratio: the background is the whole output square, and the 16:9
        // picture is cropped from its center, never stretched.
        CaptureDocument doc(shotOf(QSize(64, 64)));
        Frame f;
        f.background = {QStringLiteral("image-gradient"), tarn::imageGradientParams("sea-glass")};
        f.padding = 0.5;
        f.ratio = QStringLiteral("1:1");
        doc.setFrame(f);
        const QImage out = doc.flatten();
        QCOMPARE(out.width(), out.height());
        const QImage expected = tarn::coveredImage(tarn::imageGradientPath("sea-glass"), out.size());
        QVERIFY(!expected.isNull());
        // The corners are background (the shot sits in the middle).
        for (const QPoint p : {QPoint(1, 1), QPoint(out.width() - 2, 1), QPoint(1, out.height() - 2), QPoint(out.width() - 2, out.height() - 2)})
            QCOMPARE(out.pixel(p), expected.pixel(p));
    }

    void aWallpaperMatchesItsReference() {
        const QString file = makeImage(env->scratch() + "/stripes.png", QSize(160, 90));
        CaptureDocument doc(shotOf(kShot));
        Frame f;
        f.background = {QStringLiteral("wallpaper"), {{"path", file}}};
        f.padding = 0.22;
        doc.setFrame(f);
        compareWithReference(doc.flatten(), "wallpaper.png");
    }

    void eachBlurredTileMatchesItsReference_data() {
        QTest::addColumn<QString>("style");
        QTest::newRow("light") << "light";
        QTest::newRow("heavy") << "heavy";
        QTest::newRow("dark") << "dark";
    }
    void eachBlurredTileMatchesItsReference() {
        QFETCH(QString, style);
        CaptureDocument doc(blocksOf(kShot));
        Frame f;
        f.background = {QStringLiteral("blur"), {{"style", style}}};
        f.padding = 0.22;
        doc.setFrame(f);
        compareWithReference(doc.flatten(), "blur-" + style + ".png");
    }

    void blurredTilesAreMadeFromTheShotAndGetStrongerThenDarker() {
        const QImage shot = shotOf(kShot);
        auto tile = [&](const char *style) { return tarn::renderBackgroundTile("blur", {{"style", style}}, QSize(96, 64), shot).convertToFormat(QImage::Format_RGB32); };
        // The yellow block's edge: a column crossing it jumps in the shot; a blur smooths it.
        auto jump = [](const QImage &img) {
            int most = 0;
            for (int y = 1; y < img.height(); ++y)
                most = std::max(most, std::abs(qBlue(img.pixel(30, y)) - qBlue(img.pixel(30, y - 1))));
            return most;
        };
        QVERIFY(jump(shot) > jump(tile("light")));
        QVERIFY(jump(tile("light")) > jump(tile("heavy")));
        // Darkened: same blur, every pixel darker.
        const QImage heavy = tile("heavy"), dark = tile("dark");
        for (int y = 0; y < heavy.height(); y += 7)
            for (int x = 0; x < heavy.width(); x += 7)
                QVERIFY(qGray(dark.pixel(x, y)) < qGray(heavy.pixel(x, y)));
        // Made from THIS shot: another shot gives another tile.
        const QImage other = tarn::renderBackgroundTile("blur", {{"style", "heavy"}}, QSize(96, 64), stripes(QSize(96, 64)));
        QVERIFY(other != heavy);
    }

    void aBlurIsTheSameOnABigShotAsOnASmallOne() {
        const QImage small = tarn::renderBackgroundTile("blur", {{"style", "heavy"}}, QSize(96, 64), shotOf(QSize(96, 64)));
        const QImage large = tarn::renderBackgroundTile("blur", {{"style", "heavy"}}, QSize(288, 192), shotOf(QSize(288, 192)))
                                 .scaled(96, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        qint64 total = 0;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 96; ++x)
                total += std::abs(qGray(small.pixel(x, y)) - qGray(large.pixel(x, y)));
        QVERIFY2(total / (96 * 64) < 6, qPrintable(QString::number(total / (96 * 64))));
    }

    void aWallpaperCoversTheFrameWithoutStretching() {
        const QString file = makeImage(env->scratch() + "/wide.png", QSize(200, 50)); // 4:1
        const QImage tile = tarn::renderBackgroundTile("wallpaper", {{"path", file}}, QSize(40, 40), QImage());
        QCOMPARE(tile.size(), QSize(40, 40));
        // Not squeezed: a 4:1 picture cropped to a square shows only its middle quarter, so
        // the diagonal bands are as wide as in the original (12 px bands, 17 px along a row).
        const QImage cover = tarn::renderBackgroundTile("wallpaper", {{"path", file}}, QSize(200, 50), QImage());
        QCOMPARE(cover.convertToFormat(QImage::Format_RGB32), stripes(QSize(200, 50)));
    }

    // ---- gradients ----

    void gradientsAreColorStopsWithAnAngle() {
        QImage img(64, 64, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            tarn::BackgroundKinds::find("gradient")->paint(p, QRectF(0, 0, 64, 64),
                {{"stops", QVariantList{"#ffff0000", "#ff0000ff"}}, {"angle", 90.0}}, QImage()); // left to right
        }
        QVERIFY(qRed(img.pixel(1, 32)) > 240);
        QVERIFY(qBlue(img.pixel(62, 32)) > 240);
        QCOMPARE(img.pixel(10, 5), img.pixel(10, 60)); // no vertical change
    }

    void stopsMayCarryPositionsAndAGradientWithoutStopsStillPaints() {
        QImage img(64, 64, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        const auto *k = tarn::BackgroundKinds::find("gradient");
        k->paint(p, QRectF(0, 0, 64, 64),
                 {{"stops", QVariantList{QVariantMap{{"color", "#ffff0000"}, {"pos", 0.0}}, QVariantMap{{"color", "#ff0000ff"}, {"pos", 0.25}}}}, {"angle", 90.0}},
                 QImage());
        QVERIFY(qBlue(img.pixel(40, 30)) > 240); // past the last stop: its color
        p.end();
        img.fill(Qt::transparent);
        QPainter q(&img);
        k->paint(q, QRectF(0, 0, 64, 64), {}, QImage());
        QCOMPARE(qAlpha(img.pixel(32, 32)), 255);
    }

    void tarnShipsTwentyImageGradientsInAFixedOrderNamedByFile() {
        const QStringList expected = {"glacial-bloom", "pearl-blush", "crimson-refraction", "lilac-drift", "solar-flare",
                                      "prism-tide", "ember-plum", "violet-afterglow", "amber-horizon", "coral-heat",
                                      "chromatic-fault", "golden-current", "peach-frost", "dusk-haze", "sea-glass",
                                      "moonlit-ink", "peach-mirage", "midnight-current", "orchid-veil", "rose-eclipse"};
        QCOMPARE(tarn::imageGradientNames(), expected);
        EditorSession *s = editor().open(addShot());
        QStringList listed;
        for (const QVariant &v : s->gradients()) {
            QCOMPARE(v.toMap().value("kind").toString(), QString("image-gradient"));
            QCOMPARE(v.toMap().value("label"), v.toMap().value("id")); // named by file
            listed << v.toMap().value("id").toString();
        }
        QCOMPARE(listed, expected);
        // None of the old color-stop gradients is offered any more.
        for (const QString &old : {"sunset", "ocean", "aurora", "peach", "berry", "mint", "dusk", "graphite"})
            QVERIFY(!listed.contains(old));
    }

    void everyShippedImageIsTheSuppliedJpegCompiledIn() {
        for (const QString &name : tarn::imageGradientNames()) {
            QImageReader reader(tarn::imageGradientPath(name));
            QCOMPARE(reader.format().toLower(), QByteArray("jpeg"));
            QCOMPARE(reader.size(), QSize(1672, 941));
            QVERIFY2(tarn::imageGradientAverage(name).isValid(), qPrintable(name));
        }
        QVERIFY(tarn::imageGradientParams("nonsense").isEmpty());
    }

    void pickingAnImageGradientIsOneUndoStep() {
        EditorSession *s = editor().open(addShot());
        s->setFrameGradient("sea-glass");
        QCOMPARE(s->frameMap().value("kind").toString(), QString("image-gradient"));
        QCOMPARE(s->frameMap().value("background").toMap().value("name").toString(), QString("sea-glass"));
        s->undo();
        QVERIFY(!s->frameMap().value("on").toBool());
        s->redo();
        QCOMPARE(s->frameMap().value("background").toMap().value("name").toString(), QString("sea-glass"));
    }

    void anUnknownImageGradientNameFallsBackQuietlyAndIsMarkedMissing() {
        const auto *k = tarn::BackgroundKinds::find("image-gradient");
        QVERIFY(k);
        QVERIFY(k->missing({{"name", "gone"}}));
        QVERIFY(!k->missing(tarn::imageGradientParams("dusk-haze")));
        QImage img(32, 32, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter q(&img);
        k->paint(q, QRectF(0, 0, 32, 32), {{"name", "gone"}}, QImage());
        q.end();
        QCOMPARE(qAlpha(img.pixel(16, 16)), 255);
    }

    void theClosestImageGradientIsPickedByAverageColor() {
        for (const QString &name : tarn::imageGradientNames())
            QCOMPARE(tarn::closestImageGradient(tarn::imageGradientAverage(name)), name);
        // Stops are averaged first.
        const QString name = tarn::imageGradientNames().at(5);
        const QColor c = tarn::imageGradientAverage(name);
        QCOMPARE(tarn::closestImageGradient(QVariantList{c.name(), c.name(QColor::HexArgb)}), name);
        QCOMPARE(tarn::closestImageGradient(QVariantList{}), tarn::imageGradientNames().first());
    }

    // ---- Palette gradients ----

    void paletteGradientsAreHiddenWhileThereIsNoPaletteFile() {
        EditorSession *s = editor().open(addShot());
        QVERIFY(!env->daemon().service<tarn::Palette>().fromFile());
        QVERIFY(s->paletteGradients().isEmpty());
        QCOMPARE(s->gradients().size(), 20);
    }

    void paletteGradientsTakeTheirStopsFromPaletteSlots() {
        writeFile(shellDir() + "/palette.json", paletteFile("#fbb974"));
        writeFile(configFile(), ("palette_file = \"" + shellDir() + "/palette.json\"\n").toUtf8());
        QTRY_VERIFY(env->daemon().service<tarn::Palette>().fromFile());
        EditorSession *s = editor().open(addShot());
        const QVariantList list = s->paletteGradients();
        QVERIFY(list.size() >= 3);
        const QVariantMap first = list.first().toMap();
        QCOMPARE(first.value("id").toString(), QString("accent-canvas"));
        const QVariantMap params = first.value("params").toMap();
        QCOMPARE(params.value("slots").toStringList(), QStringList({"accent", "canvas"}));
        QCOMPARE(params.value("stops").toList().first().toString(), QString("#fffbb974")); // the accent slot
        s->setFramePaletteGradient("accent-canvas");
        QCOMPARE(s->frameMap().value("background").toMap().value("slots").toStringList(), QStringList({"accent", "canvas"}));
        QVERIFY(framed(s).size() != kShot);
    }

    void withoutAPaletteFileAPaletteGradientFallsBackToTheClosestImageGradient() {
        const auto &palette = env->daemon().service<tarn::Palette>();
        QVERIFY(!palette.fromFile());
        for (const auto &g : tarn::paletteGradients()) {
            const QVariantMap params = tarn::paletteGradientParams(g.id, palette);
            QCOMPARE(params, tarn::imageGradientParams(tarn::closestImageGradientForLegacy(g.fallbackId)));
            QVERIFY(!params.isEmpty());
            QVERIFY(!params.contains("slots"));
        }
        EditorSession *s = editor().open(addShot());
        s->setFramePaletteGradient("accent-canvas");
        QCOMPARE(s->frameMap().value("kind").toString(), QString("image-gradient"));
        QCOMPARE(s->frameMap().value("background").toMap().value("name").toString(),
                 tarn::closestImageGradientForLegacy("dusk")); // accent-canvas's old fallback
    }

    // ---- wallpapers ----

    void theFirstLineOfTheWallpaperFileNamesTheCurrentWallpaper() {
        const QString file = makeImage(env->scratch() + "/Current One.png");
        writeFile(wallpaperFile(), (file + "\nsecond line is ignored\n").toUtf8());
        useWallpaperFile();
        QCOMPARE(wallpapers().current(), file);
        const QVariantList list = wallpapers().list();
        QCOMPARE(list.first().toMap().value("path").toString(), file);
        QVERIFY(list.first().toMap().value("current").toBool());
    }

    void withoutTheSettingTheOldShellFilesAreNotRead() {
        const QString file = makeImage(env->scratch() + "/src.png");
        writeFile(shellDir() + "/wallpaper", (file + "\n").toUtf8());
        writeFile(shellDir() + "/palette.source", file.toUtf8());
        env->restartDaemon();
        QCOMPARE(wallpapers().current(), QString());
    }

    void aMissingUnreadableOrNonsenseWallpaperFileLeavesOnlyTheFolder() {
        const QString inFolder = makeImage(wallpaperDir() + "/only.png");
        QCOMPARE(names(wallpapers().list()), QStringList({"only.png"})); // no Setting
        useWallpaperFile(); // the Setting names a file that is not there
        QCOMPARE(wallpapers().current(), QString());
        QCOMPARE(names(wallpapers().list()), QStringList({"only.png"}));
        writeFile(wallpaperFile(), "/nowhere/gone.png\n");
        QTRY_COMPARE(wallpapers().current(), QString());
        writeFile(wallpaperFile(), (env->scratch() + "\n").toUtf8()); // a folder
        QCOMPARE(wallpapers().current(), QString());
        writeFile(wallpaperFile(), "");
        QCOMPARE(wallpapers().current(), QString());
        const QString text = env->scratch() + "/notes.png"; // looks like an image, is not
        writeFile(text, "not a picture");
        writeFile(wallpaperFile(), text.toUtf8());
        QCOMPARE(wallpapers().current(), QString());
        QFile::remove(wallpaperFile());
        QCOMPARE(names(wallpapers().list()), QStringList({"only.png"}));
        QVERIFY(env->calls("notify-send").isEmpty()); // quiet
    }

    void aChangedWallpaperFileUpdatesTheChoiceWithoutARestart() {
        const QString one = makeImage(env->scratch() + "/one.png");
        const QString two = makeImage(env->scratch() + "/two.png");
        writeFile(wallpaperFile(), (one + "\n").toUtf8());
        useWallpaperFile();
        QSignalSpy changed(&wallpapers(), &Wallpapers::changed);
        QCOMPARE(wallpapers().current(), one);
        writeFile(wallpaperFile(), (two + "\n").toUtf8());
        QTRY_COMPARE(wallpapers().current(), two);
        QTRY_VERIFY(changed.count() >= 1);
    }

    void aFileAppearingAfterTarnStartedIsPickedUp() {
        const QString one = makeImage(env->scratch() + "/one.png");
        useWallpaperFile();
        QSignalSpy changed(&wallpapers(), &Wallpapers::changed);
        writeFile(wallpaperFile(), (one + "\n").toUtf8());
        QTRY_VERIFY(changed.count() >= 1);
        QCOMPARE(wallpapers().current(), one);
    }

    void aToolReplacingTheWallpaperFileByRenameIsStillSeen() {
        const QString one = makeImage(env->scratch() + "/one.png");
        const QString two = makeImage(env->scratch() + "/two.png");
        const QString three = makeImage(env->scratch() + "/three.png");
        writeFile(wallpaperFile(), (one + "\n").toUtf8());
        useWallpaperFile();
        for (const QString &next : {two, three}) { // twice: the watch must survive each swap
            QSignalSpy changed(&wallpapers(), &Wallpapers::changed);
            writeFile(wallpaperFile() + ".tmp", (next + "\n").toUtf8());
            QFile::remove(wallpaperFile());
            QVERIFY(QFile::rename(wallpaperFile() + ".tmp", wallpaperFile()));
            QTRY_VERIFY(changed.count() >= 1);
            QCOMPARE(wallpapers().current(), next);
        }
    }

    void theFirstRunTemplateOffersWallpaperFileEmptyWithAComment() {
        const QString text = QString::fromUtf8(TestEnv::readFile(configFile()));
        QVERIFY2(text.contains("wallpaper_file = \"\"\n"), qPrintable(text));
        const int at = text.indexOf("wallpaper_file");
        QVERIFY(text.lastIndexOf("# ", at) > text.lastIndexOf("\n\n", at)); // a comment sits right above it
    }

    void theCurrentWallpaperSeamCanBeStubbed() {
        const QString file = makeImage(env->scratch() + "/seam.png");
        wallpapers().setCurrentSource([file] { return file; });
        QCOMPARE(wallpapers().current(), file);
        wallpapers().setCurrentSource([] { return QString(); });
        QCOMPARE(wallpapers().current(), QString());
    }

    void wallpapersListTheCurrentOneFirstThenTheFolderInNameOrder() {
        makeImage(wallpaperDir() + "/b-two.png");
        makeImage(wallpaperDir() + "/A-one.png");
        makeImage(wallpaperDir() + "/c-ten10.jpg");
        makeImage(wallpaperDir() + "/c-ten9.jpg");
        writeFile(wallpaperDir() + "/readme.txt", "not an image");
        const QString current = makeImage(env->scratch() + "/elsewhere/now.png");
        wallpapers().setCurrentSource([current] { return current; });
        EditorSession *s = editor().open(addShot());
        const QVariantList list = s->wallpapers();
        QCOMPARE(names(list), QStringList({"now.png", "A-one.png", "b-two.png", "c-ten9.jpg", "c-ten10.jpg"}));
        QVERIFY(list.first().toMap().value("current").toBool());
        QVERIFY(!list.at(1).toMap().value("current").toBool());
        QCOMPARE(list.first().toMap().value("label").toString(), QString("Current wallpaper"));
    }

    void theCurrentWallpaperIsNotListedTwiceWhenItIsInTheFolder() {
        const QString inFolder = makeImage(wallpaperDir() + "/mine.png");
        makeImage(wallpaperDir() + "/other.png");
        wallpapers().setCurrentSource([inFolder] { return inFolder; });
        QCOMPARE(names(wallpapers().list()), QStringList({"mine.png", "other.png"}));
    }

    void noCurrentWallpaperAndNoFolderIsJustAnEmptyList() {
        QVERIFY(wallpapers().list().isEmpty()); // ~/Pictures/Wallpapers does not exist
    }

    void theWallpaperFolderSettingDefaultsToPicturesWallpapersAndIsInConfigToml() {
        QCOMPARE(env->daemon().service<tarn::Settings>().wallpaperFolder(), env->pictures() + "/Wallpapers");
        const QByteArray text = TestEnv::readFile(configFile());
        QVERIFY(text.contains("wallpaper_folder = \"~/Pictures/Wallpapers\"") || text.contains("wallpaper_folder = \"~"));
        QVERIFY(text.contains("wallpaper_folder"));
    }

    void changingTheWallpaperFolderSettingAppliesLive() {
        makeImage(wallpaperDir() + "/one.png");
        const QString other = env->scratch() + "/more";
        makeImage(other + "/two.png");
        makeImage(other + "/three.png");
        EditorSession *s = editor().open(addShot());
        QCOMPARE(names(s->wallpapers()), QStringList({"one.png"}));
        QSignalSpy changed(s, &EditorSession::backgroundsChanged);
        QByteArray text = TestEnv::readFile(configFile());
        QList<QByteArray> lines = text.split('\n');
        for (QByteArray &line : lines)
            if (line.startsWith("wallpaper_folder = "))
                line = "wallpaper_folder = \"" + other.toUtf8() + "\"";
        writeFile(configFile(), lines.join('\n'));
        QTRY_VERIFY(!changed.isEmpty());
        QCOMPARE(names(s->wallpapers()), QStringList({"three.png", "two.png"}));
    }

    void anyImageCanBePickedWithPlusAndANonImageIsRefused() {
        const QString file = makeImage(env->scratch() + "/picked.weird"); // no known suffix: "+" takes any picture Qt can read
        EditorSession *s = editor().open(addShot());
        QVERIFY(s->setFrameWallpaper(QUrl::fromLocalFile(file).toString()));
        QCOMPARE(s->frameMap().value("kind").toString(), QString("wallpaper"));
        QCOMPARE(s->frameMap().value("background").toMap().value("path").toString(), file);
        QVERIFY(!s->frameMap().value("background").toMap().value("current").toBool());

        const QString text = env->scratch() + "/text.png";
        writeFile(text, "nope");
        QVERIFY(!s->setFrameWallpaper(text));
        QCOMPARE(s->frameMap().value("background").toMap().value("path").toString(), file); // unchanged
        QCOMPARE(env->calls("notify-send").size(), 1);
    }

    void pickingTheCurrentWallpaperRemembersThatItWasTheCurrentOne() {
        const QString file = makeImage(env->scratch() + "/now.png");
        wallpapers().setCurrentSource([file] { return file; });
        EditorSession *s = editor().open(addShot());
        QVERIFY(s->setFrameWallpaper(s->wallpapers().first().toMap().value("id").toString(), true));
        QVERIFY(s->frameMap().value("background").toMap().value("current").toBool());
    }

    void aDeletedWallpaperFileFallsBackQuietlyWithAMark() {
        const QString file = makeImage(env->scratch() + "/doomed.png");
        EditorSession *s = editor().open(addShot());
        QVERIFY(s->setFrameWallpaper(file));
        QVERIFY(!s->frameMap().value("missing").toBool());
        const QImage with = framed(s);
        QVERIFY(QFile::remove(file));
        QVERIFY(s->frameMap().value("missing").toBool()); // the mark
        const QImage without = framed(s);                 // still renders, opaque, no crash
        QCOMPARE(without.size(), with.size());
        QVERIFY(without != with);
        QCOMPARE(qAlpha(without.pixel(1, 1)), 255);
        QVERIFY(env->calls("notify-send").isEmpty()); // quiet
    }

    void aWallpaperFrameIsOpaque() {
        Frame f;
        f.background = {QStringLiteral("wallpaper"), {{"path", "/nowhere.png"}}};
        QVERIFY(tarn::frameIsOpaque(f));
    }

    // ---- blurred tiles through the session ----

    void threeBlurredTilesAreOfferedAndPickingOneFramesTheShotWithIt() {
        EditorSession *s = editor().open(addShot());
        const QVariantList tiles = s->blurTiles();
        QCOMPARE(tiles.size(), 3);
        QCOMPARE(tiles.at(0).toMap().value("id").toString(), QString("light"));
        QCOMPARE(tiles.at(1).toMap().value("id").toString(), QString("heavy"));
        QCOMPARE(tiles.at(2).toMap().value("id").toString(), QString("dark"));
        s->setFrameBlur("dark");
        QCOMPARE(s->frameMap().value("background").toMap().value("style").toString(), QString("dark"));
        s->setFrameBlur("nonsense");
        QCOMPARE(s->frameMap().value("background").toMap().value("style").toString(), QString("dark"));
    }

    void thePanelsTilesComeFromTheSameRendererAsTheExport() {
        EditorSession *s = editor().open(addShot());
        const QImage tile = s->backgroundTile("blur", {{"style", "light"}}, QSize(56, 40));
        QCOMPARE(tile.size(), QSize(56, 40));
        QVERIFY(!tile.isNull());
        QVERIFY(s->backgroundTile("nonsense", {}, QSize(56, 40)).isNull());
    }

    void aPresetStoredAsAMapBringsTheBackgroundBack() {
        EditorSession *s = editor().open(addShot());
        s->setFrameGradient("orchid-veil");
        const Frame again = Frame::fromMap(s->frameMap());
        QCOMPARE(again.background.kind, QString("image-gradient"));
        QCOMPARE(again.background.params.value("name").toString(), QString("orchid-veil"));
        CaptureDocument a(shotOf(kShot)), b(shotOf(kShot));
        a.setFrame(s->document().frame());
        b.setFrame(again);
        QCOMPARE(a.flatten(), b.flatten());
    }
};

QTEST_MAIN(BackgroundsTest)
#include "backgrounds_test.moc"
