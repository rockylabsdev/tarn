// Text layout. K in Text Mode cycles Paragraphs, Lines, Table; the choice
// is remembered (also across a restart). Lines and Table both ask Tesseract for
// `--psm 6 tsv` and rebuild from the word boxes: Lines keeps every line break
// and each line's indent, with a blank line only for a real gap; Table
// copies tab-separated cells, or falls back to Lines with a notice.
// Tesseract is a stub with canned word boxes (tests/support/stubs/fixtures/tesseract).
#include "test_env.h"

#include "overlay/overlay.h"
#include "state/remembered_state.h"
#include "text/text_capture.h"
#include "text/text_layout.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTest>

using tarn::Overlay;
using tarn::OverlayModel;
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
QByteArray stubFixture(const char *name) {
    const QString dir = QString::fromUtf8(TARN_STUB_DIR) + "/fixtures/tesseract/";
    QFile f(dir + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
// The same word boxes as Tesseract reports them for the Selection enlarged
// 2x: left, top, width and height doubled.
QByteArray doubled(const QByteArray &tsv) {
    QByteArrayList out;
    for (const QByteArray &line : tsv.split('\n')) {
        QByteArrayList f = line.split('\t');
        if (f.size() >= 12 && f.at(0) != "level")
            for (int i = 6; i <= 9; ++i)
                f[i] = QByteArray::number(f.at(i).toInt() * 2);
        out << f.join('\t');
    }
    return out.join('\n');
}
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0}])";
const QByteArray kTsvHeader = "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n";

// tsv-prose.txt as Lines: five plain lines.
const char *const kProseLines = "Tarn reads the words on the screen and puts them\n"
                                "on the clipboard so that nothing has to be typed\n"
                                "out again by hand, which saves a great deal of\n"
                                "time when a document cannot be selected at all\n"
                                "or when the text sits inside a picture or video";

// Word boxes for monospaced text as Tesseract draws them: 10 px a character,
// the box tight to the ink (1 px in from each side). `line` is the row on a
// 24 px pitch; `column` counts characters from the text's left edge at x = 20.
// Words like `}` and `{};` reach above and below the letters; `=` and `,`
// sit lower and are shorter.
QByteArray monoWord(int line, int column, const QByteArray &text) {
    int top = 30 + line * 24, height = 16;
    if (text.contains('}') || text.contains('{') || text.contains('(') || text.contains(';')) {
        top -= 2;
        height = 20;
    } else if (text == "=" || text == ",") {
        top += 6;
        height = 6;
    }
    return "5\t1\t1\t1\t1\t1\t" + QByteArray::number(20 + column * 10 + 1) + "\t" + QByteArray::number(top) + "\t" +
           QByteArray::number(text.size() * 10 - 2) + "\t" + QByteArray::number(height) + "\t90\t" + text + "\n";
}
// One line of code at `line`: its words split on spaces, columns kept.
QByteArray monoLine(int line, const QByteArray &code) {
    QByteArray out;
    int column = 0;
    for (const QByteArray &word : code.split(' ')) {
        if (!word.isEmpty())
            out += monoWord(line, column, word);
        column += word.size() + 1;
    }
    return out;
}
QByteArray monoCode(const QList<QByteArray> &lines) {
    QByteArray tsv = kTsvHeader;
    for (int i = 0; i < lines.size(); ++i)
        if (!lines.at(i).isEmpty())
            tsv += monoLine(i, lines.at(i));
    return tsv;
}
} // namespace

class TextLayoutTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    QString statePath() const { return env->scratch() + "/state/tarn/state.toml"; }
    QString stateText() const { return QString::fromUtf8(TestEnv::readFile(statePath())); }
    void drag() {
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerMoved(30, 25);
        model().pointerReleased(30, 25, Qt::LeftButton);
        QTRY_VERIFY(!env->daemon().service<tarn::TextCapture>().busy());
    }
    QString clipboardText() { return QString::fromUtf8(env->stdinOfCall("tarn-clip", 1)); }
    QStringList notes() {
        QStringList out;
        for (const auto &call : env->calls("notify-send"))
            out << call.mid(call.indexOf("--") + 1).join(" | ");
        return out;
    }
    // The tesseract calls that asked for tsv.
    int tsvCalls() {
        int n = 0;
        for (const auto &call : env->calls("tesseract"))
            n += call.contains("tsv");
        return n;
    }
    // A Text capture in `presses` Ks from Paragraphs: 0 Paragraphs, 1 Lines, 2 Table.
    void textCapture(int presses, const char *tsvFixture = nullptr) {
        if (tsvFixture)
            env->setFixture("tesseract", "tsv.txt", stubFixture(tsvFixture));
        QVERIFY(env->request({"capture", "text"}).ok);
        for (int i = 0; i < presses; ++i)
            model().keyPressed(Qt::Key_K);
        drag();
    }
    void rememberLayout(const QString &word) {
        QDir().mkpath(QFileInfo(statePath()).absolutePath());
        QFile f(statePath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(("[text]\nlayout = \"" + word + "\"\n").toUtf8());
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setFixture("grim", "output.png", pngOf(gradient()));
        env->setPointer(10, 10);
    }
    void cleanup() { env.reset(); }

    // ---- K and the button ----
    void kCyclesParagraphsLinesTableInTextMode() {
        QVERIFY(env->request({"capture", "text"}).ok);
        QCOMPARE(model().textLayoutLabel(), QString("Paragraphs"));
        model().keyPressed(Qt::Key_K);
        QCOMPARE(model().textLayoutLabel(), QString("Lines"));
        model().keyPressed(Qt::Key_K);
        QCOMPARE(model().textLayoutLabel(), QString("Table"));
        model().keyPressed(Qt::Key_K);
        QCOMPARE(model().textLayoutLabel(), QString("Paragraphs"));
    }
    void kDoesNothingInTheOtherModes() {
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_K);
        QCOMPARE(model().textLayoutLabel(), QString("Paragraphs"));
        model().setMode(OverlayModel::Record);
        model().keyPressed(Qt::Key_K);
        QCOMPARE(model().textLayoutLabel(), QString("Paragraphs"));
        QVERIFY(!QFile::exists(statePath()) || !stateText().contains("layout"));
    }
    void theChoiceIsWrittenToRememberedState() {
        QVERIFY(env->request({"capture", "text"}).ok);
        model().keyPressed(Qt::Key_K);
        QVERIFY(stateText().contains("layout = \"lines\""));
        model().keyPressed(Qt::Key_K);
        QVERIFY(stateText().contains("layout = \"table\""));
    }
    void theNextTextCaptureOpensInTheLastLayoutAndSoDoesARestart() {
        QVERIFY(env->request({"capture", "text"}).ok);
        model().keyPressed(Qt::Key_K);
        model().cancel();
        QVERIFY(env->request({"capture", "text"}).ok); // Ctrl+Shift+Print again
        QCOMPARE(model().textLayoutLabel(), QString("Lines"));
        model().keyPressed(Qt::Key_K); // Table
        model().cancel();
        env->restartDaemon(); // a restart: the new daemon reads the file
        QVERIFY(env->request({"capture", "text"}).ok);
        QCOMPARE(model().textLayoutLabel(), QString("Table"));
    }
    void anUnreadableLayoutIsParagraphs() {
        rememberLayout("sideways");
        env->restartDaemon();
        QVERIFY(env->request({"capture", "text"}).ok);
        QCOMPARE(model().textLayoutLabel(), QString("Paragraphs"));
    }

    // ---- Paragraphs and Lines ----
    void paragraphsIsUnchangedAndUsesTheDefaultCall() {
        textCapture(0);
        QCOMPARE(env->calls("tesseract").first(), (QStringList{"stdin", "stdout", "-l", "eng"}));
        QCOMPARE(clipboardText(), QString("Tarn takes screenshots and reads text.\n\nSecond paragraph has two lines."));
    }
    // ---- Lines from word boxes ----
    void indentationIsKeptAtTwoAndFourLevels() {
        const QByteArray tsv = monoCode({
            "void copy() {",
            "  if (ready) {",
            "    send(text);",
            "  }",
            "}",
        });
        QCOMPARE(tarn::linesFromTsv(tsv), QString("void copy() {\n"
                                                  "  if (ready) {\n"
                                                  "    send(text);\n"
                                                  "  }\n"
                                                  "}"));
    }
    void deepIndentsStayExact() {
        // Boxes are tight to the ink, so a word's width over its length reads a
        // little under the real character width; 32 columns must not drift.
        const QByteArray tsv = monoCode({
            "a",
            "        bb",
            "                ccc",
            "                        dddd",
            "                                eeeee",
        });
        QCOMPARE(tarn::linesFromTsv(tsv), QString("a\n        bb\n                ccc\n"
                                                  "                        dddd\n"
                                                  "                                eeeee"));
    }
    void aRealBlankLineBetweenBlocksIsKeptAsOne() {
        const QByteArray tsv = monoCode({
            "int a() {",
            "    return 1;",
            "}",
            "",
            "int b() {",
            "    return {};",
            "}",
            "",
            "",
            "",
            "int c;",
        });
        QCOMPARE(tarn::linesFromTsv(tsv), QString("int a() {\n"
                                                  "    return 1;\n"
                                                  "}\n"
                                                  "\n"
                                                  "int b() {\n"
                                                  "    return {};\n"
                                                  "}\n"
                                                  "\n"
                                                  "int c;"));
    }
    void aBlankLineInAShortSelectionIsKept() {
        QCOMPARE(tarn::linesFromTsv(monoCode({"one;", "two;", "", "three;"})), QString("one;\ntwo;\n\nthree;"));
        QCOMPARE(tarn::linesFromTsv(monoCode({"one;", "", "two;", "three;"})), QString("one;\n\ntwo;\nthree;"));
    }
    void shortClosingLinesGetNoBlankLineBeforeThem() {
        // Plain Tesseract starts a paragraph before `}` and `return {};`; the
        // word boxes sit on the usual pitch, so no blank line comes out.
        const QByteArray tsv = monoCode({
            "if (x) {",
            "  if (y) {",
            "    return {};",
            "  }",
            "}",
            "done();",
        });
        QCOMPARE(tarn::linesFromTsv(tsv), QString("if (x) {\n  if (y) {\n    return {};\n  }\n}\ndone();"));
    }
    void realMonospacedCodeKeepsItsSpacingWithinLines() {
        // Real Tesseract splits `connect(&m_timer` and `Clipboard::sharedDir`
        // into two words; on monospaced text the gap says how many spaces
        // there really are, none here. (Its one misread, `Cachelocation`, stays.)
        textCapture(1, "tsv-code.txt");
        QCOMPARE(clipboardText(), QString("Clipboard::Clipboard(Daemon &daemon) : m_daemon(daemon) {\n"
                                          "    sweep(); // start-up\n"
                                          "    m_timer.setInterval(kSweepEveryMs);\n"
                                          "    connect(&m_timer, &QTimer::timeout, this, [this] { sweep(); });\n"
                                          "    m_timer.start();\n"
                                          "}\n"
                                          "\n"
                                          "QString Clipboard::sharedDir() {\n"
                                          "    QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCachelocation);\n"
                                          "    if (cache.isEmpty())\n"
                                          "        cache = QDir::homePath() + QStringLiteral(\"/.cache\");\n"
                                          "    return cache + QStringLiteral(\"/tarn/shared\");\n"
                                          "}"));
    }
    void aWordSplitBetweenColonsJoinsUp() {
        // `:` is a thin glyph in a wide cell: a split between the two colons of
        // `::` leaves a gap of half a character, still no space.
        const QByteArray tsv = kTsvHeader +
                               "5\t1\t1\t1\t1\t1\t21\t30\t98\t16\t90\tClipboard:\n"   // ends at 119
                               "5\t1\t1\t1\t1\t2\t126\t30\t168\t16\t90\t:Clipboard(Daemon\n" // gap 7: (7 - 2) / 10
                               "5\t1\t1\t1\t1\t3\t306\t30\t78\t16\t90\t&daemon)\n"   // one space: gap 12
                               "5\t1\t1\t1\t1\t4\t396\t28\t8\t20\t90\t{\n";
        QCOMPARE(tarn::linesFromTsv(tsv), QString("Clipboard::Clipboard(Daemon &daemon) {"));
    }
    void aSplitInsideDoubleColonsAlwaysJoins() {
        // Tesseract's boxes for `QFile:` and `:remove` can leave both colons out,
        // a gap of two characters (real reads, Liberation Mono 15 px: 1.85-2.14).
        const QByteArray tsv = kTsvHeader +
                               "5\t1\t1\t1\t1\t1\t21\t30\t48\t16\t90\tQFile:\n"          // ink of `QFile` only
                               "5\t1\t1\t1\t1\t2\t91\t30\t128\t16\t90\t:remove(path);\n" // ink from `r`
                               "5\t1\t1\t1\t1\t3\t231\t30\t58\t16\t90\treturn\n"
                               "5\t1\t1\t1\t1\t4\t301\t30\t28\t16\t90\t{};\n";
        QCOMPARE(tarn::linesFromTsv(tsv), QString("QFile::remove(path); return {};"));
    }
    void monospacedAlignmentSpacesAreKept() {
        QCOMPARE(tarn::linesFromTsv(monoCode({"int a    = 1;", "int bbbb = 2;"})), QString("int a    = 1;\nint bbbb = 2;"));
    }
    void proportionalWordsWithNarrowSpacesKeepOneSpace() {
        // A condensed proportional font: spaces under half a character. Word
        // widths do not follow their lengths, so the gaps are not read as columns.
        const QByteArray tsv = kTsvHeader +
                               "5\t1\t1\t1\t1\t1\t10\t10\t14\t20\t90\till\n"
                               "5\t1\t1\t1\t1\t2\t27\t10\t52\t20\t90\tmoms\n"
                               "5\t1\t1\t1\t1\t3\t82\t10\t20\t20\t90\tlift\n"
                               "5\t1\t1\t1\t1\t4\t105\t10\t61\t20\t90\twarm\n"
                               "5\t1\t1\t1\t1\t5\t169\t10\t27\t20\t90\tjilt\n"
                               "5\t1\t1\t1\t1\t6\t199\t10\t70\t20\t90\tWOMB\n";
        QCOMPARE(tarn::linesFromTsv(tsv), QString("ill moms lift warm jilt WOMB"));
    }
    void realProportionalProseHasOneSpaceBetweenWords() {
        textCapture(1, "tsv-prose-sans.txt");
        QCOMPARE(clipboardText(), QString("Tarn reads the words on the screen and puts them on the clipboard,\n"
                                          "so that nothing has to be typed out again by hand. It keeps every\n"
                                          "line break, and a blank line only where the text really has one.\n"
                                          "\n"
                                          "A second paragraph follows, with a few short words: a, an, it, of."));
    }
    void linesAsksForPsm6TsvAndCopiesTheRebuiltLines() {
        env->setFixture("tesseract", "tsv.txt", monoCode({"fn main() {", "    go();", "}"}));
        textCapture(1);
        QCOMPARE(env->calls("tesseract").size(), 1);
        QCOMPARE(env->calls("tesseract").first(), (QStringList{"stdin", "stdout", "-l", "eng", "--psm", "6", "tsv"}));
        QCOMPARE(clipboardText(), QString("fn main() {\n    go();\n}"));
        QCOMPARE(notes(), QStringList{"fn main() {"});
    }
    void proportionalProseReadsAsPlainLines() {
        textCapture(1, "tsv-prose.txt");
        QCOMPARE(clipboardText(), QString(kProseLines));
    }
    void proseStartingAFewPixelsApartIsNotIndented() {
        // Proportional letters start their ink a pixel or two in ("T" vs "o").
        const QByteArray tsv = kTsvHeader +
                               "5\t1\t1\t1\t1\t1\t12\t14\t39\t20\t90\tThe\n"
                               "5\t1\t1\t1\t1\t2\t59\t14\t48\t20\t90\tlines\n"
                               "5\t1\t1\t1\t1\t1\t14\t44\t21\t20\t90\ton\n"
                               "5\t1\t1\t1\t1\t2\t41\t44\t30\t20\t90\tthe\n"
                               "5\t1\t1\t1\t1\t1\t13\t74\t66\t20\t90\twrapped\n"
                               "5\t1\t1\t1\t1\t1\t15\t134\t48\t20\t90\tAfter\n"; // a paragraph gap above
        QCOMPARE(tarn::linesFromTsv(tsv), QString("The lines\non the\nwrapped\n\nAfter"));
    }
    void linesWithNoWordsIsEmpty() {
        QCOMPARE(tarn::linesFromTsv(kTsvHeader), QString());
        QCOMPARE(tarn::linesFromTsv(""), QString());
    }

    // ---- Table ----
    void tableAsksForPsm6TsvWithTheConfiguredLanguages() {
        textCapture(2, "tsv-table.txt");
        QCOMPARE(env->calls("tesseract").first(), (QStringList{"stdin", "stdout", "-l", "eng", "--psm", "6", "tsv"}));
        QCOMPARE(env->calls("tesseract").size(), 1);
    }
    void aWebTableComesOutTabSeparated() {
        textCapture(2, "tsv-table.txt");
        QCOMPARE(clipboardText(), QString("Name\tRole\tCity\n"
                                          "Ada Lovelace\tEngineer\tLondon\n"
                                          "Grace Hopper\tAdmiral\tNew York\n"
                                          "Linus Torvalds\tMaintainer\tPortland\n"
                                          "Margaret Hamilton\tDirector\tLos Angeles"));
        QCOMPARE(notes(), QStringList{"Name\tRole\tCity"});
    }
    void lsLongListingSplitsIntoColumns() {
        textCapture(2, "tsv-ls.txt");
        QCOMPARE(clipboardText(), QString("-rw-r--r--\t1\tjamey\tjamey\t1204\tNov\t4\t10:22\tnotes.txt\n"
                                          "drwxr-xr-x\t2\tjamey\tjamey\t4096\tNov\t3\t09:01\tsrc\n"
                                          "-rwxr-xr-x\t1\tjamey\tjamey\t88\tOct\t30\t18:45\trun.sh\n"
                                          "-rw-r--r--\t1\troot\troot\t1048576\tSep\t12\t07:30\tbig.bin"));
    }
    void proseFallsBackToLinesWithTheNotice() {
        textCapture(2, "tsv-prose.txt");
        QCOMPARE(tsvCalls(), 1);
        QCOMPARE(env->calls("tesseract").size(), 1); // Lines are rebuilt from the same word boxes
        QCOMPARE(clipboardText(), QString(kProseLines));
        QCOMPARE(env->calls("notify-send").size(), 1);
        QCOMPARE(notes(), QStringList{"No table found, copied lines"});
    }
    void rowsIgnoreTesseractsBlockAndLineIds() {
        // The fixtures give every word a wrong line id; rows still come out right.
        const QByteArray tsv =
            "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n"
            "5\t1\t3\t1\t7\t1\t10\t10\t40\t20\t90\tA\n"
            "5\t1\t1\t1\t1\t1\t200\t12\t40\t20\t90\tB\n"
            "5\t1\t2\t1\t4\t1\t12\t50\t40\t20\t90\tC\n"
            "5\t1\t9\t1\t2\t1\t202\t52\t40\t20\t90\tD\n";
        QCOMPARE(tarn::tableFromTsv(tsv), QString("A\tB\nC\tD"));
    }
    void aShortWordLikeADashStaysOnItsRow() {
        // `ls -l`'s "-" size is a 2 px tall box a little off the row's center, above
        // it or below it; it used to get a row of its own.
        const QByteArray tsv =
            "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n"
            "5\t1\t1\t1\t1\t1\t10\t10\t60\t20\t90\tdrwx\n"
            "5\t1\t1\t1\t1\t2\t200\t21\t8\t2\t90\t-\n" // center 22 vs 20
            "5\t1\t1\t1\t1\t3\t300\t10\t60\t20\t90\tBrain\n"
            "5\t1\t1\t1\t1\t1\t10\t40\t60\t20\t90\tdrwx\n"
            "5\t1\t1\t1\t1\t2\t200\t46\t8\t2\t90\t-\n" // center 47 vs 50
            "5\t1\t1\t1\t1\t3\t300\t40\t60\t20\t90\tMusic\n";
        QCOMPARE(tarn::tableFromTsv(tsv), QString("drwx\t-\tBrain\ndrwx\t-\tMusic"));
    }
    void aColumnNeedsNinetyPercentOfRowsEmpty() {
        // Ten rows; one stray word crosses the gap in one row (10%): still a gap.
        QByteArray tsv = "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n";
        for (int r = 0; r < 10; ++r) {
            const int top = 10 + r * 30;
            tsv += QByteArray("5\t1\t1\t1\t1\t1\t10\t") + QByteArray::number(top) + "\t60\t20\t90\tL" + QByteArray::number(r) + "\n";
            tsv += QByteArray("5\t1\t1\t1\t1\t2\t200\t") + QByteArray::number(top) + "\t60\t20\t90\tR" + QByteArray::number(r) + "\n";
        }
        tsv += "5\t1\t1\t1\t1\t3\t60\t10\t150\t20\t90\tX\n"; // bridges the gap in row 0 only
        const QString table = tarn::tableFromTsv(tsv);
        QVERIFY(table.startsWith("L0"));
        QCOMPARE(table.split('\n').size(), 10);
        QVERIFY(table.contains("\tR9"));
        // Two bridged rows out of ten is 20%: the gap is gone, one column.
        tsv += "5\t1\t1\t1\t1\t4\t60\t40\t150\t20\t90\tY\n";
        QVERIFY(tarn::tableFromTsv(tsv).isEmpty());
    }
    void nothingReadIsNoTextFound() {
        env->setMode("tesseract", "empty");
        textCapture(2);
        QCOMPARE(notes(), QStringList{"No text found"});
        QVERIFY(env->calls("tarn-clip").isEmpty());
    }

    void anEnlargedReadGivesTheSameLinesAndTable() {
        // Lines and Table measure in characters and line heights, so the 2x
        // picture Tesseract now reads changes nothing (Table's 2 px gap floor
        // only matters for words under 5 px tall).
        for (const char *name : {"tsv-code.txt", "tsv-ls.txt", "tsv-prose.txt", "tsv-prose-sans.txt", "tsv-table.txt"}) {
            const QByteArray tsv = stubFixture(name);
            QVERIFY2(!tsv.isEmpty(), name);
            QCOMPARE(tarn::linesFromTsv(doubled(tsv)), tarn::linesFromTsv(tsv));
            QCOMPARE(tarn::tableFromTsv(doubled(tsv)), tarn::tableFromTsv(tsv));
        }
    }
};

QTEST_MAIN(TextLayoutTest)
#include "text_layout_test.moc"
