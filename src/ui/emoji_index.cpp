#include "emoji_index.h"

#include <QFile>
#include <algorithm>

namespace tarn {

EmojiIndex::EmojiIndex(QObject *parent) : QObject(parent) {
    QFile f(QStringLiteral(":/qt/qml/Tarn/Ui/assets/emoji.tsv"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    for (const QByteArray &raw : f.readAll().split('\n')) {
        const QStringList cols = QString::fromUtf8(raw).split(QLatin1Char('\t'));
        if (cols.size() < 2 || cols[0].isEmpty())
            continue;
        m_entries.push_back({cols[0], cols[1], (cols[1] + QLatin1Char(' ') + (cols.size() > 2 ? cols[2] : QString())).toLower()});
    }
}

QVariantList EmojiIndex::search(const QString &query, int limit) const {
    const QStringList terms = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    struct Hit {
        int entry;
        int score; // lower is better
    };
    std::vector<Hit> hits;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        const QString padded = QLatin1Char(' ') + m_entries[i].words;
        int score = 0;
        bool all = true;
        for (const QString &term : terms) {
            const int at = padded.indexOf(QLatin1Char(' ') + term);
            if (at < 0) {
                all = false;
                break;
            }
            score += at; // earlier in the name beats a tag
        }
        if (all)
            hits.push_back({int(i), score});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.score < b.score; });
    QVariantList out;
    for (const Hit &h : hits) {
        if (out.size() >= limit)
            break;
        out << QVariantMap{{"emoji", m_entries[h.entry].emoji}, {"name", m_entries[h.entry].name}};
    }
    return out;
}

} // namespace tarn
