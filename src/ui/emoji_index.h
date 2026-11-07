#pragma once
// The emoji picker's data and search: 1715 emoji with a name and search
// words each (assets/emoji.tsv, from GitHub's gemoji, MIT; see THIRD-PARTY.md).
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <vector>

namespace tarn {

class EmojiIndex : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ count CONSTANT)
public:
    explicit EmojiIndex(QObject *parent = nullptr);
    int count() const { return int(m_entries.size()); }
    // Entries as {emoji, name}: every word of `query` must start a word of the
    // emoji's name or search words (best matches first). An empty query lists
    // them all, in the usual order (smileys first). At most `limit`.
    Q_INVOKABLE QVariantList search(const QString &query, int limit = 200) const;

private:
    struct Entry {
        QString emoji;
        QString name;
        QString words; // lower case, space separated: name then aliases and tags
    };
    std::vector<Entry> m_entries;
};

} // namespace tarn
