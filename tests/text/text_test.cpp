// Text capture (Ctrl+Shift+Print). The Overlay opens in text mode, area
// only; Tesseract (a stub here, never the real one) reads the Selection with the
// configured languages; lines within a paragraph are joined, paragraphs stay
// apart; the text lands on the clipboard through tarn-clip; one notification
// shows the first line, or "No text found"; History is never written.
#include "test_env.h"

#include "overlay/overlay.h"
#include "settings/settings.h"
#include "text/text_capture.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTest>

#include <cstdlib>

using tarn::Overlay;
using tarn::OverlayModel;
using tarn::Settings;
using tarn::ThumbnailStack;
using tarn::joinParagraphs;
using tarn::test::TestEnv;

namespace {
QImage gradient() {
    QImage image(128, 80, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb(x * 2, y * 3, (x ^ y) * 4));
    return image;
}
QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}
// One 128 x 80 monitor at scale 2: 64 x 40 logical pixels.
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0}])";
} // namespace

class TextTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    QString historyDir() const { return env->scratch() + "/data/tarn/history"; }
    // Drags (10,5) to (30,25) in the open Overlay (a 20 x 20 logical Selection),
    // or between the given corners.
    void drag(QPoint from = {10, 5}, QPoint to = {30, 25}) {
        model().pointerPressed(from.x(), from.y(), Qt::LeftButton);
        model().pointerMoved(to.x(), to.y());
        model().pointerReleased(to.x(), to.y(), Qt::LeftButton);
        // Tesseract runs in the background: wait until the whole read is over.
        QTRY_VERIFY(!env->daemon().service<tarn::TextCapture>().busy());
    }
    QString clipboardText() { return QString::fromUtf8(env->stdinOfCall("tarn-clip", 1)); }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setFixture("grim", "output.png", pngOf(gradient()));
        env->setPointer(10, 10);
    }
    void cleanup() { env.reset(); }

    void joinsLinesWithinAParagraphAndKeepsParagraphsApart() {
        QCOMPARE(joinParagraphs("one\ntwo\n\nthree\nfour\n\f"), QString("one two\n\nthree four"));
        QCOMPARE(joinParagraphs("a\n\n\n\n  \nb\r\nc\n"), QString("a\n\nb c"));
        QCOMPARE(joinParagraphs("  spaced   out \n line "), QString("spaced out line"));
        QCOMPARE(joinParagraphs("\n \f\n"), QString());
    }

    void ctrlShiftPrintOpensTheOverlayInTextMode() {
        QVERIFY(env->request({"capture", "text"}).ok);
        QVERIFY(model().active());
        QCOMPARE(model().mode(), OverlayModel::Text);
    }

    void theSelectionIsReadAndTheTextCopiedAndNotified() {
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QVERIFY(!model().active());

        // tesseract got the Selection's pixels (20 x 20 logical = 40 x 40 here) on
        // stdin, enlarged 2x: small code text is misread at screen size.
        QCOMPARE(env->calls("tesseract").size(), 1);
        const QImage seen = QImage::fromData(env->stdinOfCall("tesseract", 1), "PNG");
        QCOMPARE(seen.size(), QSize(80, 80));
        // smoothly scaled: the green ramp (3 per source row) starts at row 10's 30
        QVERIFY(std::abs(qGreen(seen.pixel(0, 0)) - 30) <= 3);
        QVERIFY(std::abs(qGreen(seen.pixel(0, 79)) - 3 * 49) <= 3);

        const QString text = "Tarn takes screenshots and reads text.\n\nSecond paragraph has two lines.";
        QCOMPARE(env->calls("tarn-clip").size(), 1);
        QCOMPARE(env->calls("tarn-clip").first().first(), QString("--text"));
        QCOMPARE(clipboardText(), text);
        QVERIFY(env->calls("wl-copy").isEmpty());
        // the helper keeps its own copy: nothing is left behind in the shared folder
        QVERIFY(TestEnv::filesIn(env->scratch() + "/cache/tarn/shared").isEmpty());

        const auto notes = env->calls("notify-send");
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.first().last(), QString("Tarn takes screenshots and reads text."));
    }

    void aSlowReadDoesNotBlockTheDaemon() {
        env->setMode("tesseract", "slow");
        QVERIFY(env->request({"capture", "text"}).ok);
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerMoved(30, 25);
        model().pointerReleased(30, 25, Qt::LeftButton);
        auto &text = env->daemon().service<tarn::TextCapture>();
        QVERIFY(text.busy());
        QVERIFY(env->calls("notify-send").isEmpty()); // still reading
        // The daemon answers commands meanwhile (the stub takes a second).
        QElapsedTimer clock;
        clock.start();
        QVERIFY(env->request({"settings"}).ok);
        QVERIFY(clock.elapsed() < 800);
        QTRY_VERIFY_WITH_TIMEOUT(!text.busy(), 5000);
        QCOMPARE(env->calls("notify-send").size(), 1);
        QCOMPARE(env->calls("tarn-clip").size(), 1);
    }

    void noTextSaysSoAndLeavesTheClipboardAlone() {
        env->setMode("tesseract", "empty");
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        const auto notes = env->calls("notify-send");
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.first().last(), QString("No text found"));
        QVERIFY(env->calls("tarn-clip").isEmpty());
    }

    void tesseractFailingIsOneNotificationAndNoCopy() {
        env->setMode("tesseract", "fail");
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QCOMPARE(env->calls("notify-send").size(), 1);
        QVERIFY(env->calls("tarn-clip").isEmpty());
    }

    void textCaptureNeverWritesHistoryOrTheThumbnailsOrTheLastArea() {
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QVERIFY(!QDir(historyDir()).exists() || QDir(historyDir()).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot));
        QCOMPARE(env->daemon().service<ThumbnailStack>().count(), 0);
        QVERIFY(env->calls("tarn-clip").first().first() == "--text"); // not an image copy
        // Shift+Print (repeat) still finds no saved area, so it opens the Overlay
        QVERIFY(env->request({"capture", "repeat"}).ok);
        QVERIFY(model().active());
    }

    void theLanguagesSettingDefaultsToEnglishAndChangesTheArgumentsLive() {
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QCOMPARE(env->calls("tesseract").first(), (QStringList{"stdin", "stdout", "-l", "eng"}));

        const QString config = env->scratch() + "/config/tarn/config.toml";
        QByteArray text = TestEnv::readFile(config);
        QVERIFY(text.contains("text_languages = \"eng\""));
        text.replace("text_languages = \"eng\"", "text_languages = \"eng+fra\"");
        QFile f(config);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
        f.close();
        QTRY_COMPARE(env->daemon().service<Settings>().textLanguages(), QString("eng+fra"));

        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QCOMPARE(env->calls("tesseract").size(), 2);
        QCOMPARE(env->calls("tesseract").last(), (QStringList{"stdin", "stdout", "-l", "eng+fra"}));
    }

    void aFullScreenSelectionIsEnlargedLessToStayUnderTheCap() {
        // One 2560 x 1440 monitor at scale 1: 3.7 megapixels, 14.7 at 2x.
        env->setFixture("hyprctl", "monitors.json",
                        R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":2560,"height":1440,"scale":1.0,"transform":0}])");
        QImage screen(2560, 1440, QImage::Format_RGB32);
        screen.fill(qRgb(30, 30, 40));
        env->setFixture("grim", "output.png", pngOf(screen));
        QVERIFY(env->request({"capture", "text"}).ok);
        drag({0, 0}, {2560, 1440});

        const QImage seen = QImage::fromData(env->stdinOfCall("tesseract", 1), "PNG");
        // enlarged, but by less than 2x: under 8 megapixels, same shape
        QVERIFY(seen.width() > 2560 && seen.width() < 2 * 2560);
        QVERIFY(qint64(seen.width()) * seen.height() <= 8'000'000);
        QVERIFY(qint64(seen.width()) * seen.height() > 7'900'000);
        QVERIFY(std::abs(double(seen.width()) / seen.height() - 2560.0 / 1440) < 0.01);
        // ZXingReader reads the Selection at its own size (the drag to the far
        // corner selects up to the last whole pixel: 2559 x 1439)
        QCOMPARE(QImage::fromData(env->stdinOfCall("ZXingReader", 1), "PNG").size(), QSize(2559, 1439));
    }

    void aQrCodeReplacesTheTextAndIsNotified() {
        env->setFixture("ZXingReader", "codes.txt", "hello from a code\n");
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QCOMPARE(env->calls("ZXingReader").size(), 1);
        const QImage seen = QImage::fromData(env->stdinOfCall("ZXingReader", 1), "PNG");
        QCOMPARE(seen.size(), QSize(40, 40));
        QCOMPARE(clipboardText(), QString("hello from a code"));
        const auto notes = env->calls("notify-send");
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.first().mid(notes.first().size() - 2), (QStringList{"QR code copied", "hello from a code"}));
    }

    void severalCodesAreCopiedOnePerLine() {
        env->setFixture("ZXingReader", "codes.txt", "first\nsecond\nthird\n");
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QCOMPARE(clipboardText(), QString("first\nsecond\nthird"));
        QVERIFY(env->calls("notify-send").first().contains("QR code copied"));
    }

    void aResultThatIsExactlyOneUrlIsALinkCopiedAndNothingOpensIt() {
        env->setFixture("ZXingReader", "codes.txt", "https://example.com/a?b=1\n");
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QCOMPARE(clipboardText(), QString("https://example.com/a?b=1"));
        const auto note = env->calls("notify-send").first();
        QCOMPARE(note.mid(note.size() - 2), (QStringList{"Link copied", "https://example.com/a?b=1"}));
        QVERIFY(env->calls("xdg-open").isEmpty());
    }

    void aLinkAmongOtherTextOrSeveralLinksIsNotALinkCopied() {
        env->setFixture("ZXingReader", "codes.txt", "https://a.example\nhttps://b.example\n");
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QCOMPARE(env->calls("notify-send").first().at(env->calls("notify-send").first().size() - 2), QString("QR code copied"));
    }

    void anOcrResultThatIsExactlyOneUrlIsAlsoALinkCopied() {
        env->setFixture("tesseract", "output.txt", "  https://example.com/x \n");
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QCOMPARE(clipboardText(), QString("https://example.com/x"));
        const auto note = env->calls("notify-send").first();
        QCOMPARE(note.mid(note.size() - 2), (QStringList{"Link copied", "https://example.com/x"}));
    }

    void withoutAnyCodeTextCaptureIsAsBefore() {
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QCOMPARE(env->calls("ZXingReader").size(), 1);
        QCOMPARE(env->calls("notify-send").first().last(), QString("Tarn takes screenshots and reads text."));
    }

    void withoutZXingReaderInstalledTextCaptureWorksAsBefore() {
        const QString bin = env->scratch() + "/nozxing";
        QDir().mkpath(bin);
        const QDir stubs(QString::fromLocal8Bit(qgetenv("PATH")).section(':', 0, 0));
        // Stubs find their fixtures next to themselves: copy the scripts, link the fixtures.
        for (const QFileInfo &f : stubs.entryInfoList(QDir::Files))
            if (f.fileName() != "ZXingReader")
                QFile::copy(f.absoluteFilePath(), bin + "/" + f.fileName());
        QFile::link(stubs.absoluteFilePath("fixtures"), bin + "/fixtures");
        qputenv("PATH", (bin + ":" + env->scratch() + "/sysbin").toUtf8());
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QVERIFY(env->calls("ZXingReader").isEmpty());
        QCOMPARE(env->calls("notify-send").size(), 1);
        QCOMPARE(env->calls("notify-send").first().last(), QString("Tarn takes screenshots and reads text."));
    }

    void aCodeIsReadEvenWhenTesseractFindsNothing() {
        env->setMode("tesseract", "empty");
        env->setFixture("ZXingReader", "codes.txt", "only a code\n");
        QVERIFY(env->request({"capture", "text"}).ok);
        drag();
        QCOMPARE(clipboardText(), QString("only a code"));
    }

    void aBadLanguagesLineKeepsTheLastGoodValue() {
        const QString config = env->scratch() + "/config/tarn/config.toml";
        QByteArray text = TestEnv::readFile(config);
        text.replace("text_languages = \"eng\"", "text_languages = 5");
        QFile f(config);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
        f.close();
        QTRY_COMPARE(env->calls("notify-send").size(), 1);
        QCOMPARE(env->daemon().service<Settings>().textLanguages(), QString("eng"));
    }
};

QTEST_MAIN(TextTest)
#include "text_test.moc"
