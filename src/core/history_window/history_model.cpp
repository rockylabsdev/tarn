#include "history_model.h"

#include <QHash>

#include "daemon/daemon.h"
#include "editor/editor.h"
#include "clipboard/clipboard.h"
#include "history/history.h"
#include "notify/notifier.h"
#include "process/process_runner.h"
#include "pin/pins.h"
#include "state/remembered_state.h"
#include "storage/image_file.h"
#include "thumbnails/thumbnail_stack.h"

#include <QFile>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QImageReader>
#include <QJsonDocument>
#include <QFileInfo>
#include <QLocale>
#include <algorithm>

namespace tarn {

HistoryModel::HistoryModel(Daemon &daemon, QObject *parent) : QAbstractListModel(parent), m_daemon(daemon) {
    auto &history = daemon.service<History>();
    connect(&history, &History::entryAdded, this, [this](const QString &id, const QJsonObject &d) {
        const int existing = entryIndex(id);
        if (existing >= 0)
            m_entries.erase(m_entries.begin() + existing);
        m_entries.push_back(makeEntry(id, d));
        sortAndRebuild();
    });
    connect(&history, &History::entryChanged, this, [this](const QString &id, const QJsonObject &d) {
        const int existing = entryIndex(id);
        if (existing < 0)
            return;
        const int revision = m_entries[existing].revision + 1;
        m_entries[existing] = makeEntry(id, d, m_entries[existing].size);
        m_entries[existing].revision = revision;
        sortAndRebuild();
        if (m_chosen.contains(id))
            emit selectionChanged(); // the summary's Edited, Revert's enabled state
    });
    connect(&history, &History::entryRemoved, this, [this](const QString &id) {
        const int existing = entryIndex(id);
        if (existing < 0)
            return;
        m_entries.erase(m_entries.begin() + existing);
        if (m_chosen.contains(id)) {
            m_chosen.remove(id);
            if (m_selected == id)
                m_selected = m_chosen.isEmpty() ? QString() : selectedIds().value(0);
            if (m_anchor == id)
                m_anchor = m_selected;
            emit selectionChanged();
        }
        sortAndRebuild();
    });
}

static QJsonObject readDescription(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

HistoryModel::Entry HistoryModel::makeEntry(const QString &id, const QJsonObject &d, const QSize &known) const {
    Entry e;
    e.id = id;
    e.time = QDateTime::fromString(d.value(QStringLiteral("captureTime")).toString(), Qt::ISODate);
    e.kind = History::normalizedKind(d.value(QStringLiteral("kind")).toString());
    e.app = d.value(QStringLiteral("app")).toString();
    e.title = d.value(QStringLiteral("title")).toString();
    e.textRead = d.contains(QStringLiteral("text"));
    e.text = d.value(QStringLiteral("text")).toString();
    // Text annotations and Callouts are read from the Project file, never OCR'd.
    auto &history = m_daemon.service<History>();
    e.annotationText = history.projectText(id);
    e.edited = history.isEdited(id);
    e.size = known;
    if (!e.size.isValid())
        e.size = QImageReader(m_daemon.service<History>().pngPath(id)).size();
    return e;
}

void HistoryModel::reload() {
    auto &history = m_daemon.service<History>();
    m_entries.clear();
    for (const QString &id : history.ids())
        m_entries.push_back(makeEntry(id, readDescription(history.descriptionPath(id))));
    QSet<QString> still;
    for (const QString &id : std::as_const(m_chosen))
        if (entryIndex(id) >= 0)
            still.insert(id);
    if (still != m_chosen) {
        m_chosen = still;
        if (!m_chosen.contains(m_selected))
            m_selected = m_chosen.isEmpty() ? QString() : *m_chosen.cbegin();
        m_anchor = m_selected;
        emit selectionChanged();
    }
    sortAndRebuild();
}

// Turns m_rows into `rows` with row removals, or with row insertions, when that's all
// that changed; false (nothing done) otherwise. Rows that stay are first pointed at
// their entries' new places, so the view reads the right entry at every step.
bool HistoryModel::applyRowsInPlace(std::vector<Row> &rows) {
    auto isSubsequence = [](const std::vector<Row> &small, const std::vector<Row> &big) {
        size_t j = 0;
        for (size_t i = 0; i < big.size() && j < small.size(); ++i)
            if (big[i].key == small[j].key)
                ++j;
        return j == small.size();
    };
    const bool removals = rows.size() < m_rows.size() && isSubsequence(rows, m_rows);
    const bool insertions = rows.size() > m_rows.size() && isSubsequence(m_rows, rows);
    if (!removals && !insertions) {
        if (rows.size() != m_rows.size() || !isSubsequence(rows, m_rows))
            return false;
        m_rows = std::move(rows); // the same rows: nothing to add or take away
        if (!m_rows.empty())
            emit dataChanged(index(0), index(int(m_rows.size()) - 1)); // a description may have changed (an edit's pencil)
        return true;
    }
    QHash<QString, int> entryOf;
    for (const Row &r : rows)
        entryOf.insert(r.key, r.entry);
    for (Row &r : m_rows)
        r.entry = entryOf.value(r.key, -1);
    if (removals) {
        size_t j = 0;
        std::vector<int> gone;
        for (int i = 0; i < int(m_rows.size()); ++i) {
            if (j < rows.size() && m_rows[i].key == rows[j].key)
                ++j;
            else
                gone.push_back(i);
        }
        for (auto it = gone.rbegin(); it != gone.rend(); ++it) {
            beginRemoveRows({}, *it, *it);
            m_rows.erase(m_rows.begin() + *it);
            endRemoveRows();
        }
    } else {
        for (int i = 0; i < int(rows.size()); ++i) { // the old rows are in order among the new
            if (i < int(m_rows.size()) && m_rows[i].key == rows[i].key)
                continue;
            beginInsertRows({}, i, i);
            m_rows.insert(m_rows.begin() + i, rows[i]);
            endInsertRows();
        }
    }
    m_rows = std::move(rows); // same rows, same order: refreshes day labels and entry places
    if (!m_rows.empty())
        emit dataChanged(index(0), index(int(m_rows.size()) - 1)); // a tile keeps its picture unless its source changed
    return true;
}

QString HistoryModel::dayLabel(const QDateTime &t) const {
    const QDate today = m_daemon.now().date();
    if (t.date() == today)
        return QStringLiteral("Today");
    if (t.date() == today.addDays(-1))
        return QStringLiteral("Yesterday");
    return QLocale::c().toString(t.date(), QStringLiteral("dddd d MMMM"));
}

void HistoryModel::sortAndRebuild() {
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b) {
        return a.time != b.time ? a.time > b.time : a.id > b.id;
    });
    if (!m_app.isEmpty() && std::none_of(m_entries.begin(), m_entries.end(), [this](const Entry &e) { return e.app == m_app; })) {
        m_app.clear(); // the app has left History
        emit filtersChanged();
    }
    std::vector<Row> rows;
    std::vector<int> shown;
    QDate day;
    for (int i = 0; i < int(m_entries.size()); ++i) {
        if (!passesFilters(m_entries[i]) || !matchesSearch(m_entries[i]))
            continue;
        if (shown.empty() || m_entries[i].time.date() != day) {
            day = m_entries[i].time.date();
            rows.push_back({true, i, dayLabel(m_entries[i].time), QLatin1Char('#') + day.toString(Qt::ISODate)});
        }
        shown.push_back(i);
        rows.push_back({false, i, dayLabel(m_entries[i].time), m_entries[i].id});
    }
    m_shown = std::move(shown);
    // Delete and a new capture touch only their own rows: the grid keeps every other tile
    // and its picture. Anything else (filters, search) rebuilds the lot.
    if (!applyRowsInPlace(rows)) {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }
    // What the filters and search hide cannot stay selected.
    QSet<QString> visible;
    for (int i : m_shown)
        visible.insert(m_entries[i].id);
    const QSet<QString> kept = m_chosen & visible;
    if (kept != m_chosen) {
        m_chosen = kept;
        if (!m_chosen.contains(m_selected))
            m_selected = m_chosen.isEmpty() ? QString() : selectedIds().value(0);
        if (!m_chosen.contains(m_anchor))
            m_anchor = m_selected;
        emit selectionChanged();
    }
    emit changed();
}

bool HistoryModel::passesFilters(const Entry &e) const {
    return (m_kind.isEmpty() || e.kind == m_kind) && (m_app.isEmpty() || e.app == m_app);
}

// Every typed word must appear somewhere in the recognized text, the Text
// annotations and Callouts, the title or the app; case ignored; a word may sit
// inside a longer word; nothing fuzzy.
bool HistoryModel::matchesSearch(const Entry &e) const {
    const QStringList words = m_search.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString &word : words) {
        if (!e.text.contains(word, Qt::CaseInsensitive) && !e.annotationText.contains(word, Qt::CaseInsensitive) &&
            !e.title.contains(word, Qt::CaseInsensitive) && !e.app.contains(word, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

void HistoryModel::setSearch(const QString &search) {
    if (search == m_search)
        return;
    m_search = search;
    emit filtersChanged();
    sortAndRebuild();
}

void HistoryModel::setKindFilter(const QString &kind) {
    if (kind == m_kind)
        return;
    m_kind = kind;
    emit filtersChanged();
    sortAndRebuild();
}

void HistoryModel::setAppFilter(const QString &app) {
    if (app == m_app)
        return;
    m_app = app;
    emit filtersChanged();
    sortAndRebuild();
}

void HistoryModel::showEverything() {
    m_search.clear();
    m_kind.clear();
    m_app.clear();
    emit filtersChanged();
    sortAndRebuild();
}

QVariantList HistoryModel::apps() const {
    QMap<QString, int> counts;
    for (const Entry &e : m_entries)
        if (!e.app.isEmpty())
            ++counts[e.app];
    QList<QPair<QString, int>> sorted;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        sorted.append({it.key(), it.value()});
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    QVariantList out;
    for (const auto &[name, count] : sorted)
        out << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("count"), count}};
    return out;
}

QString HistoryModel::countText() const {
    const int n = shownCount();
    QString text = n == 1 ? QStringLiteral("1 capture") : QStringLiteral("%1 captures").arg(n);
    if (m_search.trimmed().isEmpty() || QStandardPaths::findExecutable(QStringLiteral("tesseract")).isEmpty())
        return text;
    // Captures the reader has not reached that the other filters let through and
    // the search has not already matched: they may still turn up.
    int reading = 0;
    for (const Entry &e : m_entries)
        if (!e.textRead && passesFilters(e) && !matchesSearch(e))
            ++reading;
    return reading > 0 ? text + QStringLiteral(" \u00b7 reading %1").arg(reading) : text;
}

QString HistoryModel::searchPlaceholder() const {
    return QStandardPaths::findExecutable(QStringLiteral("tesseract")).isEmpty() ? QStringLiteral("Install tesseract to search text in pictures")
                                                                                  : QStringLiteral("Search");
}

QString HistoryModel::nothingMatchesText() const {
    const QString search = m_search.trimmed();
    return search.isEmpty() ? QStringLiteral("Nothing matches") : QStringLiteral("Nothing matches \u201c%1\u201d").arg(search);
}

int HistoryModel::entryIndex(const QString &id) const {
    for (int i = 0; i < int(m_entries.size()); ++i)
        if (m_entries[i].id == id)
            return i;
    return -1;
}

QHash<int, QByteArray> HistoryModel::roleNames() const {
    return {{IdRole, "entryId"},         {HeadingRole, "heading"}, {DayRole, "day"},       {AppRole, "app"},       {TitleRole, "windowTitle"},
            {TimeRole, "time"},     {KindRole, "kind"},       {WidthRole, "pixelWidth"},   {HeightRole, "pixelHeight"}, {SelectedRole, "selected"},
            {PathRole, "path"},
            {EditedRole, "edited"}, {RevisionRole, "revision"}};
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rowCount())
        return {};
    const Row &row = m_rows[index.row()];
    if (role == HeadingRole)
        return row.heading;
    if (role == DayRole)
        return row.day;
    if (row.heading || row.entry < 0)
        return {};
    const Entry &e = m_entries[row.entry];
    switch (role) {
    case IdRole: return e.id;
    case AppRole: return e.app;
    case TitleRole: return e.title;
    case TimeRole: return e.time.toString(QStringLiteral("HH:mm"));
    case KindRole: return e.kind;
    case WidthRole: return e.size.width();
    case HeightRole: return e.size.height();
    case SelectedRole: return m_chosen.contains(e.id);
    case PathRole: return m_daemon.service<History>().pngPath(e.id);
    case EditedRole: return e.edited;
    case RevisionRole: return e.revision;
    }
    return {};
}

QString HistoryModel::summary() const {
    if (m_chosen.size() > 1)
        return QStringLiteral("%1 selected").arg(m_chosen.size());
    const int i = entryIndex(m_selected);
    if (m_chosen.isEmpty() || i < 0)
        return QStringLiteral("Select a capture");
    const Entry &e = m_entries[i];
    QStringList parts;
    if (!e.app.isEmpty())
        parts << e.app;
    if (!e.title.isEmpty())
        parts << e.title;
    if (e.size.isValid())
        parts << QStringLiteral("%1 × %2").arg(e.size.width()).arg(e.size.height());
    if (e.edited)
        parts << QStringLiteral("Edited");
    return parts.join(QStringLiteral(" · "));
}

bool HistoryModel::isEdited(const QString &id) const {
    const int i = entryIndex(id);
    return i >= 0 && m_entries[i].edited;
}

bool HistoryModel::selectedEdited() const { return singleSelected() && isEdited(m_selected); }

void HistoryModel::setChosen(const QSet<QString> &chosen, const QString &cursor) {
    if (chosen == m_chosen && cursor == m_selected)
        return;
    const QSet<QString> changed = (chosen - m_chosen) + (m_chosen - chosen);
    m_chosen = chosen;
    m_selected = cursor;
    for (int r = 0; r < rowCount(); ++r)
        if (!m_rows[r].heading && changed.contains(m_entries[m_rows[r].entry].id))
            emit dataChanged(index(r), index(r), {SelectedRole});
    emit selectionChanged();
}

// A plain selection: just this entry, and a new anchor for Shift-click.
void HistoryModel::setSelected(const QString &id) {
    m_anchor = id;
    setChosen(id.isEmpty() ? QSet<QString>() : QSet<QString>{id}, id);
}

void HistoryModel::select(const QString &id) { setSelected(entryIndex(id) >= 0 ? id : QString()); }
void HistoryModel::clearSelection() { setSelected({}); }

QStringList HistoryModel::selectedIds() const {
    QStringList out;
    for (const Entry &e : m_entries)
        if (m_chosen.contains(e.id))
            out << e.id;
    return out;
}

void HistoryModel::toggle(const QString &id) {
    if (entryIndex(id) < 0)
        return;
    QSet<QString> chosen = m_chosen;
    QString cursor = id;
    if (chosen.contains(id)) {
        chosen.remove(id);
        cursor = chosen.isEmpty() ? QString() : (chosen.contains(m_selected) ? m_selected : *chosen.cbegin());
    } else {
        chosen.insert(id);
    }
    m_anchor = cursor;
    setChosen(chosen, cursor);
}

void HistoryModel::extendTo(const QString &id) {
    auto shownIndex = [this](const QString &wanted) {
        for (int i = 0; i < int(m_shown.size()); ++i)
            if (m_entries[m_shown[i]].id == wanted)
                return i;
        return -1;
    };
    const int to = shownIndex(id);
    if (to < 0)
        return;
    const int from = shownIndex(m_anchor);
    if (from < 0) {
        setSelected(id);
        return;
    }
    QSet<QString> chosen;
    for (int i = std::min(from, to); i <= std::max(from, to); ++i)
        chosen.insert(m_entries[m_shown[i]].id);
    setChosen(chosen, id); // the anchor stays
}

void HistoryModel::selectAll() {
    if (m_shown.empty())
        return;
    QSet<QString> all;
    for (int i : m_shown)
        all.insert(m_entries[i].id);
    const QString first = m_entries[m_shown.front()].id;
    if (m_anchor.isEmpty() || !all.contains(m_anchor))
        m_anchor = first;
    setChosen(all, all.contains(m_selected) ? m_selected : first);
}

void HistoryModel::move(const QString &direction, int columns) {
    if (m_shown.empty())
        return;
    auto E = [this](int i) -> const Entry & { return m_entries[m_shown[i]]; };
    int current = -1;
    for (int i = 0; !m_chosen.isEmpty() && i < int(m_shown.size()); ++i)
        if (E(i).id == m_selected)
            current = i;
    if (current < 0) {
        setSelected(E(0).id);
        return;
    }
    columns = std::max(1, columns);
    const int n = int(m_shown.size());
    // The first and last entry of the day an entry belongs to.
    auto dayRange = [&](int i) {
        const QDate day = E(i).time.date();
        int first = i, last = i;
        while (first > 0 && E(first - 1).time.date() == day)
            --first;
        while (last + 1 < n && E(last + 1).time.date() == day)
            ++last;
        return std::pair<int, int>(first, last);
    };
    int target = current;
    if (direction == QLatin1String("left")) {
        target = std::max(0, current - 1);
    } else if (direction == QLatin1String("right")) {
        target = std::min(n - 1, current + 1);
    } else {
        const auto [first, last] = dayRange(current);
        const int k = current - first, col = k % columns;
        if (direction == QLatin1String("down")) {
            if ((k / columns + 1) * columns <= last - first) {
                target = std::min(first + (k / columns + 1) * columns + col, last);
            } else if (last + 1 < n) { // the next (older) day's first row
                const int nextLast = dayRange(last + 1).second;
                target = std::min({last + 1 + col, nextLast, last + columns});
            }
        } else if (direction == QLatin1String("up")) {
            if (k >= columns) {
                target = first + (k / columns - 1) * columns + col;
            } else if (first > 0) { // the previous (newer) day's last row
                const auto [prevFirst, prevLast] = dayRange(first - 1);
                const int rowStart = prevFirst + ((prevLast - prevFirst) / columns) * columns;
                target = std::min(rowStart + col, prevLast);
            }
        }
    }
    setSelected(E(target).id);
}

// ---- Actions ----

// What an action on one entry hands out: the finished picture (the edits flattened on
// the original, or the original when there are none).
static ThumbnailStack::Capture captureOf(History &history, const QString &id) {
    ThumbnailStack::Capture capture;
    capture.png = history.originalPng(id);
    capture.edited = history.editedPng(id);
    const QJsonObject d = history.description(id);
    capture.captureTime = QDateTime::fromString(d.value(QStringLiteral("captureTime")).toString(), Qt::ISODate);
    capture.name = history.captureName(id);
    capture.historyId = id;
    return capture;
}

bool HistoryModel::copy() {
    if (!singleSelected())
        return false;
    const auto capture = captureOf(m_daemon.service<History>(), m_selected);
    QString error = QStringLiteral("its picture could not be read");
    if (capture.isNull() || m_daemon.service<Clipboard>().copyPng(capture.current(), capture.captureTime, &error, capture.name).isEmpty()) {
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not copy the screenshot"), error);
        return false;
    }
    return true;
}

bool HistoryModel::pin() {
    if (!singleSelected())
        return false;
    const auto capture = captureOf(m_daemon.service<History>(), m_selected);
    if (m_daemon.service<Pins>().pinCapture(capture) >= 0)
        return true;
    m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not pin the screenshot"), QStringLiteral("its picture could not be read"));
    return false;
}

QString HistoryModel::saveAsFolder() const { return m_daemon.service<ThumbnailStack>().saveAsFolder(); }

QString HistoryModel::saveAsSuggestion() const {
    return m_selected.isEmpty() ? saveAsFolder() : saveAsFolder() + '/' + m_daemon.service<History>().saveAsFileName(m_selected);
}

int HistoryModel::saveAsFilterIndex() const { return m_daemon.service<ThumbnailStack>().saveAsFilterIndex(); }

bool HistoryModel::saveAs(const QString &path) {
    if (!singleSelected() || path.isEmpty())
        return false;
    auto &history = m_daemon.service<History>();
    const auto capture = captureOf(history, m_selected);
    if (capture.isNull()) {
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not save the screenshot"), QStringLiteral("its picture could not be read"));
        return false;
    }
    return history.saveImageAs(m_selected, capture.current(), path);
}

bool HistoryModel::showInFiles() {
    if (!singleSelected())
        return false;
    const QString folder = QFileInfo(m_daemon.service<History>().pngPath(m_selected)).absolutePath();
    QString error;
    if (m_daemon.service<ProcessRunner>().openWithDefaultApp(folder, QStringLiteral("folder"), &error))
        return true;
    m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not open the file manager"), error);
    return false;
}

bool HistoryModel::edit() {
    if (!singleSelected())
        return false;
    return m_daemon.service<Editor>().openFromHistory(m_selected) != nullptr;
}

bool HistoryModel::revertToOriginal() {
    if (!selectedEdited())
        return false;
    if (m_daemon.service<History>().revertProject(m_selected))
        return true;
    m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not revert the screenshot"), QStringLiteral("the Trash could not take its edits"));
    return false;
}

int HistoryModel::restore() { return m_daemon.service<ThumbnailStack>().restoreFromHistory(selectedIds()); }

int HistoryModel::trashSelected() {
    auto &history = m_daemon.service<History>();
    int moved = 0;
    for (const QString &id : selectedIds()) {
        if (history.trash(id)) // emits entryRemoved: the row leaves and the selection shrinks
            ++moved;
        else
            m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not delete the screenshot"), QStringLiteral("the Trash could not take it"));
    }
    return moved;
}

} // namespace tarn
