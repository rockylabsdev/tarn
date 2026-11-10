// The File name pattern and the Image format Settings. The pure naming rules
// first (every token, empty-token collapse, cleaning, the 60-character cut, the
// trailing app name), then what Save, auto-save, Save as, Copy and kept Recordings
// really write, driven through the daemon with `hyprctl clients` fixtures.
#include "test_env.h"

#include "clipboard/clipboard.h"
#include "history/history.h"
#include "history_window/history_model.h"
#include "history_window/history_window.h"
#include "hyprland/hyprland_bridge.h"
#include "pin/pins.h"
#include "recording/recording_session.h"
#include "settings/settings.h"
#include "storage/file_name.h"
#include "thumbnails/thumbnail_stack.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using tarn::CaptureName;
using tarn::fileBaseName;
using tarn::test::TestEnv;

namespace {
const QDateTime kWhen(QDate(2026, 11, 2), QTime(14, 3, 22));
const QByteArray kMonitor =
    R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":2560,"height":1440,"scale":1.0,"transform":0,"activeWorkspace":{"id":1},"specialWorkspace":{"id":0},"focused":true}])";

QJsonObject client(const QString &app, const QString &title) {
    return {{"address", "0x1"}, {"mapped", true}, {"hidden", false}, {"workspace", QJsonObject{{"id", 1}}},
            {"at", QJsonArray{0, 0}}, {"size", QJsonArray{2560, 1440}}, {"fullscreen", 0}, {"floating", false},
            {"class", app}, {"title", title}, {"focusHistoryID", 0}};
}

QString name(const QString &pattern, const CaptureName &n = {}) { return fileBaseName(pattern, kWhen, n); }
} // namespace

class NamingTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    tarn::ThumbnailStack &stack() { return env->daemon().service<tarn::ThumbnailStack>(); }
    QString configFile() const { return env->scratch() + "/config/tarn/config.toml"; }
    QString shared() const { return env->scratch() + "/cache/tarn/shared"; }
    // Edits one line like the user would, and waits until the daemon applied it.
    void applyConfig(const QByteArray &key, const QByteArray &value) {
        QList<QByteArray> lines = TestEnv::readFile(configFile()).split('\n');
        for (QByteArray &line : lines)
            if (line.startsWith(key + " = "))
                line = key + " = " + value;
        QFile f(configFile());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(lines.join('\n'));
        f.close();
        QTest::qWait(250); // the watcher's debounce
    }
    void setWindow(const QString &app, const QString &title) {
        env->setFixture("hyprctl", "clients.json", QJsonDocument(QJsonArray{client(app, title)}).toJson());
        env->setFixture("hyprctl", "activewindow.json", QJsonDocument(client(app, title)).toJson());
    }
    int shoot() {
        if (!env->request({"capture", "fullscreen"}).ok)
            return -1;
        return stack().ids().last();
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setNow(kWhen);
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->daemon().service<tarn::Settings>(); // writes the file
    }
    void cleanup() { env.reset(); }

    // ---- the pure rules ----

    void everyToken() {
        const CaptureName n{"window", "Firefox", "Rust Docs"};
        QCOMPARE(name("{date}", n), QString("2026-11-02"));
        QCOMPARE(name("{time}", n), QString("14-03-22"));
        QCOMPARE(name("{app}", n), QString("firefox")); // lowercased
        QCOMPARE(name("{title}", n), QString("Rust-Docs")); // case kept
        QCOMPARE(name("{kind}", n), QString("window"));
        QCOMPARE(name("{kind}-{date}", {"screen", "", ""}), QString("screen-2026-11-02"));
    }
    void theDefaultPatternIsDateTimeApp() {
        QCOMPARE(tarn::defaultFileNamePattern(), QString("{date}_{time}_{app}"));
        QCOMPARE(name("{date}_{time}_{app}", {"area", "kitty", "x"}), QString("2026-11-02_14-03-22_kitty"));
    }
    void anEmptyTokenDropsWithItsSeparator() {
        QCOMPARE(name("{date}_{time}_{app}", {"area", "", ""}), QString("2026-11-02_14-03-22"));
        QCOMPARE(name("{app}_{date}", {"area", "", ""}), QString("2026-11-02"));
        QCOMPARE(name("{date}_{title}_{time}", {"area", "kitty", ""}), QString("2026-11-02_14-03-22"));
        QCOMPARE(name("{app} - {title}", {"area", "kitty", ""}), QString("kitty"));
    }
    void cleaningKeepsLettersDigitsAndDotDashUnderscore() {
        QCOMPARE(name("{title}", {"", "", "a/b:c*d?\"e<f>g|h"}), QString("a-b-c-d-e-f-g-h"));
        QCOMPARE(name("{title}", {"", "", "  --Hello,   World!  "}), QString("Hello-World"));
        QCOMPARE(name("{title}", {"", "", "v1.2_final-draft"}), QString("v1.2_final-draft"));
        QCOMPARE(name("{title}", {"", "", QString::fromUtf8("日本語のタイトル Привет")}), QString::fromUtf8("日本語のタイトル-Привет"));
    }
    void aTitleIsCutAtSixtyOnAWordBoundary() {
        const QString title = "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi";
        QCOMPARE(name("{title}", {"", "", title}), QString("alpha-beta-gamma-delta-epsilon-zeta-eta-theta-iota-kappa"));
        QCOMPARE(name("{title}", {"", "", QString(80, 'x')}), QString(60, 'x')); // no boundary: hard cut
        QCOMPARE(name("{title}", {"", "", QString(60, 'x')}), QString(60, 'x'));
    }
    void aTrailingAppNameIsDropped() {
        QCOMPARE(name("{title}", {"window", "firefox", QString::fromUtf8("Issue 72 · GitHub — Mozilla Firefox")}), QString("Issue-72-GitHub"));
        QCOMPARE(name("{title}", {"window", "firefox", "Docs - Firefox"}), QString("Docs"));
        QCOMPARE(name("{title}", {"window", "Code", "main.cpp | Code"}), QString("main.cpp"));
        QCOMPARE(name("{title}", {"window", "google-chrome", "Tab - Google Chrome"}), QString("Tab"));
        QCOMPARE(name("{title}", {"window", "firefox", "Docs - Something Else"}), QString("Docs-Something-Else")); // not the app
        QCOMPARE(name("{title}", {"window", "firefox", "Firefox"}), QString("Firefox")); // nothing before it: kept
    }

    void badPatternsAreRefused() {
        QString why;
        QVERIFY(tarn::checkFileNamePattern("{date}_{time}_{app}", &why));
        QVERIFY(tarn::checkFileNamePattern("shot-{kind}", &why));
        QVERIFY(!tarn::checkFileNamePattern("{date}_{color}", &why));
        QVERIFY(why.contains("color"));
        QVERIFY(!tarn::checkFileNamePattern("a/{date}", &why));
        QVERIFY(why.contains("/"));
        QVERIFY(!tarn::checkFileNamePattern("{date", &why));
        QVERIFY(!tarn::checkFileNamePattern("", &why));
    }

    // ---- Settings ----

    void aNewSettingsFileHasBothKeysWithComments() {
        const QString text = QString::fromUtf8(TestEnv::readFile(configFile()));
        for (const char *key : {"file_name", "image_format"}) {
            const int at = text.indexOf(QString("\n%1 = ").arg(key));
            QVERIFY2(at > 0, key);
            QVERIFY2(text.left(at).trimmed().split('\n').last().startsWith('#'), key);
        }
        QVERIFY(text.contains("file_name = \"{date}_{time}_{app}\""));
        QVERIFY(text.contains("image_format = \"png\""));
    }
    void badValuesGetAPerLineErrorAndTheDefault() {
        auto &settings = env->daemon().service<tarn::Settings>();
        applyConfig("file_name", "\"{date}_{color}\"");
        QCOMPARE(env->calls("notify-send").size(), 1);
        QVERIFY(env->calls("notify-send").first().join(' ').contains("file_name"));
        QCOMPARE(settings.fileNamePattern(), QString("{date}_{time}_{app}"));
        applyConfig("file_name", "\"a/{date}\"");
        QCOMPARE(env->calls("notify-send").size(), 2);
        QCOMPARE(settings.fileNamePattern(), QString("{date}_{time}_{app}"));
        applyConfig("file_name", "\"{date}_{time}_{app}\"");
        applyConfig("image_format", "\"bmp\"");
        QCOMPARE(env->calls("notify-send").size(), 3);
        QVERIFY(env->calls("notify-send").last().join(' ').contains("image_format"));
        QCOMPARE(settings.imageFormat(), QString("png"));
    }

    // ---- what is written ----

    void saveFollowsThePatternOnAFullScreenShotWithAWindow() {
        setWindow("Firefox", QString::fromUtf8("Issue 72 · GitHub — Mozilla Firefox"));
        const int id = shoot();
        QCOMPARE(stack().save(id), env->screenshots() + "/2026-11-02_14-03-22_firefox.png");
    }
    void aShotOverBareDesktopHasNoSuffix() {
        const int id = shoot();
        QCOMPARE(stack().save(id), env->screenshots() + "/2026-11-02_14-03-22.png");
    }
    void aCustomPatternNamesSaveAutoSaveSaveAsAndCopy() {
        applyConfig("file_name", "\"{kind}_{title}\"");
        applyConfig("save_automatically", "true");
        setWindow("firefox", QString::fromUtf8("Issue 72 · GitHub — Mozilla Firefox"));
        const int id = shoot();
        QTRY_COMPARE(TestEnv::filesIn(env->screenshots()), QStringList{"screen_Issue-72-GitHub.png"}); // auto-save, once the PNG is made
        QCOMPARE(stack().saveAsSuggestion(id), env->screenshots() + "/screen_Issue-72-GitHub.png");
        QVERIFY(stack().copy(id));
        QCOMPARE(TestEnv::filesIn(shared()), QStringList{"screen_Issue-72-GitHub.png"});
    }
    void historySaveAsSuggestsThePatternNameAndTheFormatsFilter() {
        applyConfig("file_name", "\"{kind}_{title}\"");
        applyConfig("image_format", "\"jpeg\"");
        setWindow("firefox", "Issue 72");
        shoot();
        auto &history = env->daemon().service<tarn::History>();
        history.flush();
        auto &window = env->daemon().service<tarn::HistoryWindow>();
        window.open();
        auto &model = window.model();
        model.select(history.ids().last());
        QCOMPARE(model.saveAsSuggestion(), model.saveAsFolder() + "/screen_Issue-72.jpg");
        QCOMPARE(model.saveAsFilterIndex(), 1);
    }
    void clashesGetUnderscoreTwoAndThree() {
        applyConfig("file_name", "\"{kind}\"");
        const int a = shoot(), b = shoot(), c = shoot();
        for (int id : {a, b, c})
            QVERIFY(!stack().save(id).isEmpty());
        QCOMPARE(TestEnv::filesIn(env->screenshots()), (QStringList{"screen.png", "screen_2.png", "screen_3.png"}));
    }
    void historyKeepsItsOwnIds() {
        applyConfig("file_name", "\"{kind}\"");
        shoot();
        env->daemon().service<tarn::History>().flush();
        QCOMPARE(TestEnv::filesIn(env->scratch() + "/data/tarn/history"),
                 (QStringList{"2026-11-02_14-03-22.json", "2026-11-02_14-03-22.png"}));
    }

    void aKeptRecordingFollowsThePatternWithTheWindowUnderItsArea() {
        applyConfig("file_name", "\"{kind}_{app}_{date}\"");
        setWindow("Firefox", "Docs");
        env->daemon().service<tarn::HyprlandBridge>().takeSnapshot();
        auto &session = env->daemon().service<tarn::RecordingSession>();
        QVERIFY(session.start(QRect(10, 10, 200, 100), QRect(0, 0, 2560, 1440), 1.0, "DP-1").ok);
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), tarn::RecordingSession::Recording, 5000);
        QCOMPARE(TestEnv::filesIn(env->keptRecordings()), QStringList{"recording_firefox_2026-11-02.mp4"});
        QVERIFY(env->request({"capture", "record"}).ok); // stop
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), tarn::RecordingSession::Done, 10000);
        const QString saved = stack().save(stack().ids().last());
        QCOMPARE(saved, env->recordings() + "/recording_firefox_2026-11-02.mp4");
    }

    void aGifPinsSaveAsSuggestsTheRecordingsName() {
        applyConfig("file_name", "\"{kind}_{app}_{date}\"");
        setWindow("Firefox", "Docs");
        env->daemon().service<tarn::HyprlandBridge>().takeSnapshot();
        auto &session = env->daemon().service<tarn::RecordingSession>();
        QVERIFY(session.start(QRect(10, 10, 200, 100), QRect(0, 0, 2560, 1440), 1.0, "DP-1", tarn::RecordingSession::Gif).ok);
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), tarn::RecordingSession::Recording, 5000);
        QVERIFY(env->request({"capture", "record"}).ok); // stop
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), tarn::RecordingSession::Done, 15000);
        auto &pins = env->daemon().service<tarn::Pins>();
        const int pin = pins.pin(stack().ids().last());
        QVERIFY(pin >= 0);
        QCOMPARE(pins.saveAsSuggestion(pin), pins.saveAsFolder() + "/recording_firefox_2026-11-02.gif");
    }

    void imageFormatChangesSaveAutoSaveAndSaveAs() {
        applyConfig("image_format", "\"jpeg\"");
        const int id = shoot();
        QCOMPARE(stack().saveAsFilterIndex(), 1);
        QVERIFY(stack().saveAsSuggestion(id).endsWith("2026-11-02_14-03-22.jpg"));
        const QString path = stack().save(id);
        QCOMPARE(path, env->screenshots() + "/2026-11-02_14-03-22.jpg");
        QCOMPARE(QImageReader(path).format().toLower(), QByteArray("jpeg"));

        applyConfig("image_format", "\"webp\"");
        QCOMPARE(stack().saveAsFilterIndex(), 2);
        applyConfig("save_automatically", "true");
        shoot();
        const QString webp = env->screenshots() + "/2026-11-02_14-03-22.webp";
        if (QImageWriter::supportedImageFormats().contains("webp")) {
            QTRY_VERIFY(QFile::exists(webp)); // once the PNG is made
            QCOMPARE(QImageReader(webp).format().toLower(), QByteArray("webp"));
        } else { // no Qt plugin on this machine: nothing half-written, and the user is told
            QTRY_COMPARE(env->calls("notify-send").size(), 1); // once the PNG is made
            QVERIFY(!QFile::exists(webp));
        }
        applyConfig("image_format", "\"png\"");
        QCOMPARE(stack().saveAsFilterIndex(), 0);
    }
    void copyAndHistoryStayPngWhateverTheFormat() {
        applyConfig("image_format", "\"jpeg\"");
        const int id = shoot();
        QVERIFY(stack().copy(id));
        QCOMPARE(TestEnv::filesIn(shared()), QStringList{"2026-11-02_14-03-22.png"});
        env->daemon().service<tarn::History>().flush();
        const QString original = env->scratch() + "/data/tarn/history/2026-11-02_14-03-22.png";
        QVERIFY(QFile::exists(original));
        QCOMPARE(QImageReader(original).format().toLower(), QByteArray("png"));
    }
};

QTEST_MAIN(NamingTest)
#include "naming_test.moc"
