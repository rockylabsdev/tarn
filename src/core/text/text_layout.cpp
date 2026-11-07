#include "text_layout.h"

#include <QList>
#include <QMap>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace tarn {

namespace {

struct Word {
    QString text;
    int left, top, width, height;
    double center() const { return top + height / 2.0; }
};

struct Row {
    QList<Word> words;
    double center = 0;
    double height = 0;
    void add(const Word &w) {
        const int n = words.size();
        center = (center * n + w.center()) / (n + 1);
        height = (height * n + w.height) / (n + 1);
        words << w;
    }
};

// The word rows of the TSV (level 5, with text). The block, paragraph and line
// ids are read past on purpose: they are Tesseract's guess, and Lines and
// Table do their own grouping.
QList<Word> parseWords(const QByteArray &tsv) {
    QList<Word> words;
    for (const QByteArray &raw : tsv.split('\n')) {
        const QList<QByteArray> f = raw.trimmed().isEmpty() ? QList<QByteArray>() : raw.split('\t');
        if (f.size() < 12 || f.at(0) != "5")
            continue;
        const QString text = QString::fromUtf8(f.mid(11).join('\t')).trimmed();
        bool ok = true;
        const Word w{text, f.at(6).toInt(&ok), f.at(7).toInt(), f.at(8).toInt(), f.at(9).toInt()};
        if (ok && !text.isEmpty() && w.width > 0 && w.height > 0)
            words << w;
    }
    return words;
}

// Rows: by vertical center, top to bottom. A word joins the row whose center is
// within half a word height of its own: the taller of the two, so a short word
// (a "-", a ".") a little off center still joins its row. Words in a row run
// left to right.
QList<Row> rowsOf(QList<Word> words) {
    std::sort(words.begin(), words.end(), [](const Word &a, const Word &b) { return a.center() < b.center(); });
    QList<Row> rows;
    for (const Word &w : std::as_const(words)) {
        if (rows.isEmpty() || w.center() - rows.last().center > 0.5 * std::max<double>(rows.last().height, w.height))
            rows << Row();
        rows.last().add(w);
    }
    for (Row &row : rows)
        std::sort(row.words.begin(), row.words.end(), [](const Word &a, const Word &b) { return a.left < b.left; });
    return rows;
}

// The lower median: of an even count, the smaller middle value, so with one
// wide gap among two the usual line pitch is the narrow one. `values` is not empty.
double median(QList<double> values) {
    std::sort(values.begin(), values.end());
    return values.at((values.size() - 1) / 2);
}

// The character grid of the text, from the word boxes. `words` is not empty.
struct CharGrid {
    double width = 1; // a character's advance
    double inset = 0; // how much narrower a word's box is than its characters
    bool monospaced = false;
};

// Tesseract's boxes are tight to the ink, so a word's width over its length
// reads a little narrow, the more so the shorter the word; how much wider a
// word gets per extra character does not. The width is the median, over pairs
// of word lengths, of the change in (median) box width per character: exact
// for monospaced text. With words of one length only, their width over their
// length. The text is monospaced when at least 85% of the words of two or more
// characters (and at least 3 of them) are within 0.35 of a character of
// length x width - inset: real code read 93%, real proportional prose 64%.
CharGrid charGridOf(const QList<Word> &words) {
    QMap<int, QList<double>> widthsByLength;
    for (const Word &w : words)
        widthsByLength[int(w.text.size())] << w.width;
    QList<int> lengths;
    QList<double> widths;
    for (auto it = widthsByLength.cbegin(); it != widthsByLength.cend(); ++it) {
        lengths << it.key();
        widths << median(it.value());
    }
    QList<double> slopes;
    for (int i = 0; i < lengths.size(); ++i)
        for (int j = i + 1; j < lengths.size(); ++j)
            slopes << (widths.at(j) - widths.at(i)) / (lengths.at(j) - lengths.at(i));
    CharGrid grid;
    if (!slopes.isEmpty() && median(slopes) > 0) {
        grid.width = median(slopes);
    } else {
        QList<double> perChar;
        for (const Word &w : words)
            perChar << double(w.width) / w.text.size();
        grid.width = median(perChar);
    }

    QList<double> insets;
    for (const Word &w : words)
        insets << grid.width * w.text.size() - w.width;
    grid.inset = median(insets);

    int longWords = 0, onGrid = 0;
    for (const Word &w : words) {
        if (w.text.size() < 2)
            continue;
        ++longWords;
        onGrid += std::abs(grid.width * w.text.size() - grid.inset - w.width) <= 0.35 * grid.width;
    }
    grid.monospaced = longWords >= 3 && onGrid * 20 >= longWords * 17;
    return grid;
}

struct Column {
    int from, to; // [from, to) in x - minX
};

} // namespace

QString linesFromTsv(const QByteArray &tsv) {
    const QList<Word> words = parseWords(tsv);
    if (words.isEmpty())
        return {};
    const QList<Row> rows = rowsOf(words);
    const CharGrid grid = charGridOf(words);

    int minLeft = rows.first().words.first().left;
    for (const Row &row : rows)
        minLeft = std::min(minLeft, row.words.first().left);

    // A blank line goes where the gap to the line above is clearly more than
    // the usual line pitch (the median gap), one however wide the gap. Two
    // lines have no usual pitch to compare with, so never a blank between them.
    QList<double> gaps;
    for (int i = 1; i < rows.size(); ++i)
        gaps << rows.at(i).center - rows.at(i - 1).center;
    const double pitch = gaps.isEmpty() ? 0 : median(gaps);

    QStringList lines;
    for (int i = 0; i < rows.size(); ++i) {
        const Row &row = rows.at(i);
        if (i > 0 && gaps.at(i - 1) > 1.5 * pitch)
            lines << QString();
        const int indent = int(std::lround((row.words.first().left - minLeft) / grid.width));
        QString line = QString(indent, QLatin1Char(' ')) + row.words.first().text;
        for (int j = 1; j < row.words.size(); ++j) {
            const Word &before = row.words.at(j - 1), &w = row.words.at(j);
            // Monospaced: the gap (less the ink inset) is that many characters,
            // so a word Tesseract split in two joins up again and aligned
            // columns stay aligned. A split leaves up to about half a character
            // (`:` and `(` are thin glyphs in wide cells), a real space at least
            // three quarters (real reads: 0.2-0.42 and 0.75-1.42), so a space
            // starts at 0.6. Proportional: one space. A `::` Tesseract split
            // always joins: its boxes can leave the colons out, which reads as
            // a gap of up to two characters (and `x :: Int` is three words).
            const double gap = (w.left - (before.left + before.width) - grid.inset) / grid.width;
            int spaces = grid.monospaced ? std::max(0, int(std::floor(gap + 0.4))) : 1;
            if (before.text.endsWith(QLatin1Char(':')) && w.text.startsWith(QLatin1Char(':')))
                spaces = 0;
            line += QString(spaces, QLatin1Char(' ')) + w.text;
        }
        lines << line;
    }
    return lines.join(QLatin1Char('\n'));
}

QString tableFromTsv(const QByteArray &tsv) {
    QList<Word> words = parseWords(tsv);
    if (words.size() < 2)
        return {};

    QList<Row> rows = rowsOf(words);
    if (rows.size() < 2)
        return {};

    // Columns: each row covers the x positions its words cover. A strip is a
    // gap when at most 10% of rows cover it (empty in at least 90%). A gap
    // narrower than 40% of a typical word height is only the space between
    // two words, so it does not cut a column.
    int minX = words.first().left, maxX = 0;
    QList<int> heights;
    for (const Word &w : std::as_const(words)) {
        minX = std::min(minX, w.left);
        maxX = std::max(maxX, w.left + w.width);
        heights << w.height;
    }
    std::sort(heights.begin(), heights.end());
    const int minGap = std::max(2, int(0.4 * heights.at(heights.size() / 2)));
    const int span = maxX - minX;
    QList<int> covered(span, 0);
    for (const Row &row : std::as_const(rows)) {
        QList<bool> hit(span, false);
        for (const Word &w : row.words)
            for (int x = w.left - minX; x < w.left - minX + w.width && x < span; ++x)
                hit[x] = true;
        for (int x = 0; x < span; ++x)
            covered[x] += hit.at(x) ? 1 : 0;
    }
    const auto isGap = [&](int x) { return covered.at(x) * 10 <= rows.size(); };

    QList<Column> columns;
    int x = 0;
    while (x < span) {
        if (isGap(x)) {
            ++x;
            continue;
        }
        int end = x, probe = x;
        while (probe < span) {
            if (!isGap(probe)) {
                end = probe + 1;
                ++probe;
                continue;
            }
            int g = probe;
            while (g < span && isGap(g))
                ++g;
            if (g - probe >= minGap || g >= span)
                break;
            probe = g;
        }
        columns << Column{x, end};
        x = end;
    }
    if (columns.size() < 2)
        return {};

    QStringList lines;
    for (const Row &row : std::as_const(rows)) {
        QStringList cells;
        for (int c = 0; c < columns.size(); ++c)
            cells << QString();
        for (const Word &w : std::as_const(row.words)) {
            const int center = w.left - minX + w.width / 2;
            int c = 0;
            while (c + 1 < columns.size() && center >= columns.at(c + 1).from)
                ++c;
            cells[c] += (cells.at(c).isEmpty() ? QString() : QStringLiteral(" ")) + w.text;
        }
        lines << cells.join(QLatin1Char('\t'));
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace tarn
