// Tarn's colors follow a Palette file any tool can write (the palette_file
// Setting), or a fixed accent (accent_color), and fall back to built-in colors
// otherwise. Asserted through the colors the core exposes (the Palette service),
// with the Settings written the way a user writes them.
#include "test_env.h"

#include "palette/palette.h"
#include "settings/settings.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

using tarn::Palette;
using tarn::test::TestEnv;

namespace {

void writeFile(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(bytes);
}

QByteArray json(QString text) { return text.replace(QLatin1Char('\''), QLatin1Char('"')).toUtf8(); }

QByteArray paletteJson(const QString &primary, const QString &surfaceLow = "#211a14") {
    return json(QString("{'primary':'%1','on_primary':'#492900','surface_container_low':'%2','surface_container_lowest':'#130d07',"
                        "'surface_container':'#251e17','surface_container_high':'#302921','surface_container_highest':'#3c332c',"
                        "'surface_bright':'#403830','on_surface':'#eee0d5','on_surface_variant':'#d5c3b5','outline':'#9d8e81',"
                        "'outline_variant':'#50453a'}").arg(primary, surfaceLow));
}

const QColor kFallbackAccent("#9ABCF2");
const QColor kFallbackAccentText("#111111");
const QColor kFallbackSurface("#181818");

} // namespace

class PaletteTest : public QObject {
    Q_OBJECT
    TestEnv *env = nullptr;

    QString configFile() const { return env->scratch() + "/config/tarn/config.toml"; }
    QString paletteFile() const { return env->scratch() + "/shell/colors.json"; }

    // The Settings file as a user writes it, then a fresh daemon so the start state is settled.
    void configure(const QString &lines) {
        writeFile(configFile(), lines.toUtf8());
        env->restartDaemon();
    }
    void usePaletteFile() { configure(QString("palette_file = \"%1\"\n").arg(paletteFile())); }
    Palette &palette() { return env->daemon().service<Palette>(); }

private slots:
    void init() { env = new TestEnv; }
    void cleanup() {
        delete env;
        env = nullptr;
    }

    // ---- the Palette file ----

    void withNoPaletteFileTheBuiltInFallbackApplies() {
        auto &p = palette();
        QVERIFY(!p.fromFile());
        QCOMPARE(p.accent(), kFallbackAccent);
        QCOMPARE(p.accentText(), kFallbackAccentText);
        QCOMPARE(p.surface(), kFallbackSurface);
        QCOMPARE(p.text(), QColor("#F2F2F2"));
    }

    void aPaletteFileSuppliesTheAccentAndSurfaces() {
        writeFile(paletteFile(), paletteJson("#fbb974"));
        usePaletteFile();
        auto &p = palette();
        QVERIFY(p.fromFile());
        QCOMPARE(p.accent(), QColor("#fbb974"));
        QCOMPARE(p.accentText(), QColor("#492900"));
        QCOMPARE(p.surface(), QColor("#211a14"));
        QCOMPARE(p.control(), QColor("#302921"));
        QCOMPARE(p.text(), QColor("#eee0d5"));
    }

    void aPartlyFilledFileKeepsWhatItHasKeyByKey() {
        writeFile(paletteFile(), json("{'primary':'#fbb974','surface_container_low':'not a color','on_surface':42,'outline':'#9d8e81'}"));
        usePaletteFile();
        auto &p = palette();
        QVERIFY(p.fromFile());
        QCOMPARE(p.accent(), QColor("#fbb974"));
        QCOMPARE(p.muted(), QColor("#9d8e81"));
        QCOMPARE(p.surface(), kFallbackSurface);
        QCOMPARE(p.text(), QColor("#F2F2F2"));
        QCOMPARE(p.accentText(), kFallbackAccentText);
    }

    void anUnreadableFileGivesTheBuiltInColors() {
        writeFile(paletteFile(), "{ this is not json");
        usePaletteFile();
        auto &p = palette();
        QVERIFY(!p.fromFile());
        QCOMPARE(p.accent(), kFallbackAccent);
        QCOMPARE(p.surface(), kFallbackSurface);
    }

    void aMissingEmptyOrWrongKindOfFileGivesTheBuiltInColors() {
        usePaletteFile(); // the Setting points at a file that is not there
        QVERIFY(!palette().fromFile());
        QCOMPARE(palette().accent(), kFallbackAccent);
        writeFile(paletteFile(), "[1, 2, 3]");
        env->restartDaemon();
        QVERIFY(!palette().fromFile());
        writeFile(paletteFile(), "");
        env->restartDaemon();
        QCOMPARE(palette().accent(), kFallbackAccent);
    }

    void aFileAppearingAfterTarnStartedIsPickedUp() {
        usePaletteFile();
        auto &p = palette();
        QCOMPARE(p.accent(), kFallbackAccent);
        writeFile(paletteFile(), paletteJson("#fbb974"));
        QTRY_COMPARE(p.accent(), QColor("#fbb974"));
        QVERIFY(p.fromFile());
    }

    void aChangedFileRecolorsLiveWithoutARestart() {
        writeFile(paletteFile(), paletteJson("#fbb974"));
        usePaletteFile();
        auto &p = palette();
        QSignalSpy changed(&p, &Palette::changed);
        QCOMPARE(p.accent(), QColor("#fbb974"));
        writeFile(paletteFile(), paletteJson("#4fa3ff", "#101820"));
        QTRY_COMPARE(p.accent(), QColor("#4fa3ff"));
        QCOMPARE(p.surface(), QColor("#101820"));
        QVERIFY(changed.count() >= 1);
    }

    void aToolReplacingTheFileByRenameIsStillSeen() {
        writeFile(paletteFile(), paletteJson("#fbb974"));
        usePaletteFile();
        auto &p = palette();
        for (const char *accent : {"#4fa3ff", "#33cc66"}) { // twice: the watch must survive each swap
            writeFile(paletteFile() + ".tmp", paletteJson(accent));
            QFile::remove(paletteFile());
            QVERIFY(QFile::rename(paletteFile() + ".tmp", paletteFile()));
            QTRY_COMPARE(p.accent(), QColor(accent));
        }
    }

    void aFileBrokenWhileInUseReturnsToTheBuiltInColors() {
        writeFile(paletteFile(), paletteJson("#fbb974"));
        usePaletteFile();
        auto &p = palette();
        QCOMPARE(p.accent(), QColor("#fbb974"));
        writeFile(paletteFile(), "garbage");
        QTRY_COMPARE(p.accent(), kFallbackAccent);
        QVERIFY(!p.fromFile());
    }

    void changingTheSettingSwitchesFilesWithoutARestart() {
        writeFile(paletteFile(), paletteJson("#fbb974"));
        writeFile(env->scratch() + "/shell/other.json", paletteJson("#33cc66"));
        usePaletteFile();
        auto &p = palette();
        QCOMPARE(p.accent(), QColor("#fbb974"));
        writeFile(configFile(), QString("palette_file = \"%1/shell/other.json\"\n").arg(env->scratch()).toUtf8());
        QTRY_COMPARE(p.accent(), QColor("#33cc66"));
        writeFile(configFile(), "palette_file = \"\"\n");
        QTRY_COMPARE(p.accent(), kFallbackAccent);
        QVERIFY(!p.fromFile());
    }

    void theAccentsJsonFileBesideThePaletteIsNoLongerRead() {
        writeFile(paletteFile(), paletteJson("#fbb974"));
        writeFile(env->scratch() + "/shell/accents.json", json("{'result':{'roles':{'primary':'#F79800','onAccent':'#111111'}}}"));
        usePaletteFile();
        QCOMPARE(palette().accent(), QColor("#fbb974"));
        QCOMPARE(palette().accentText(), QColor("#492900"));
    }

    // ---- the accent, in every combination ----
    // Order: the accent_color Setting, then the file's accent, then its primary, then built-in.

    void accentOrderSettingBeatsFileAccentBeatsPrimaryBeatsBuiltIn_data() {
        QTest::addColumn<bool>("setting");
        QTest::addColumn<bool>("fileAccent");
        QTest::addColumn<bool>("filePrimary");
        QTest::addColumn<QString>("expected");
        const QString setting = "#aa0000", fileAccent = "#00aa00", primary = "#0000aa", builtIn = "#9abcf2";
        for (int mask = 0; mask < 8; ++mask) {
            const bool s = mask & 1, a = mask & 2, p = mask & 4;
            QTest::newRow(qPrintable(QString("setting=%1 accent=%2 primary=%3").arg(s).arg(a).arg(p)))
                << s << a << p << (s ? setting : a ? fileAccent : p ? primary : builtIn);
        }
    }
    void accentOrderSettingBeatsFileAccentBeatsPrimaryBeatsBuiltIn() {
        QFETCH(bool, setting);
        QFETCH(bool, fileAccent);
        QFETCH(bool, filePrimary);
        QFETCH(QString, expected);
        QString body = "{'surface_container_low':'#211a14'";
        if (fileAccent)
            body += ",'accent':'#00aa00'";
        if (filePrimary)
            body += ",'primary':'#0000aa'";
        writeFile(paletteFile(), json(body + "}"));
        QString config = QString("palette_file = \"%1\"\n").arg(paletteFile());
        if (setting)
            config += "accent_color = \"#aa0000\"\n";
        configure(config);
        QCOMPARE(palette().accent(), QColor(expected));
    }

    void onAccentBeatsOnPrimaryBeatsBuiltIn() {
        writeFile(paletteFile(), json("{'primary':'#0000aa','on_primary':'#eeeeee','on_accent':'#101010'}"));
        usePaletteFile();
        QCOMPARE(palette().accentText(), QColor("#101010"));
        writeFile(paletteFile(), json("{'primary':'#0000aa','on_primary':'#eeeeee'}"));
        QTRY_COMPARE(palette().accentText(), QColor("#eeeeee"));
        writeFile(paletteFile(), json("{'primary':'#0000aa'}"));
        QTRY_COMPARE(palette().accentText(), kFallbackAccentText);
    }

    void theAccentSettingAloneColorsTheAccentWithNoPaletteFile() {
        configure("accent_color = \"#aa0000\"\n");
        QCOMPARE(palette().accent(), QColor("#aa0000"));
        QCOMPARE(palette().surface(), kFallbackSurface);
        QVERIFY(!palette().fromFile());
    }

    void editingTheAccentSettingRecolorsLive() {
        writeFile(paletteFile(), paletteJson("#fbb974"));
        usePaletteFile();
        auto &p = palette();
        writeFile(configFile(), QString("palette_file = \"%1\"\naccent_color = \"#aa0000\"\n").arg(paletteFile()).toUtf8());
        QTRY_COMPARE(p.accent(), QColor("#aa0000"));
        QCOMPARE(p.surface(), QColor("#211a14")); // the rest still follows the file
        writeFile(configFile(), QString("palette_file = \"%1\"\naccent_color = \"\"\n").arg(paletteFile()).toUtf8());
        QTRY_COMPARE(p.accent(), QColor("#fbb974"));
    }

    void aBadAccentSettingIsToldAndKeepsTheLastGoodColor() {
        configure("accent_color = \"#aa0000\"\n");
        QCOMPARE(palette().accent(), QColor("#aa0000"));
        writeFile(configFile(), "accent_color = \"red\"\n");
        QTRY_VERIFY(!env->calls("notify-send").isEmpty());
        QCOMPARE(palette().accent(), QColor("#aa0000"));
    }

    // ---- the first-run Settings file ----

    void theFirstRunTemplateOffersBothKeysEmptyWithComments() {
        const QString text = QString::fromUtf8(TestEnv::readFile(configFile()));
        QVERIFY2(text.contains("palette_file = \"\"\n"), qPrintable(text));
        QVERIFY2(text.contains("accent_color = \"\"\n"), qPrintable(text));
        const int pf = text.indexOf("palette_file");
        QVERIFY(text.lastIndexOf("# ", pf) > text.lastIndexOf("\n\n", pf)); // a comment sits right above it
        const int ac = text.indexOf("accent_color");
        QVERIFY(text.lastIndexOf("# ", ac) > text.lastIndexOf("\n\n", ac));
        QVERIFY(text.contains("#rrggbb"));
    }
};

QTEST_MAIN(PaletteTest)
#include "palette_test.moc"
