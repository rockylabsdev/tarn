// Saved data from before the American spelling. state.toml, Project files and
// presets.toml written by an earlier Tarn (the fixtures: British key names such as
// `colour` and `centre`) must still open with everything in them, and what Tarn
// writes now must use the American names only.
//
// Asserted on what a user sees: the saved colors and tool color after a restart,
// the file text after a save, the Project file Tarn writes after reopening an old
// one, and the Preset a Frame is read from.
#include "test_env.h"

#include "document/capture_document.h"
#include "document/project_file.h"
#include "editor/editor.h"
#include "history/history.h"
#include "preset/preset_store.h"
#include "saved_colors/saved_colors.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using tarn::Editor;
using tarn::EditorSession;
using tarn::History;
using tarn::SavedColors;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

namespace {

QByteArray fixture(const char *name) { return TestEnv::readFile(QStringLiteral(TARN_SAVED_DATA_FIXTURES "/") + QLatin1String(name)); }

void writeFile(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        qFatal("cannot write %s", qPrintable(path));
    f.write(bytes);
}

QByteArray whiteShot() {
    QImage shot(96, 64, QImage::Format_RGB32);
    shot.fill(Qt::white);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    shot.save(&buffer, "PNG");
    return png;
}

} // namespace

class SavedDataTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    SavedColors &saved() { return env->daemon().service<SavedColors>(); }
    History &history() { return env->daemon().service<History>(); }
    Editor &editor() { return env->daemon().service<Editor>(); }
    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    tarn::PresetStore &presets() { return env->daemon().service<tarn::PresetStore>(); }
    QString stateFile() const { return env->scratch() + "/state/tarn/state.toml"; }
    QString stateText() const { return QString::fromUtf8(TestEnv::readFile(stateFile())); }

    void startWithOldState() {
        writeFile(stateFile(), fixture("state-before-american-spelling.toml"));
        env->restartDaemon();
    }

private slots:
    void init() { env = std::make_unique<TestEnv>(); }
    void cleanup() { env.reset(); }

    // ---- state.toml ----

    void anOldStateFileKeepsItsSavedColorsAfterASave() {
        startWithOldState();
        QCOMPARE(saved().colors(), (QList<QColor>{QColor(0x2d, 0x6c, 0xdf), QColor(255, 0, 0, 0x80)}));
        saved().save(QColor(10, 20, 30));
        env->restartDaemon();
        QCOMPARE(saved().colors(), (QList<QColor>{QColor(10, 20, 30), QColor(0x2d, 0x6c, 0xdf), QColor(255, 0, 0, 0x80)}));
        const QString text = stateText();
        QVERIFY2(text.contains("[colors]"), qPrintable(text));
        QVERIFY2(!text.contains("colours"), qPrintable(text));
    }

    void anOldStateFileKeepsTheToolColorAndWritesTheNewName() {
        startWithOldState();
        EditorSession *s = editor().open(stack().add(whiteShot(), env->daemon().now()));
        QCOMPARE(s->color(), QColor(0xe5, 0x39, 0x35));
        saved().save(QColor(1, 2, 3)); // any save writes the whole file
        const QString text = stateText();
        QVERIFY2(text.contains("color = \"#ffe53935\""), qPrintable(text));
        QVERIFY2(!text.contains("colour"), qPrintable(text));
        QVERIFY2(text.contains("line = 3"), qPrintable(text)); // the rest is untouched
    }

    void aNewStateFileWinsOverAnOldKeyLeftBeside() {
        writeFile(stateFile(), "[colors]\nsaved = \"#ff010203\"\n\n[colours]\nsaved = \"#ff040506\"\n");
        env->restartDaemon();
        QCOMPARE(saved().colors(), (QList<QColor>{QColor(1, 2, 3)}));
    }

    // ---- the Project file ----

    void anOldProjectFileReopensWithItsColorsAndPositions() {
        const int thumb = stack().add(whiteShot(), env->daemon().now());
        const QString historyId = stack().historyId(thumb);
        history().flush();
        writeFile(history().projectPath(historyId), fixture("project-before-american-spelling.edit.json"));

        auto loaded = history().loadProject(historyId, QImage(history().pngPath(historyId)));
        QCOMPARE(int(loaded.state), int(History::ProjectState::Loaded));
        const QJsonObject root = QJsonDocument::fromJson(tarn::projectToJson(*loaded.document)).object();
        const QByteArray written = QJsonDocument(root).toJson(QJsonDocument::Compact);
        QVERIFY2(!written.contains("colour") && !written.contains("centre"), written.constData());

        const QJsonArray annotations = root.value("annotations").toArray();
        QCOMPARE(annotations.size(), 3);
        auto data = [&](int i) { return annotations.at(i).toObject().value("data").toObject(); };
        QCOMPARE(data(0).value("style").toObject().value("color").toString(), QString("#ffe53935")); // the rectangle
        QCOMPARE(data(1).value("color").toString(), QString("#ff30d158"));                           // the text
        QCOMPARE(data(2).value("style").toObject().value("color").toString(), QString("#ff0a84ff")); // the counter
        QCOMPARE(data(2).value("center").toObject().value("x").toDouble(), 70.0);
        QCOMPARE(data(2).value("center").toObject().value("y").toDouble(), 20.0);

        const QJsonObject frame = root.value("frame").toObject();
        QCOMPARE(frame.value("background").toObject().value("color").toString(), QString("#ff2d6cdf"));
        QCOMPARE(frame.value("ah").toInt(), -1); // the alignment survives
        QCOMPARE(frame.value("av").toInt(), 0);
    }

    // ---- presets.toml ----

    void anOldPresetKeepsItsColorsAndPresetsWrittenNowUseTheNewName() {
        writeFile(env->scratch() + "/config/tarn/presets.toml", fixture("presets-before-american-spelling.toml"));
        const auto plain = presets().preset("Plain");
        QVERIFY(plain.has_value());
        QCOMPARE(plain->background.params.value("color").toString(), QString("#ff112233"));
        const auto stops = presets().preset("Stops");
        QVERIFY(stops.has_value());
        const QVariantList list = stops->background.params.value("stops").toList();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).toMap().value("color").toString(), QString("#ffff0000"));
        QCOMPARE(list.at(1).toMap().value("pos").toDouble(), 0.25);

        QVERIFY(presets().save("Fresh", *plain));
        const QString text = QString::fromUtf8(TestEnv::readFile(env->scratch() + "/config/tarn/presets.toml"));
        const QString fresh = text.mid(text.indexOf("[preset.\"Fresh\"]"));
        QVERIFY2(fresh.contains("color = \"#ff112233\""), qPrintable(fresh));
        QVERIFY2(!fresh.contains("colour"), qPrintable(fresh));
    }
};

QTEST_MAIN(SavedDataTest)
#include "saved_data_test.moc"
