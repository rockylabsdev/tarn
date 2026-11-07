#include "editor.h"

#include "daemon/daemon.h"
#include "history/history.h"
#include "history/history.h"
#include "history_window/history_window.h"
#include "hyprland/hyprland_bridge.h"
#include "notify/notifier.h"
#include "opening_size.h"
#include "thumbnails/thumbnail_stack.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QScreen>

namespace tarn {

namespace {
size_t fingerprint(const QImage &image) {
    if (image.isNull())
        return 0;
    return qHashBits(image.constBits(), size_t(image.sizeInBytes()), size_t(image.width()) * 100003 + size_t(image.height()));
}
} // namespace

Editor::Editor(Daemon &daemon) : m_daemon(daemon) {
    m_clipboardImage = [] {
        const QClipboard *clipboard = QGuiApplication::clipboard();
        return clipboard ? clipboard->image() : QImage();
    };
    // The History sweep leaves an entry alone while an Editor is open on it.
    daemon.service<History>().addBusyCheck([this](const QString &id) { return editsHistoryEntry(id); });
    auto &thumbs = stack();
    connect(&thumbs, &ThumbnailStack::editRequested, this, &Editor::open);
    connect(&thumbs, &ThumbnailStack::closed, this, &Editor::onClosed);
}

ThumbnailStack &Editor::stack() { return m_daemon.service<ThumbnailStack>(); }

EditorSession *Editor::session(int captureId) const {
    const auto it = m_sessions.find(captureId);
    return it == m_sessions.end() ? nullptr : it->second;
}

EditorSession *Editor::sessionForHistory(const QString &historyId) const {
    const auto it = m_historySessions.find(historyId);
    return it == m_historySessions.end() ? nullptr : it->second;
}

// An Editor already open comes forward. It may be on another workspace, so Hyprland is
// asked to focus its window (that takes you there, as Alt+Print does for History).
void Editor::bringForward(EditorSession *session) {
    emit broughtForward(session);
    QString error;
    m_daemon.service<HyprlandBridge>().focusWindow(session->windowTitle(), &error); // best effort: Qt's raise still runs
}

EditorSession *Editor::openFromHistory(const QString &historyId) {
    if (EditorSession *existing = sessionForHistory(historyId)) {
        bringForward(existing);
        return existing;
    }
    // The entry is also a Thumbnail: one Editor per capture, so that one (an Editor
    // already open on it comes forward; otherwise the Thumbnail's Editor opens).
    if (const int captureId = stack().idForHistory(historyId); captureId >= 0) {
        if (EditorSession *onStack = session(captureId)) {
            bringForward(onStack);
            return onStack;
        }
        if (EditorSession *opened = open(captureId))
            return opened;
    }
    auto &history = m_daemon.service<History>();
    const QImage original = QImage::fromData(history.originalPng(historyId), "PNG");
    if (original.isNull()) {
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not open the screenshot"),
                                            QStringLiteral("its picture in History could not be read"));
        return nullptr;
    }
    History::LoadedProject loaded = history.loadProject(historyId, original);
    std::unique_ptr<CaptureDocument> working =
        loaded.document ? std::move(loaded.document) : std::make_unique<CaptureDocument>(original);

    const MonitorSize monitor = monitorSizeUnderPointer(m_daemon);
    const QSize size = editorOpeningSize(working->size(), monitor.size, monitor.scale);

    const int id = m_nextHistorySession--;
    auto *created = new EditorSession(*this, id, std::move(working), size, historyId);
    created->setParent(this);
    m_sessions[id] = created;
    m_historySessions[historyId] = created;
    emit opened(created); // no Thumbnail is hidden: this Editor is not on the stack
    connect(created, &EditorSession::finished, created, &QObject::deleteLater);
    return created;
}

const CaptureDocument *Editor::committedDocument(int captureId) const {
    const auto it = m_committed.find(captureId);
    return it == m_committed.end() ? nullptr : it->second.get();
}

EditorSession *Editor::open(int captureId) {
    if (EditorSession *existing = session(captureId))
        return existing;
    auto &thumbs = stack();
    if (thumbs.isHidden(captureId) || thumbs.kind(captureId) != ThumbnailStack::Screenshot)
        return nullptr;
    // Already open from the History window (Edit): that Editor comes forward, no second one.
    if (EditorSession *fromHistory = sessionForHistory(thumbs.historyId(captureId))) {
        bringForward(fromHistory);
        return fromHistory;
    }
    // A capture framed by the auto-applied Preset opens on its original picture
    // with that Frame in the document, so the panel shows it and can change or remove it.
    const auto autoFrame = thumbs.autoFrame(captureId);
    const QImage shot = autoFrame ? thumbs.originalImage(captureId) : thumbs.image(captureId);
    if (shot.isNull())
        return nullptr;

    // The Thumbnail's shot as captured; after a Done the committed document
    // carries it along with the Annotations.
    std::unique_ptr<CaptureDocument> working;
    if (const auto it = m_committed.find(captureId); it != m_committed.end())
        working = it->second->clone();
    else {
        // Edits saved beside the original (the Project file) come back as they were; a
        // damaged one is notified by History and the original opens instead.
        const QString historyId = thumbs.historyId(captureId);
        if (!autoFrame && !historyId.isEmpty())
            working = m_daemon.service<History>().loadProject(historyId, thumbs.originalImage(captureId)).document;
        if (!working) {
            working = std::make_unique<CaptureDocument>(shot);
            if (autoFrame)
                working->setFrameLive(*autoFrame);
        }
    }

    const MonitorSize monitor = monitorSizeUnderPointer(m_daemon);
    const QSize size = editorOpeningSize(working->size(), monitor.size, monitor.scale);

    auto *created = new EditorSession(*this, captureId, std::move(working), size);
    created->setParent(this);
    m_sessions[captureId] = created;
    thumbs.setHidden(captureId, true); // out of the stack, not counted toward the 4
    emit opened(created);
    // Connected after the window's own handler, so the window is deleted first
    // and its QML never sees the session vanish under it.
    connect(created, &EditorSession::finished, created, &QObject::deleteLater);
    return created;
}

void Editor::copyAnnotations(int sourceCaptureId, std::vector<std::unique_ptr<Annotation>> annotations) {
    m_copied = std::move(annotations);
    m_copiedFrom = sourceCaptureId;
    m_pastes = 0;
    m_imageSeenAtCopy = fingerprint(m_clipboardImage());
}

QImage Editor::clipboardImageToPaste() const {
    const QImage image = m_clipboardImage();
    if (image.isNull() || (!m_copied.empty() && fingerprint(image) == m_imageSeenAtCopy))
        return {};
    return image;
}

std::vector<std::unique_ptr<Annotation>> Editor::annotationsToPaste(int captureId) {
    std::vector<std::unique_ptr<Annotation>> out;
    if (m_copied.empty())
        return out;
    if (captureId == m_copiedFrom)
        ++m_pastes;
    const QPointF offset = captureId == m_copiedFrom ? QPointF(10, 10) * m_pastes : QPointF();
    for (const auto &a : m_copied) {
        out.push_back(a->clone());
        out.back()->translate(offset);
    }
    return out;
}

void Editor::commit(EditorSession *session) {
    if (session->fromHistory())
        return; // the Project file is its memory
    m_committed[session->captureId()] = session->document().clone();
}

void Editor::release(EditorSession *session) {
    const int id = session->captureId();
    m_sessions.erase(id);
    if (session->fromHistory()) {
        m_historySessions.erase(session->historyId());
        m_daemon.service<HistoryWindow>().focus(); // back to the History window
        return;
    }
    stack().setHidden(id, false); // no-op if the Thumbnail is gone
}

void Editor::onClosed(int captureId) {
    m_committed.erase(captureId);
    if (EditorSession *open = session(captureId)) {
        open->end();
        m_sessions.erase(captureId);
    }
}

TARN_MODULE(editor, [](Daemon &daemon) {
    daemon.service<Editor>(); // listens for Thumbnail clicks from the start
});

} // namespace tarn
