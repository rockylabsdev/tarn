#pragma once
// Text layout: how a Text capture arranges what it copies.
#include <QByteArray>
#include <QString>

namespace tarn {

enum class TextLayout { Paragraphs, Lines, Table };

inline QString textLayoutName(TextLayout layout) {
    switch (layout) {
    case TextLayout::Lines: return QStringLiteral("Lines");
    case TextLayout::Table: return QStringLiteral("Table");
    default: return QStringLiteral("Paragraphs");
    }
}

// The word written in Remembered state ("text.layout").
inline QString textLayoutKey(TextLayout layout) { return textLayoutName(layout).toLower(); }

inline TextLayout textLayoutFromKey(const QString &key) {
    if (key == QLatin1String("lines"))
        return TextLayout::Lines;
    if (key == QLatin1String("table"))
        return TextLayout::Table;
    return TextLayout::Paragraphs; // also anything unreadable
}

inline TextLayout nextTextLayout(TextLayout layout) { return TextLayout((int(layout) + 1) % 3); }

// Lines rebuilt from `tesseract ... --psm 6 tsv` word boxes, rows grouped as
// for Table. A line is indented by (its first word's left - the
// leftmost left) / the character width, as spaces. On monospaced text the
// spaces between words follow their gap (none where Tesseract split a word);
// otherwise words are joined by one space. A split `::` always joins. One blank line goes where the gap to the line above is over 1.5x the
// median line pitch, never more than one in a row. Empty when there is no text.
QString linesFromTsv(const QByteArray &tsv);

// Rebuilds a table from `tesseract ... --psm 6 tsv` output: words regrouped
// into rows by vertical center (within half the taller word's height), columns cut at vertical strips that are empty
// in at least 90% of rows, cells joined with tabs and rows with newlines.
// Empty when fewer than two columns are found.
QString tableFromTsv(const QByteArray &tsv);

} // namespace tarn
