#pragma once
// The Thumbnail stack: the fresh captures waiting for the user, oldest first.
// It is the model the QML Thumbnails screen shows, and it holds every rule about
// the stack:
//   - at most 4 Thumbnails on screen; a fifth capture auto-saves the oldest
//     (edits and Frame included) and closes it. If that save fails the oldest
//     stays and one notification says why;
//   - hidden while editing: a Thumbnail whose Editor is open is hidden (setHidden)
//     and does not count toward the 4. It is not a row of the model while hidden;
//   - one restore slot: whatever Thumbnail closed last, with its edits, comes
//     back with restore() (`tarn restore`), whatever closed it;
//   - Capture hide: the whole stack is off screen while Tarn takes a shot or
//     records (hideForCapture), so Thumbnails are never in a capture. Not the
//     same as hidden while editing: the model keeps its rows, only the surface goes;
//   - kinds: a Screenshot, or a Recording (a poster frame plus the kept file).
//     The auto-close Setting never applies to a Recording (autoClose refuses).
#include <QAbstractListModel>

#include <QByteArray>
#include <QDateTime>
#include <QFuture>
#include <QImage>
#include <QPointer>
#include <QRect>
#include "frame/frame.h"
#include "storage/file_name.h"
#include <optional>
#include <utility>
#include <vector>

namespace tarn {

class Daemon;
class ThumbnailStack;

// One hold of the Capture hide: the stack stays off screen while any hold lives.
// Move it to hand the hide on to the next step (the Overlay to the Countdown or
// the Recording), so the stack never shows between them. Destroying or reset()
// releases it; a hold that outlives the stack does nothing.
class CaptureHide {
public:
    CaptureHide() = default;
    CaptureHide(CaptureHide &&other) noexcept : m_stack(std::exchange(other.m_stack, nullptr)) {}
    CaptureHide &operator=(CaptureHide &&other) noexcept;
    CaptureHide(const CaptureHide &) = delete;
    CaptureHide &operator=(const CaptureHide &) = delete;
    ~CaptureHide() { reset(); }
    void reset();
    explicit operator bool() const { return !m_stack.isNull(); }

private:
    friend class ThumbnailStack;
    explicit CaptureHide(ThumbnailStack *stack) : m_stack(stack) {}
    QPointer<ThumbnailStack> m_stack;
};

class ThumbnailStack : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool captureHidden READ captureHidden NOTIFY captureHiddenChanged)
public:
    enum Role { IdRole = Qt::UserRole + 1, KindRole };
    enum Kind { Screenshot, Recording };
    Q_ENUM(Kind)
    // Why a Thumbnail left; the restore slot remembers it (a Deleted one also
    // has to come back out of the Trash).
    enum CloseReason { Closed, Copied, Saved, SavedAs, Pinned, Deleted, Overflow, AutoClosed, CloseAll };
    Q_ENUM(CloseReason)

    static constexpr int kMaxThumbnails = 4;

    explicit ThumbnailStack(Daemon &daemon);

    // Adds a capture (grim's PNG bytes, kept untouched for saving). Returns its
    // id, or -1 if the bytes are not a readable image. Over the limit, the
    // oldest Thumbnail is auto-saved first.
    // `framed = false` skips the auto-applied Preset (the Overlay's B). Otherwise,
    // with a Preset marked to apply to every capture, the Thumbnail starts out with
    // that Frame (as its edit), so every Copy, Save and overflow includes it.
    int add(const QByteArray &png, const QDateTime &captureTime, const CaptureName &name = {}, bool framed = true);
    // The same for a shot whose PNG is still being made (encodePngInBackground): the
    // card shows `image` at once. Copy / Save automatically wait for the PNG; an
    // action that needs it before then (Copy, Save, Pin, a close) waits for it, so
    // nothing ever works from a partial file.
    int add(const QImage &image, const QFuture<QByteArray> &png, const QDateTime &captureTime, const CaptureName &name = {},
            bool framed = true);
    // Adds a Recording: its poster frame (PNG), the kept video file (in the kept
    // folder, see RecordingStore) and its length. Not in History, a Pin only for a GIF, never
    // auto-closes. Save writes the file to the Recordings folder, Copy puts a
    // file on the clipboard; the kept file is deleted once the Recording can no
    // longer be restored (it leaves the restore slot).
    int addRecording(const QByteArray &posterPng, const QDateTime &captureTime, const QString &videoPath, qint64 durationMs = 0,
                      const QString &sourcePath = {}, const CaptureName &name = {});
    // (A GIF Recording's videoPath is the GIF, which Copy and Save use; sourcePath is
    // the MP4 it was made from, kept beside it for Trim and deleted with it.)
    // The Recording chip's text: "MP4 0:06" (empty for a screenshot).
    Q_INVOKABLE QString chip(int id) const;

    // Save button: writes the capture (with its edits) to the screenshot folder
    // and closes the Thumbnail. Returns the path. On failure it raises a
    // notification, keeps the Thumbnail and returns an empty string.
    Q_INVOKABLE QString save(int id);
    // Save as…: writes to `path`, the format chosen by its extension (png, jpg,
    // jpeg or webp), remembers the folder, closes the Thumbnail. On failure it
    // notifies and keeps it.
    Q_INVOKABLE bool saveAs(int id, const QString &path);
    // The folder the Save as… dialog starts in: the last one used, else the screenshot folder.
    Q_INVOKABLE QString saveAsFolder() const;
    // Save as…: the full path the dialog starts on (last folder + the File name pattern
    // + the Image format's extension), and which type (0 PNG, 1 JPEG, 2 WebP) it preselects.
    Q_INVOKABLE QString saveAsSuggestion(int id) const;
    Q_INVOKABLE int saveAsFilterIndex() const;
    CaptureName captureName(int id) const;
    // Copy button: puts the capture on the clipboard (see Clipboard) and closes
    // the Thumbnail, silently. On failure it notifies and keeps the Thumbnail.
    Q_INVOKABLE bool copy(int id);
    // The x button: closes without saving.
    Q_INVOKABLE void close(int id);
    // Right-click Delete: History's original goes to the Trash and the Thumbnail
    // closes. If the Trash move fails it notifies and keeps the Thumbnail.
    Q_INVOKABLE bool trash(int id);
    // Right-click Close all: every Thumbnail (hidden ones too) closes without saving.
    Q_INVOKABLE void closeAll();
    // Everything a Pin (or a Thumbnail brought back from one) needs of a capture.
    struct Capture {
        QByteArray png;     // as captured
        QByteArray edited;  // flattened edits, empty if none
        QDateTime captureTime;
        CaptureName name;
        QString historyId;
        QRect origin;       // where it was captured: Hyprland layout, logical px (empty if unknown)
        QByteArray current() const { return edited.isEmpty() ? png : edited; }
        bool isNull() const { return png.isEmpty(); }
    };
    // Pin: takes the capture away from the stack (closing it, reason Pinned) and
    // hands it over. Null (and nothing closes) for a Recording or an unknown id.
    // (A GIF Recording's Pin leaves the Thumbnail in place: Pins reads videoPath.)
    Capture pin(int id);
    // The Pin's Edit: puts a capture back in the stack as a Thumbnail (no automatic
    // Copy / Save / auto-close: it was already on screen). Returns its id.
    int adopt(const Capture &capture);
    // Restore from the History window: brings back up to kMaxThumbnails of the given
    // History entries (in the order given, the first ones win) as Thumbnails with their
    // edits, skipping entries already in the stack (hidden ones too). They enter oldest
    // first, so the stack's overflow rule applies as to any new Thumbnail. No automatic
    // Copy / Save / auto-close. Returns how many came back.
    int restoreFromHistory(const QStringList &historyIds);
    // Whether a Thumbnail (hidden or not) is this History entry.
    bool holdsHistoryEntry(const QString &historyId) const;
    // The id of the Thumbnail (hidden or not) that is this History entry, or -1.
    int idForHistory(const QString &historyId) const;
    // Pin on a Thumbnail (its corner button): asks the Pins module to pin it.
    Q_INVOKABLE void requestPin(int id);
    // False for an MP4 Recording (only screenshots and GIF Recordings have a Pin): no Pin button.
    Q_INVOKABLE bool pinnable(int id) const;
    // Where the capture was taken, set by the capture right after add().
    void setOrigin(int id, const QRect &layoutRect);
    QRect origin(int id) const;
    // The monitor the layer surface shows on: where the newest capture was taken (empty
    // until one has an origin). A layer surface has one output, so it follows the newest.
    QString monitorName() const { return m_monitorName; }
    // The auto-close Setting's timer ran out. Refused (false) for a Recording.
    bool autoClose(int id);
    // `tarn restore`: brings back the last closed Thumbnail with its edits. A
    // Deleted one is put back from the Trash first. False if nothing to restore
    // or the History entry could not be recovered (then it notifies).
    bool restore();
    bool canRestore() const { return m_slot.has_value(); }

    // Capture hide: the surface goes off screen in one frame while any hold
    // lives, and every Thumbnail springs back in together when the last one is
    // released. Thumbnails added meanwhile wait off screen too. No Thumbnail
    // auto-closes while hidden; the full delay starts again on return.
    [[nodiscard]] CaptureHide hideForCapture();
    bool captureHidden() const { return m_captureHides > 0; }
    // Before grim or the recorder runs: once the surface has been told to go, one
    // pass of the event loop gets it off screen (measured on the nested bench:
    // docs/hyprland.md). Costs nothing when no Thumbnail was on screen.
    void waitUntilOffScreen();

    // The Editor opened / closed on this Thumbnail. Hidden ones leave the
    // model (signal hidden) and do not count toward the 4; un-hiding puts it
    // back in its place (signal revealed) and may auto-save the oldest.
    void setHidden(int id, bool hidden);
    bool isHidden(int id) const;
    // The flattened result of editing (PNG bytes), used by every save and copy
    // from now on. History keeps the untouched original either way. Empty bytes
    // take the edits away again (the Thumbnail is as captured).
    void setEdited(int id, const QByteArray &png);
    // The Frame the auto-applied Preset gave this capture, until the Editor takes
    // over (setEdited): the Editor opens with it, on the original picture.
    std::optional<Frame> autoFrame(int id) const;
    // The picture as captured, whatever the edits (image() is the edited one).
    QImage originalImage(int id) const;
    // The edits set so far (empty if none).
    QByteArray edited(int id) const;
    QDateTime captureTime(int id) const;
    // The Thumbnail was clicked, or its Edit button: asks the Editor to open
    // (signal editRequested). Nothing else happens here.
    Q_INVOKABLE void edit(int id);
    Kind kind(int id) const;
    QString videoPath(int id) const;
    // A GIF Recording's kept MP4 (what Trim works on); empty for an MP4 Recording.
    QString sourcePath(int id) const;
    // A Recording's length in ms (0 for a screenshot).
    qint64 durationMs(int id) const;
    // Trim finished on a Recording's kept file: it has a new poster frame and length.
    void setRecordingTrimmed(int id, const QByteArray &posterPng, qint64 durationMs);
    // Trim's Save as…: writes the Recording's kept file to `path` (replacing a
    // file the user just confirmed), remembers the folder, closes the Thumbnail.
    // On failure it notifies and keeps it.
    bool saveRecordingAs(int id, const QString &path);

    // Links a Thumbnail to its History entry, so saving it records the path.
    // `writeProject` false: the auto-applied Preset's Project file is already
    // written (saveAutoProject, when the shot reached History).
    void setHistoryId(int id, const QString &historyId, bool writeProject = true);
    // Writes the Project file of the auto-applied Preset (and Watermark) for a shot
    // that is in History but not on the stack yet (a Sequence's), unless `framed`
    // is false or no Preset is marked. Same file setHistoryId writes for a Thumbnail.
    void saveAutoProject(const QString &historyId, const QByteArray &png, bool framed);
    QString historyId(int id) const;

    QImage image(int id) const;
    // The Thumbnails on screen (not hidden ones).
    int count() const;
    // Everything the stack holds, hidden included.
    int totalCount() const { return int(m_entries.size()); }
    Q_INVOKABLE QList<int> ids() const;

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : count(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{IdRole, "captureId"}, {KindRole, "kind"}}; }

signals:
    void countChanged();
    void monitorChanged();
    // The Capture hide began (the surface goes now) or ended (it comes back).
    void captureHiddenChanged();
    // A Thumbnail joined / left the stack. Closed is emitted at once; the QML
    // screen keeps showing the card while its exit animation runs.
    void added(int id);
    void closed(int id);
    // Hidden while editing / shown again. The screen drops the card at once
    // (no exit animation) and adds it back.
    void hidden(int id);
    void revealed(int id);
    // Click or Edit on a Thumbnail: the Editor module opens it.
    void editRequested(int id);
    // Pin on a Thumbnail: the Pins module pins it.
    void pinRequested(int id);
    // Click or Trim on a Recording's Thumbnail: the Trim module opens it.
    void trimRequested(int id);

private:
    friend class CaptureHide;
    void releaseCaptureHide();

    struct Entry {
        int id;
        QByteArray png; // grim's bytes, or a Recording's poster
        QImage image;
        QDateTime captureTime;
        QString historyId;
        Kind kind = Screenshot;
        QString videoPath;
        QString sourcePath; // a GIF Recording: the MP4 it was made from
        qint64 durationMs = 0;
        CaptureName name; // what the File name pattern needs
        QRect origin; // where it was captured (layout, logical px)
        QByteArray edited; // flattened edits (and Frame), if any
        std::optional<Frame> autoFrame; // the auto-applied Preset's Frame, until the Editor edits
        bool hidden = false;
        QString savedPath; // written by Save automatically (or overflow) and not edited since
        int autoCloseToken = 0; // the newest auto-close timer; older ones do nothing
        bool autoCloses = false; // armed by the auto-close Setting: re-armed after a Capture hide
        QFuture<QByteArray> pendingPng; // the PNG still being made; `png` is empty until then
        QImage original;                // the shot's pixels while its PNG is being made
        bool settingsPending = false;   // Copy / Save automatically wait for the PNG
        QStringList pendingSavedPaths; // saved automatically before the History entry was linked
        QByteArray current() const { return edited.isEmpty() ? png : edited; }
    };
    struct Slot {
        Entry entry;
        CloseReason reason;
    };
    static bool isGif(const Entry &entry);
    int insert(Entry entry);
    Entry *entryFor(int id);
    // entryFor, once its PNG exists (waiting for it if it is still being made).
    Entry *settledEntry(int id);
    void settle(Entry &entry);
    void applyCopySave(Entry &entry);
    const Entry *entryFor(int id) const;
    int visibleRow(int id) const; // -1 if hidden or unknown
    void closeFor(int id, CloseReason reason);
    void enforceLimit();
    void armAutoClose(int id);
    // Auto-saves the entry (a Recording has nothing to write); false after notifying.
    bool autoSave(Entry &entry);
    QString writeScreenshot(Entry &entry);
    void failure(const QString &summary, const QString &why);

    Daemon &m_daemon;
    std::vector<Entry> m_entries; // oldest first, hidden included
    std::optional<Slot> m_slot;
    int m_nextId = 1;
    QString m_monitorName;
    int m_captureHides = 0;      // live CaptureHide holds
    bool m_leaving = false;      // the hide began with Thumbnails on screen: the next grab waits one pass
};

} // namespace tarn
