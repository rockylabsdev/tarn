// The Text tool. Core: Text in each look and over several lines, rendered in
// Tarn's UI font (the bundled Geist) at the Text size steps, compared with
// stored reference PNGs; typing, finishing, selecting, moving, recoloring and
// editing a Text again, each with undo. UI: Enter adds a line (and is not Done),
// Esc finishes the Text before stepping back further, and the emoji picker
// searches and inserts, driven with real key and mouse events off-screen.
//
// Regenerate the references through the check, which draws with the fonts CI has,
// then look at the PNGs:
//   TARN_UPDATE_REFERENCES=1 scripts/check.sh --only text_tool
#include "test_env.h"

#include "annotations/text_annotation.h"
#include "editor/editor.h"
#include "editor/tool_options.h"
#include "editor_windows.h"
#include "emoji_index.h"
#include "palette/palette.h"
#include "theme.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QFontInfo>
#include <QImage>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>

using tarn::EditorSession;
using tarn::TextAnnotation;
using tarn::test::TestEnv;

#ifndef TARN_TEXT_REFERENCE_DIR
#error "set by tests/text_tool/CMakeLists.txt"
#endif

namespace {

// A shot big enough for a few lines of text, light enough on the left for the
// outlines to show and dark on the right for the labels to.
QImage testShot() {
    QImage image(220, 130, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb(40 + x, 70 + y, 150 - x / 3));
    return image;
}

QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

void compareWithReference(const QImage &actual, const QString &name) {
    const QString path = QStringLiteral(TARN_TEXT_REFERENCE_DIR "/") + name;
    if (qEnvironmentVariableIsSet("TARN_UPDATE_REFERENCES")) {
        QVERIFY(actual.save(path, "PNG"));
        return;
    }
    QImage expected(path);
    QVERIFY2(!expected.isNull(), qPrintable("missing reference " + path));
    QCOMPARE(actual.size(), expected.size());
    QCOMPARE(actual.convertToFormat(QImage::Format_ARGB32), expected.convertToFormat(QImage::Format_ARGB32));
}

QQuickItem *findItem(QQuickItem *from, const QString &objectName) {
    if (from->objectName() == objectName)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = findItem(child, objectName))
            return hit;
    return nullptr;
}

// QTest has no keyClicks for a window: one key press per character.
void typeKeys(QWindow *window, const QString &text) {
    for (const QChar c : text)
        QTest::keyClick(window, c.toLatin1());
}

QPoint centerOf(QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }

} // namespace

class TextToolTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    QDateTime when{QDate(2026, 11, 3), QTime(9, 30, 0)};
    int shots = 0;

    tarn::ThumbnailStack &stack() { return env->daemon().service<tarn::ThumbnailStack>(); }
    tarn::Editor &editor() { return env->daemon().service<tarn::Editor>(); }
    tarn::ToolOptions &options() { return env->daemon().service<tarn::ToolOptions>(); }
    int addShot() { return stack().add(pngOf(testShot()), when.addSecs(shots++)); }
    // An Editor session on the Text tool.
    EditorSession *openEditor() {
        const int id = addShot();
        stack().edit(id);
        auto *s = editor().session(id);
        s->setTool("text");
        return s;
    }
    // Types a whole Text the way the text field reports it, and finishes it.
    static int typeText(EditorSession *s, qreal x, qreal y, const QString &text) {
        s->pointerPressed(x, y);
        s->pointerReleased(x, y);
        s->setText(text);
        s->finishText();
        return s->selection().isEmpty() ? -1 : s->selection().first();
    }

private slots:
    void initTestCase() { tarn::loadFonts(); }
    void init() {
        env = std::make_unique<TestEnv>();
        shots = 0;
    }
    void cleanup() { env.reset(); }

    // ---- font and sizes ----

    void textIsSetInTheUiFont() {
        QCOMPARE(QFontInfo(TextAnnotation::fontFor(30)).family(), QString("Geist"));
    }

    void textFollowsTheTextSizeSteps() {
        auto *s = openEditor();
        QList<qreal> heights;
        for (int step = 0; step < 6; ++step) {
            s->setSizeIndex(step);
            QCOMPARE(s->size(), qreal(QList<int>{16, 22, 30, 40, 56, 80}[step]));
            s->beginText(4, 4);
            s->setText("Hi");
            s->finishText();
            heights << s->document().annotation(s->selection().first())->bounds().height();
        }
        for (int i = 1; i < heights.size(); ++i)
            QVERIFY2(heights[i] > heights[i - 1], "a bigger step makes bigger text");
        // one line of 30 px Geist: its own line height, not a stand-in
        QCOMPARE(heights[2], QFontMetricsF(TextAnnotation::fontFor(30)).lineSpacing());
    }

    // ---- each look and several lines, against reference images ----

    void plainText() {
        auto *s = openEditor();
        s->setTextLook(0);
        typeText(s, 14, 12, "Plain text");
        compareWithReference(s->document().flatten(), "plain.png");
    }

    void outlinedText() {
        auto *s = openEditor();
        s->setTextLook(1);
        s->setColor(QColor("#ffd60a"));
        typeText(s, 14, 12, "Outlined");
        compareWithReference(s->document().flatten(), "outlined.png");
    }

    void labelText() {
        auto *s = openEditor();
        s->setTextLook(2);
        s->setColor(QColor("#0a84ff"));
        typeText(s, 22, 20, "Label");
        compareWithReference(s->document().flatten(), "label.png");
    }

    void multiLineText() {
        auto *s = openEditor();
        s->setTextLook(0);
        s->setColor(QColor("#ffffff"));
        typeText(s, 14, 12, "First line\nSecond\nThird one");
        compareWithReference(s->document().flatten(), "multiline.png");
    }

    void multiLineLabel() {
        auto *s = openEditor();
        s->setTextLook(2);
        s->setColor(QColor("#ffd60a"));
        typeText(s, 24, 24, "Two lines\nin a label");
        compareWithReference(s->document().flatten(), "multiline-label.png");
    }

    void aLabelOnALightColorGetsDarkText() {
        QCOMPARE(TextAnnotation::contrastOf(QColor("#ffd60a")), QColor(Qt::black));
        QCOMPARE(TextAnnotation::contrastOf(QColor("#0a84ff")), QColor(Qt::white));
    }

    // ---- typing, finishing, editing again ----

    void aClickWithTheTextToolStartsATextAndFinishingAddsItSelected() {
        auto *s = openEditor();
        s->pointerPressed(30, 20);
        s->pointerReleased(30, 20);
        QVERIFY(s->textEditing());
        QCOMPARE(s->textOrigin(), QPointF(30, 20));
        QCOMPARE(s->annotationCount(), 0); // not in the document until finished
        s->setText("hello");
        QVERIFY(s->draft()); // but shown while typed
        s->finishText();
        QVERIFY(!s->textEditing());
        QCOMPARE(s->annotationCount(), 1);
        QCOMPARE(s->selectionCount(), 1);
        QVERIFY(!s->draft());
        s->undo();
        QCOMPARE(s->annotationCount(), 0); // one undo step for the whole Text
        s->redo();
        QCOMPARE(s->annotationCount(), 1);
    }

    void anEmptyTextIsDropped() {
        auto *s = openEditor();
        s->pointerPressed(30, 20);
        s->setText("   ");
        s->finishText();
        QCOMPARE(s->annotationCount(), 0);
        QVERIFY(!s->canUndo());
    }

    void escFinishesTheTextBeforeSteppingBackFurther() {
        auto *s = openEditor();
        s->pointerPressed(30, 20);
        s->setText("abc");
        s->escape(); // finishes the Text
        QVERIFY(s->isOpen());
        QVERIFY(!s->textEditing());
        QCOMPARE(s->annotationCount(), 1);
        QCOMPARE(s->selectionCount(), 1);
        s->escape(); // deselects
        QVERIFY(s->isOpen());
        QCOMPARE(s->selectionCount(), 0);
        s->escape(); // cancels the Editor
        QVERIFY(!s->isOpen());
    }

    void aTextCanBeSelectedMovedAndRecoloredLater() {
        auto *s = openEditor();
        s->setColor(QColor("#ff453a"));
        const int id = typeText(s, 20, 20, "Move me");
        s->deselect();
        QCOMPARE(s->selectionCount(), 0);
        // the Select tool picks it up
        s->setTool("select");
        const QRectF before = s->document().annotation(id)->bounds();
        s->pointerPressed(before.center().x(), before.center().y());
        s->pointerMoved(before.center().x() + 30, before.center().y() + 10);
        s->pointerReleased(before.center().x() + 30, before.center().y() + 10);
        QCOMPARE(s->document().annotation(id)->bounds().topLeft(), before.topLeft() + QPointF(30, 10));
        QCOMPARE(s->selection(), QList<int>{id});
        s->undo();
        QCOMPARE(s->document().annotation(id)->bounds().topLeft(), before.topLeft());
        s->redo();
        // recolor it
        s->setColor(QColor("#30d158"));
        const auto *text = dynamic_cast<const TextAnnotation *>(s->document().annotation(id));
        QVERIFY(text);
        QCOMPARE(text->color(), QColor("#30d158"));
        QCOMPARE(text->text(), QString("Move me"));
    }

    void aTextIsEditedAgainByTheTextToolOrADoubleClick() {
        auto *s = openEditor();
        const int id = typeText(s, 20, 20, "old");
        s->deselect();
        // the Text tool on an unselected Text edits it
        const QRectF b = s->document().annotation(id)->bounds();
        s->pointerPressed(b.center().x(), b.center().y());
        s->pointerReleased(b.center().x(), b.center().y());
        QVERIFY(s->textEditing());
        QCOMPARE(s->text(), QString("old"));
        s->setText("older and longer");
        QCOMPARE(s->annotationCount(), 1); // edited in place, not a second Text
        s->finishText();
        QCOMPARE(dynamic_cast<const TextAnnotation *>(s->document().annotation(id))->text(), QString("older and longer"));
        s->undo(); // the edit is one step
        QCOMPARE(dynamic_cast<const TextAnnotation *>(s->document().annotation(id))->text(), QString("old"));
        s->redo();
        // a double click edits it with any tool
        s->deselect();
        s->setTool("select");
        QVERIFY(s->editTextAt(b.center().x(), b.center().y()));
        QCOMPARE(s->text(), QString("older and longer"));
        s->finishText();
        QVERIFY(!s->canRedo());
        // an edit that changes nothing leaves no undo step
        const bool couldUndoBefore = s->canUndo();
        QVERIFY(s->editText(id));
        s->finishText();
        QCOMPARE(s->canUndo(), couldUndoBefore);
        // emptying it deletes it, and undo brings it back
        QVERIFY(s->editText(id));
        s->setText("");
        s->finishText();
        QCOMPARE(s->annotationCount(), 0);
        s->undo();
        QCOMPARE(s->annotationCount(), 1);
        QCOMPARE(dynamic_cast<const TextAnnotation *>(s->document().annotation(id))->text(), QString("older and longer"));
    }

    void theLookAndSizeRestyleASelectedText() {
        auto *s = openEditor();
        const int id = typeText(s, 20, 20, "Style me");
        auto textOf = [&] { return dynamic_cast<const TextAnnotation *>(s->document().annotation(id)); };
        QCOMPARE(textOf()->look(), TextAnnotation::Plain);
        s->setTextLook(2);
        QCOMPARE(textOf()->look(), TextAnnotation::Label);
        s->undo();
        QCOMPARE(textOf()->look(), TextAnnotation::Plain);
        s->redo();
        s->setSizeIndex(0);
        QCOMPARE(textOf()->size(), 16.0);
        // and the look is remembered for the next Text
        QCOMPARE(options().styleValue("text_look").toInt(), 2);
    }

    void changingToolFinishesTheText() {
        auto *s = openEditor();
        s->pointerPressed(30, 20);
        s->setText("kept");
        s->setTool("rect");
        QVERIFY(!s->textEditing());
        QCOMPARE(s->annotationCount(), 1);
    }

    void doneAndCopyIncludeAText() {
        auto *s = openEditor();
        s->pointerPressed(30, 20);
        s->setText("typed, not yet finished");
        QVERIFY(s->copyAndClose()); // finishes it first
        QVERIFY(!s->isOpen());
    }

    // ---- the emoji data ----

    void emojiSearchFindsByNameAndKeyword() {
        tarn::EmojiIndex index;
        QVERIFY(index.count() > 1000);
        auto has = [&](const QString &query, const QString &emoji) {
            for (const QVariant &v : index.search(query, 500))
                if (v.toMap()["emoji"].toString() == emoji)
                    return true;
            return false;
        };
        QVERIFY(has("thumbs up", QString::fromUtf8("👍")));
        QVERIFY(has("thumbsup", QString::fromUtf8("👍"))); // an alias
        QVERIFY(has("THUMBS", QString::fromUtf8("👍")));   // case does not matter
        QVERIFY(has("fire", QString::fromUtf8("🔥")));
        QVERIFY(!has("thumbs up", QString::fromUtf8("🔥")));
        QVERIFY(index.search("zzzzqqqq").isEmpty());
        QCOMPARE(index.search("").size(), 200); // everything, capped
        QCOMPARE(index.search("heart", 5).size(), 5);
        // a name match comes before a keyword match
        QVERIFY(index.search("fire").first().toMap()["name"].toString().contains("fire"));
    }

    // ---- the Editor window ----

    void enterAddsALineEscFinishesAndTheToolKeyIsJustALetter() {
        const int id = addShot();
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        auto *session = editor().session(id);

        QTest::keyClick(view, Qt::Key_T);
        QCOMPARE(session->tool(), QString("text"));
        auto *canvas = findItem(view->rootObject(), "canvas");
        const QPoint at = canvas->mapToScene(session->docToView(20, 20)).toPoint();
        QTest::mouseClick(view, Qt::LeftButton, {}, at);
        QVERIFY(session->textEditing());
        auto *field = findItem(view->rootObject(), "textEditor");
        QVERIFY(field && field->isVisible());
        QTRY_VERIFY(field->hasActiveFocus());

        typeKeys(view, "Tt a"); // T would switch tool anywhere else
        QTest::keyClick(view, Qt::Key_Return);
        typeKeys(view, "b");
        QCOMPARE(session->tool(), QString("text"));
        QCOMPARE(session->text(), QString("Tt a\nb"));
        QVERIFY2(session->isOpen(), "Enter is not Done while typing");
        QTest::keyClick(view, Qt::Key_Backspace);
        QTest::keyClick(view, Qt::Key_Backspace);
        QCOMPARE(session->text(), QString("Tt a"));
        QTest::keyClick(view, Qt::Key_Return);
        typeKeys(view, "xyz");
        QCOMPARE(session->text(), QString("Tt a\nxyz"));
        QCOMPARE(session->annotationCount(), 0);

        QTest::keyClick(view, Qt::Key_Escape); // finishes the Text...
        QVERIFY(!session->textEditing());
        QVERIFY(session->isOpen());
        QCOMPARE(session->annotationCount(), 1);
        QCOMPARE(session->selectionCount(), 1);
        QTest::keyClick(view, Qt::Key_Escape); // ...then deselects...
        QCOMPARE(session->selectionCount(), 0);
        QVERIFY(session->isOpen());
        QTest::keyClick(view, Qt::Key_Escape); // ...then closes
        QVERIFY(!session->isOpen());
    }

    void theFieldSitsWhereTheTextIsPaintedUnderAFrameACropAndATurn() {
        const int id = addShot();
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        auto *session = editor().session(id);
        auto *canvas = findItem(view->rootObject(), "canvas");
        QVERIFY(canvas);

        session->setFrameBackground("plain", {});
        session->setFrameValue("padding", 0.2);
        session->enterCrop();
        session->setCropWidth(60);
        session->setCropHeight(40);
        session->rotateCrop(1);
        session->leaveCrop();
        session->setTool("text");
        const QPointF doc(10, 8);
        session->beginText(doc.x(), doc.y());
        QVERIFY(session->textEditing());
        auto *field = findItem(view->rootObject(), "textEditor");
        QVERIFY(field && field->isVisible());
        session->setText("Hi");
        QTest::qWait(50);

        const auto matches = [&] {
            const QPointF expected = canvas->mapToScene(session->docToView(doc.x(), doc.y()));
            const QPointF actual = field->parentItem()->mapToScene(field->position());
            return qAbs(expected.x() - actual.x()) < 0.01 && qAbs(expected.y() - actual.y()) < 0.01;
        };
        QVERIFY(matches());
        QVERIFY(session->docToView(doc.x(), doc.y()) != QPointF(doc)); // the chain really moved it

        // and it follows a later change of the Frame without being retyped
        session->setFrameValue("padding", 0.35);
        QTest::qWait(50);
        QVERIFY(matches());
    }

    void enterIsStillDoneWhenNoTextIsBeingTyped() {
        const int id = addShot();
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        auto *session = editor().session(id);
        QTest::keyClick(view, Qt::Key_T);
        QTest::keyClick(view, Qt::Key_Return);
        QVERIFY(!session->isOpen());
    }

    void theTextToolOffersLookAndEmojiAndNothingElseDoes() {
        const int id = addShot();
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        auto *session = editor().session(id);
        auto *look = findItem(view->rootObject(), "textLookMenu");
        auto *emoji = findItem(view->rootObject(), "emojiButton");
        QVERIFY(look && emoji);
        QVERIFY(!look->isVisible() && !emoji->isVisible()); // Rectangle is the opening tool
        QVERIFY(findItem(view->rootObject(), "tool-text"));
        session->setTool("text");
        QTest::qWait(60); // the top bar lays out the new buttons
        QVERIFY(look->isVisible() && emoji->isVisible());
        QVERIFY(findItem(look, "label"));
        QCOMPARE(findItem(look, "label")->property("text").toString(), QString("Plain"));
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(look));
        QQuickItem *option = nullptr;
        QTRY_VERIFY((option = findItem(view->contentItem(), "option-2")) != nullptr);
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(option));
        QCOMPARE(session->textLook(), 2);
        QCOMPARE(findItem(look, "label")->property("text").toString(), QString("Label"));
        // the size select follows the Text steps
        QCOMPARE(session->sizeGroup(), QString("text"));
        QVERIFY(findItem(view->rootObject(), "sizeMenu"));
        QCOMPARE(findItem(findItem(view->rootObject(), "sizeMenu"), "label")->property("text").toString(), QString("30 pt"));
    }

    void theEmojiPickerSearchesAndInserts() {
        const int id = addShot();
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        auto *session = editor().session(id);
        session->setTool("text");
        QTest::qWait(60); // the top bar lays out the new buttons
        auto *canvas = findItem(view->rootObject(), "canvas");
        QTest::mouseClick(view, Qt::LeftButton, {}, canvas->mapToScene(session->docToView(20, 20)).toPoint());
        typeKeys(view, "ok ");
        QCOMPARE(session->text(), QString("ok "));

        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(view->rootObject(), "emojiButton")));
        QQuickItem *grid = nullptr;
        QTRY_VERIFY((grid = findItem(view->contentItem(), "emojiGrid")) != nullptr);
        auto *search = findItem(view->contentItem(), "emojiSearch");
        QTRY_VERIFY(search->hasActiveFocus());
        QVERIFY(grid->property("count").toInt() > 1000 || grid->property("count").toInt() >= 200);
        typeKeys(view, "thumbs up");
        QTRY_COMPARE(search->property("text").toString(), QString("thumbs up"));
        const int found = grid->property("count").toInt();
        QVERIFY(found >= 1 && found < 20); // narrowed
        QQuickItem *cell = nullptr;
        QTRY_VERIFY((cell = findItem(grid, "emojiCell-0")) != nullptr);
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(cell));
        QCOMPARE(session->text(), QString("ok ") + QString::fromUtf8("👍"));
        QVERIFY(session->textEditing());

        // nothing found says so
        QTest::keyClick(view, Qt::Key_Period, Qt::ControlModifier); // Ctrl+. opens it again
        QTRY_VERIFY(search->hasActiveFocus());
        typeKeys(view, "zzzzqqqq");
        QTRY_COMPARE(grid->property("count").toInt(), 0);
        QCOMPARE(findItem(view->contentItem(), "emojiStatus")->property("text").toString(), QString("No emoji found"));
        // Esc closes the picker only
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(session->textEditing());
        QVERIFY(session->isOpen());
    }

    void anEmojiPickedWithNoTextOpenStartsOne() {
        const int id = addShot();
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        auto *session = editor().session(id);
        session->setTool("text");
        QTest::qWait(60);
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(view->rootObject(), "emojiButton")));
        QTRY_VERIFY(findItem(view->contentItem(), "emojiSearch") && findItem(view->contentItem(), "emojiSearch")->hasActiveFocus());
        typeKeys(view, "thumbs up");
        QQuickItem *cell = nullptr;
        QTRY_VERIFY((cell = findItem(view->contentItem(), "emojiCell-0")) != nullptr);
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(cell));
        QVERIFY(session->textEditing());
        QCOMPARE(session->text(), QString::fromUtf8("👍"));
        QTest::keyClick(view, Qt::Key_Escape);
        QCOMPARE(session->annotationCount(), 1);
    }
};

QTEST_MAIN(TextToolTest)
#include "text_test.moc"
