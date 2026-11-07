#include "file_name.h"

#include <QRegularExpression>
#include <QStringList>
#include <vector>

namespace tarn {

namespace {

const int kTitleLimit = 60;

bool keeps(char32_t c) {
    return QChar::isLetterOrNumber(c) || QChar::isMark(c) || c == '.' || c == '-' || c == '_';
}

// Letters (any script), digits, `.`, `-` and `_` stay; everything else becomes `-`;
// repeated `-` or `_` collapse; the ends lose `-`, `_` and `.`.
QString clean(const QString &text) {
    QString out;
    for (const char32_t c : text.toUcs4()) {
        const QChar last = out.isEmpty() ? QChar() : out.back();
        const char32_t put = keeps(c) ? c : U'-';
        if ((put == '-' || put == '_') && last == QChar(put))
            continue;
        out += QString::fromUcs4(&put, 1);
    }
    int from = 0, to = int(out.size());
    auto trimmable = [](QChar c) { return c == '-' || c == '_' || c == '.'; };
    while (from < to && trimmable(out[from]))
        ++from;
    while (to > from && trimmable(out[to - 1]))
        --to;
    return out.mid(from, to - from);
}

QString lettersAndDigits(const QString &text) {
    QString out;
    for (const QChar c : text.toLower())
        if (c.isLetterOrNumber())
            out += c;
    return out;
}

// "Issue 72 · GitHub — Mozilla Firefox" -> "Issue 72 · GitHub" when the last part
// names the app. The match ignores case and vendor prefixes ("Mozilla Firefox" is
// firefox) and the reverse-domain front of a class ("org.mozilla.firefox").
QString withoutTrailingApp(const QString &title, const QString &app) {
    if (app.isEmpty())
        return title;
    int at = -1, length = 0;
    for (const QString &separator : {QString::fromUtf8(" — "), QStringLiteral(" - "), QStringLiteral(" | ")}) {
        const int i = int(title.lastIndexOf(separator));
        if (i > at) {
            at = i;
            length = int(separator.size());
        }
    }
    if (at <= 0)
        return title;
    const QString tail = lettersAndDigits(title.mid(at + length));
    if (tail.isEmpty())
        return title;
    QStringList classes = {lettersAndDigits(app)};
    if (app.contains('.'))
        classes << lettersAndDigits(app.section('.', -1));
    for (const QString &c : classes) {
        if (c.isEmpty())
            continue;
        if (tail == c || (c.size() >= 3 && tail.endsWith(c)) || (tail.size() >= 3 && c.endsWith(tail)))
            return title.left(at).trimmed();
    }
    return title;
}

QString cleanTitle(const QString &title, const QString &app) {
    QString out = clean(withoutTrailingApp(title, app));
    if (out.size() > kTitleLimit) {
        const QString head = out.left(kTitleLimit);
        if (out[kTitleLimit] != '-') {
            const int boundary = int(head.lastIndexOf('-'));
            out = boundary > 0 ? head.left(boundary) : head;
        } else {
            out = head;
        }
        out = clean(out);
    }
    return out;
}

const QStringList &tokenNames() {
    static const QStringList names = {"date", "time", "app", "title", "kind"};
    return names;
}

struct Piece {
    bool token;
    QString text;
};

std::vector<Piece> split(const QString &pattern) {
    std::vector<Piece> pieces;
    static const QRegularExpression token(QStringLiteral("\\{([^{}]*)\\}"));
    qsizetype at = 0;
    for (auto it = token.globalMatch(pattern); it.hasNext();) {
        const auto m = it.next();
        if (m.capturedStart() > at)
            pieces.push_back({false, pattern.mid(at, m.capturedStart() - at)});
        pieces.push_back({true, m.captured(1)});
        at = m.capturedEnd();
    }
    if (at < pattern.size())
        pieces.push_back({false, pattern.mid(at)});
    return pieces;
}

} // namespace

QString defaultFileNamePattern() { return QStringLiteral("{date}_{time}_{app}"); }

bool checkFileNamePattern(const QString &pattern, QString *problem) {
    if (pattern.trimmed().isEmpty()) {
        *problem = QStringLiteral("needs a pattern like \"{date}_{time}_{app}\"");
        return false;
    }
    if (pattern.contains('/')) {
        *problem = QStringLiteral("cannot contain a / (a file name has no folders)");
        return false;
    }
    for (const Piece &piece : split(pattern)) {
        if (piece.token) {
            if (!tokenNames().contains(piece.text)) {
                *problem = QStringLiteral("{%1} is not a token (use {date}, {time}, {app}, {title} or {kind})").arg(piece.text);
                return false;
            }
        } else if (piece.text.contains('{') || piece.text.contains('}')) {
            *problem = QStringLiteral("has a { or } without its pair (tokens look like {date})");
            return false;
        }
    }
    return true;
}

QString fileBaseName(const QString &pattern, const QDateTime &time, const CaptureName &name) {
    const QString date = time.toString(QStringLiteral("yyyy-MM-dd"));
    const QString clock = time.toString(QStringLiteral("HH-mm-ss"));
    auto valueOf = [&](const QString &token) -> QString {
        if (token == QLatin1String("date"))
            return date;
        if (token == QLatin1String("time"))
            return clock;
        if (token == QLatin1String("app"))
            return clean(name.app.toLower());
        if (token == QLatin1String("title"))
            return cleanTitle(name.title, name.app);
        if (token == QLatin1String("kind"))
            return clean(name.kind);
        return {};
    };

    QString out;
    bool lastWasLiteral = false, dropped = false;
    for (const Piece &piece : split(pattern)) {
        if (!piece.token) {
            if (lastWasLiteral && dropped)
                continue; // the separator on the far side of a dropped token
            out += piece.text;
            lastWasLiteral = true;
            dropped = false;
            continue;
        }
        const QString value = valueOf(piece.text);
        if (value.isEmpty()) {
            dropped = true;
            continue;
        }
        out += value;
        lastWasLiteral = false;
        dropped = false;
    }
    out = clean(out);
    return out.isEmpty() ? date + '_' + clock : out;
}

} // namespace tarn
