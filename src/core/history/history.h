#pragma once
// The History store: every image capture's untouched original, kept as a plain
// folder (~/.local/share/tarn/history) of <name>.png + <name>.json pairs that a
// file manager can browse, plus an optional <name>.edit.json Project file holding
// the user's edits (ADR 0003; document/project_file.h). The files of one id are
// one group: the sweep and the Trash move them together. Never written for Text capture (Text capture simply
// never calls record(); it must stay that way).
#include "storage/file_name.h"

#include <QDateTime>
#include <QFuture>
#include <QImage>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <functional>
#include <memory>
#include <vector>

namespace tarn {

class CaptureDocument;
class Daemon;

// What a capture tells History about itself.
struct CaptureInfo {
    QDateTime time;
    QString kind;      // "fullscreen" (the whole monitor), "area" or "window"
    QString app;       // window captures only
    QString title;     // window captures only
    QRect position;    // where on screen the capture was taken
};

class History : public QObject {
    Q_OBJECT
public:
    // The kind a Description's `kind` stands for: v1 wrote "timer" for Timer shots
    // (always an area), and Descriptions are never migrated.
    static QString normalizedKind(const QString &kind) { return kind == QLatin1String("timer") ? QStringLiteral("area") : kind; }
    explicit History(Daemon &daemon);
    ~History() override;

    // <XDG_DATA_HOME>/tarn/history
    static QString dir();
    // Entries captured before this are expired (the "Keep History for" Setting);
    // not valid when History is kept forever.
    QDateTime retentionCutoff(const QDateTime &now) const;

    // Keeps the untouched original. The pair is written in the background; the
    // returned id (the file name without extension, named by capture time) is
    // valid at once.
    QString record(const QByteArray &png, const CaptureInfo &info);
    // The same for a PNG still being made (a full-screen shot, encodePngInBackground):
    // the pair is written once it is ready, still in the order asked for.
    QString record(const QFuture<QByteArray> &png, const CaptureInfo &info);

    // Adds a path the capture was later saved to, to its description.
    void addSavedPath(const QString &id, const QString &path);

    // The one way a finished picture is written out by the user's choice. Both notify
    // ("Tarn could not save the screenshot") and return false / "" on failure, and
    // record the path in the entry's Description when `id` is not empty.
    // saveImage: into the screenshots folder under the File name pattern (the path comes back).
    // saveImageAs: to `path` (PNG, JPEG or WebP by its extension), remembering its folder
    // as the Save as folder.
    QString saveImage(const QString &id, const QByteArray &png, const QDateTime &captureTime, const CaptureName &name);
    bool saveImageAs(const QString &id, const QByteArray &png, const QString &path);

    // Removes entries older than the retention, by capture time. Normally that is
    // permanent; but entries that expire only because the retention was SHORTENED
    // (they were still inside the retention the last sweep applied) go to the
    // Trash. Returns how many left History. Runs at start-up, every 6 hours, and
    // when the Setting changes.
    int sweep();

    // The Trash (freedesktop: <XDG_DATA_HOME>/Trash). Both return success.
    bool trash(const QString &id);
    bool restore(const QString &id);

    // The ids of the entries on disk, oldest capture first.
    QStringList ids();

    // ---- The Project file ----
    // Saves the document's edits beside the original, or, when nothing is on the
    // picture, deletes the Project file. An unreadable Project file that is already
    // there goes to the Trash first. Done in the background, in order.
    void saveProject(const QString &id, const CaptureDocument &document);
    // What the original was edited into, rebuilt on a fresh document of `original`.
    // None: there is no Project file (document is null). Unreadable: the file is
    // damaged; the document is null (the caller shows the original) and ONE
    // notification names the file. Loaded: a Frame whose wallpaper file is gone has
    // fallen back to a blurred copy of the shot, with one notification.
    enum class ProjectState { None, Loaded, Unreadable };
    struct LoadedProject {
        ProjectState state = ProjectState::None;
        std::unique_ptr<CaptureDocument> document;
    };
    LoadedProject loadProject(const QString &id, const QImage &original);
    // Whether the entry has a Project file (hasProject waits for pending writes first).
    bool isEdited(const QString &id) const;
    bool hasProject(const QString &id);
    // The words in the entry's Project file (Text annotations, Callouts); "" when none.
    QString projectText(const QString &id) const;
    // The finished picture of an entry with edits (its Project file rebuilt on the
    // original and flattened, PNG bytes). Empty when there are no edits or the
    // Project file is unreadable (then one notification, and the original is the
    // picture). What Copy, Pin, Save as and Restore hand out.
    QByteArray editedPng(const QString &id);
    QByteArray originalPng(const QString &id);
    // The entry's Description as it is on disk (empty if there is none).
    QJsonObject description(const QString &id);
    // What the File name pattern needs of the entry (kind, app, window title).
    CaptureName captureName(const QString &id);
    // The File name pattern applied to the entry, with the Image format's extension
    // (what Save as suggests, in the folder of the user's choice).
    QString saveAsFileName(const QString &id);
    // Revert to original: only the Project file goes to the Trash.
    bool revertProject(const QString &id);

    // Writes the Description's `text` (the Text reader's result; "" = nothing
    // found) in the background, then emits textRead(id).
    void setText(const QString &id, const QString &text);

    // Waits until every background write has finished.
    void flush();

    // The sweep skips an entry while any registered check says it is in use (open in
    // an Editor, or on the Thumbnail stack); it takes it on its first run after.
    void addBusyCheck(std::function<bool(const QString &id)> check) { m_busyChecks.push_back(std::move(check)); }
    bool isBusy(const QString &id) const;

    QString pngPath(const QString &id) const { return dir() + '/' + id + QStringLiteral(".png"); }
    QString descriptionPath(const QString &id) const { return dir() + '/' + id + QStringLiteral(".json"); }
    QString projectPath(const QString &id) const { return dir() + '/' + id + QStringLiteral(".edit.json"); }

signals:
    // Change events for the History window. They carry the entry's Description,
    // so a listener never has to read the disk. Added and changed arrive once the
    // files are written (on the event loop); removed arrives when the entry left
    // History (sweep, Delete to the Trash).
    void entryAdded(const QString &id, const QJsonObject &description);
    void entryChanged(const QString &id, const QJsonObject &description);
    void entryRemoved(const QString &id);
    void textRead(const QString &id); // the Description now has `text`

private:
    Daemon &m_daemon;
    QThreadPool m_pool; // one thread: writes happen in the order they were asked for
    QTimer m_sweepTimer;
    QMutex m_mutex;
    std::vector<std::function<bool(const QString &)>> m_busyChecks;
    QSet<QString> m_reserved; // ids handed out whose files may not exist yet
};

} // namespace tarn
