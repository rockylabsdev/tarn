#pragma once
// The Editor's windows. One ordinary frameless top-level window per open
// EditorSession (not a layer surface: it takes the keyboard and Hyprland floats
// and centers it by the window rules in tarn.lua, matched by the title
// "Tarn editor"). The size is set before the window is first shown, because
// Hyprland ignores a window's own resizes afterwards.
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQuickView>
#include <memory>

namespace tarn {

class Editor;
class EditorSession;
class Palette;

// What the QML window calls on its own window (context property `win`).
class EditorWindow : public QObject {
    Q_OBJECT
public:
    EditorWindow(EditorSession &session, Palette &palette, QObject *parent = nullptr);
    QQuickView &view() { return m_view; }
    // Dragging an empty part of a bar: the compositor moves the window.
    Q_INVOKABLE bool startMove() { return m_view.startSystemMove(); }

private:
    QQuickView m_view;
};

class EditorWindows : public QObject {
    Q_OBJECT
public:
    EditorWindows(Editor &editor, Palette &palette, QObject *parent = nullptr);
    // The window of an open Editor, or null.
    QQuickView *viewFor(int captureId) const;


private:
    void onOpened(EditorSession *session);

    Palette &m_palette;
    QHash<int, QPointer<EditorWindow>> m_windows;
};

} // namespace tarn
