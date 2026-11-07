#pragma once
// The Editor service: opens an Editor on a Thumbnail (a click or its Edit
// button), keeps each Thumbnail's Capture document between sessions (so Done
// then reopening shows the Annotations still editable), and hides the Thumbnail
// from the stack while its Editor is open.
//
// The QML window is a thin layer (src/ui/editor_windows.*) that listens for
// `opened`; everything the user does goes through EditorSession.
#include "editor_session.h"

#include <QObject>
#include <QImage>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace tarn {

class Daemon;
class ThumbnailStack;

class Editor : public QObject {
    Q_OBJECT
public:
    explicit Editor(Daemon &daemon);

    // Opens (or returns the already open) Editor for a Thumbnail. Null for an
    // unknown or hidden id, or a Recording (that is Trim's). A Thumbnail whose
    // History entry is open from the History window brings that Editor forward.
    EditorSession *open(int captureId);
    EditorSession *session(int captureId) const;

    // ---- Edit from History ----
    // Opens the Editor on a History entry, on the document rebuilt from its Project
    // file (the original when there is none, or it is unreadable). An Editor already
    // open on that entry (this one, or its Thumbnail's) is brought forward
    // (broughtForward) and returned; a Thumbnail on the stack for the entry opens
    // its own Editor instead of a second one. No
    // Thumbnail is hidden or added; closing writes the Project file and focuses the
    // History window. Null if the original cannot be read.
    EditorSession *openFromHistory(const QString &historyId);
    EditorSession *sessionForHistory(const QString &historyId) const;
    bool editsHistoryEntry(const QString &historyId) const { return sessionForHistory(historyId) != nullptr; }
    // A Thumbnail's Capture document between sessions (the last Done), or null.
    const CaptureDocument *committedDocument(int captureId) const;

    // Annotations copied from one Editor to paste into another (or the same):
    // independent copies, kept while the daemon runs. Nothing goes on the system
    // clipboard.
    void copyAnnotations(int sourceCaptureId, std::vector<std::unique_ptr<Annotation>> annotations);
    bool hasAnnotationsToPaste() const { return !m_copied.empty(); }
    // Fresh copies for `captureId`: in place in another Editor, nudged 10 px
    // further each time into the one they came from.
    std::vector<std::unique_ptr<Annotation>> annotationsToPaste(int captureId);

    // ---- Add image ----
    // Where Ctrl+V looks for a picture. The default reads the system clipboard;
    // tests replace it (they never touch the real one).
    using ClipboardImageSource = std::function<QImage()>;
    void setClipboardImageSource(ClipboardImageSource source) { m_clipboardImage = std::move(source); }
    // The picture on the clipboard that is NEWER than the last Annotation copy
    // (Ctrl+C on Annotations does not touch the system clipboard, so a picture
    // that was there already is not the thing the user just copied). Null if none.
    QImage clipboardImageToPaste() const;

signals:
    // A new Editor opened: the UI puts a window on it. It is deleted after `finished`.
    void opened(tarn::EditorSession *session);
    // Edit on an entry that already has an Editor: the UI raises that window.
    void broughtForward(tarn::EditorSession *session);

private:
    void bringForward(EditorSession *session);
    friend class EditorSession;
    ThumbnailStack &stack();
    Daemon &daemon() { return m_daemon; }
    void commit(EditorSession *session);   // Done
    void release(EditorSession *session);  // any way out: reveal the Thumbnail (unless it closed)
    void onClosed(int captureId);          // the Thumbnail left the stack

    Daemon &m_daemon;
    std::map<int, std::unique_ptr<CaptureDocument>> m_committed;
    std::map<int, EditorSession *> m_sessions;
    std::vector<std::unique_ptr<Annotation>> m_copied;
    std::map<QString, EditorSession *> m_historySessions;
    int m_nextHistorySession = -2; // sessions from History use ids below -1 (never a Thumbnail's)
    int m_copiedFrom = -1;
    int m_pastes = 0;
    ClipboardImageSource m_clipboardImage;
    size_t m_imageSeenAtCopy = 0; // fingerprint of the clipboard picture when Annotations were last copied
};

} // namespace tarn
