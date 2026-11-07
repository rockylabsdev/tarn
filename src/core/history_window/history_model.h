#pragma once
// The list model the History window's grid binds to. It reads every Description
// once (reload(), when the window opens), then follows the History store's
// change events and never looks at the disk again.
//
// Rows are a flat list: a day heading row, then that day's captures (newest day
// first, newest capture first). Selection lives here, so arrow keys can be tested
// without QML.
#include <QAbstractListModel>
#include <QDateTime>
#include <QJsonObject>
#include <QSet>
#include <QSize>
#include <QVariantList>
#include <vector>

namespace tarn {

class Daemon;
class History;

class HistoryModel : public QAbstractListModel {
    Q_OBJECT
    // All captures in History (0 = the empty state), and those the filters and
    // search leave.
    Q_PROPERTY(int captureCount READ captureCount NOTIFY changed)
    Q_PROPERTY(int shownCount READ shownCount NOTIFY changed)
    // The top bar's count: "N captures", or "N captures · reading M" while a search
    // is typed and M captures the Text reader has not reached could still match.
    Q_PROPERTY(QString countText READ countText NOTIFY changed)
    // Search (empty each time the window opens), Kind filter ("" = All, "area",
    // "window", "fullscreen") and app filter ("" = All apps).
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filtersChanged)
    Q_PROPERTY(QString kindFilter READ kindFilter WRITE setKindFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString appFilter READ appFilter WRITE setAppFilter NOTIFY filtersChanged)
    // [{name, count}] most used first.
    Q_PROPERTY(QVariantList apps READ apps NOTIFY changed)
    Q_PROPERTY(QString searchPlaceholder READ searchPlaceholder NOTIFY changed)
    Q_PROPERTY(bool nothingMatches READ nothingMatches NOTIFY changed)
    Q_PROPERTY(QString nothingMatchesText READ nothingMatchesText NOTIFY changed)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    // How many entries are selected, and whether it is exactly one (single-entry
    // actions are grayed otherwise). selectedId is that one entry, "" otherwise.
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(bool singleSelected READ singleSelected NOTIFY selectionChanged)
    // The bottom bar: "app · window title · width × height", or "Select a capture".
    Q_PROPERTY(QString summary READ summary NOTIFY selectionChanged)
    // Whether the one selected entry carries edits (a Project file): Revert to original is live.
    Q_PROPERTY(bool selectedEdited READ selectedEdited NOTIFY selectionChanged)
public:
    enum Role { IdRole = Qt::UserRole + 1, HeadingRole, DayRole, AppRole, TitleRole, TimeRole, KindRole, WidthRole, HeightRole, SelectedRole, PathRole, EditedRole, RevisionRole };
    Q_ENUM(Role)

    explicit HistoryModel(Daemon &daemon, QObject *parent = nullptr);

    int rowCount(const QModelIndex & = {}) const override { return int(m_rows.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Reads every Description in History (a kept PNG's size comes from its header).
    void reload();

    int captureCount() const { return int(m_entries.size()); }
    int shownCount() const { return int(m_shown.size()); }
    QString countText() const;
    QString search() const { return m_search; }
    QString kindFilter() const { return m_kind; }
    QString appFilter() const { return m_app; }
    void setSearch(const QString &search);
    void setKindFilter(const QString &kind);
    void setAppFilter(const QString &app);
    QVariantList apps() const;
    QString searchPlaceholder() const;
    bool nothingMatches() const { return !m_entries.empty() && m_shown.empty(); }
    QString nothingMatchesText() const;
    // The "Show everything" button: clears search and filters.
    Q_INVOKABLE void showEverything();
    QString selectedId() const { return m_chosen.size() == 1 ? m_selected : QString(); }
    int selectedCount() const { return int(m_chosen.size()); }
    bool singleSelected() const { return m_chosen.size() == 1; }
    // The selected ids in grid order (newest first).
    QStringList selectedIds() const;
    QString summary() const;
    bool selectedEdited() const;
    // Whether an entry carries edits; follows the Project file as it is saved or reverted.
    Q_INVOKABLE bool isEdited(const QString &id) const;

    Q_INVOKABLE void select(const QString &id);
    Q_INVOKABLE void clearSelection();
    // Ctrl-click: adds the entry to the selection, or takes it out again.
    Q_INVOKABLE void toggle(const QString &id);
    // Shift-click: selects everything from the last plain or Ctrl-clicked entry to this one.
    Q_INVOKABLE void extendTo(const QString &id);
    // Ctrl+A.
    Q_INVOKABLE void selectAll();

    // ---- Actions. They act on the finished picture and never close the window. ----
    // Copy, Pin, Save as and Show in Files need exactly one selected entry (false otherwise).
    Q_INVOKABLE bool copy();
    // Always a fresh Pin.
    Q_INVOKABLE bool pin();
    Q_INVOKABLE bool saveAs(const QString &path);
    // The folder the Save as dialog starts in (the last one used).
    Q_INVOKABLE QString saveAsFolder() const;
    // Folder + File name pattern + Image format extension, and the format's place in
    // the dialog's filters (0 PNG, 1 JPEG, 2 WebP): the same as the Editor's dialog.
    Q_INVOKABLE QString saveAsSuggestion() const;
    Q_INVOKABLE int saveAsFilterIndex() const;
    Q_INVOKABLE bool showInFiles();
    // Edit (Enter, double-click, the hover button): opens the Editor on the one selected
    // entry, rebuilt from its Project file; an Editor already open on it comes forward.
    Q_INVOKABLE bool edit();
    // Revert to original (edited entries only): only the Project file goes to the Trash.
    Q_INVOKABLE bool revertToOriginal();
    // Restore: up to 4 of the selected entries come back as Thumbnails, skipping those
    // already on the stack. Returns how many came back.
    Q_INVOKABLE int restore();
    // Delete: the whole group of every selected entry goes to the Trash. Returns how many went.
    Q_INVOKABLE int trashSelected();
    // "left", "right", "up" or "down" in a grid `columns` tiles wide (columns count
    // from the start of each day). With nothing selected the first capture is selected.
    Q_INVOKABLE void move(const QString &direction, int columns);

signals:
    void changed();
    void selectionChanged();
    void filtersChanged();

private:
    struct Entry {
        QString id;
        QDateTime time;
        QString kind, app, title;
        QSize size;
        QString text;           // the Description's recognized text
        bool textRead = false;  // the Description has `text` (even "")
        QString annotationText; // Text annotations and Callouts, from the Project file
        bool edited = false;
        int revision = 0; // changes with every saved edit, so a tile reloads its picture
    };
    struct Row {
        bool heading;
        int entry; // index into m_entries (-1 while a removed row is on its way out)
        QString day;
        QString key; // "#<date>" for a heading, the entry id for a tile: what stays the same row
    };
    Entry makeEntry(const QString &id, const QJsonObject &description, const QSize &known = {}) const;
    void sortAndRebuild();
    bool applyRowsInPlace(std::vector<Row> &rows); // only removals or only insertions: no reset
    bool passesFilters(const Entry &e) const;
    bool matchesSearch(const Entry &e) const;
    QString dayLabel(const QDateTime &t) const;
    int entryIndex(const QString &id) const;
    void setSelected(const QString &id);
    void setChosen(const QSet<QString> &chosen, const QString &cursor);

    Daemon &m_daemon;
    std::vector<Entry> m_entries; // newest first
    std::vector<int> m_shown;     // indices into m_entries that filters and search leave
    QString m_search, m_kind, m_app;
    std::vector<Row> m_rows;
    QString m_selected;      // the cursor: the last entry selected, where arrows start from
    QString m_anchor;        // where a Shift-click range starts
    QSet<QString> m_chosen;  // everything selected (contains m_selected)
};

} // namespace tarn
