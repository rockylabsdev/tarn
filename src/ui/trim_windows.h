#pragma once
// The Trim windows. Like the Editor's: one ordinary frameless top-level window per
// open TrimSession, floated and centered by the window rule in tarn.lua (matched
// by the title "Tarn trim"), sized before it is first shown with the Editor's
// rule, and opened with no animation.
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQuickView>

namespace tarn {

class Palette;
class Trim;
class TrimSession;

// What the QML window calls on its own window (context property `win`).
class TrimWindow : public QObject {
    Q_OBJECT
public:
    TrimWindow(Trim &trim, TrimSession &session, Palette &palette, QObject *parent = nullptr);
    QQuickView &view() { return m_view; }
    // Dragging an empty part of a bar: the compositor moves the window.
    Q_INVOKABLE bool startMove() { return m_view.startSystemMove(); }

private:
    QQuickView m_view;
};

class TrimWindows : public QObject {
    Q_OBJECT
public:
    TrimWindows(Trim &trim, Palette &palette, QObject *parent = nullptr);
    // The window of an open Trim, or null.
    QQuickView *viewFor(int captureId) const;

    static constexpr const char *kTitle = "Tarn trim";

private:
    void onOpened(TrimSession *session);

    Trim &m_trim;
    Palette &m_palette;
    QHash<int, QPointer<TrimWindow>> m_windows;
};

} // namespace tarn
