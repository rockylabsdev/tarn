#pragma once
// The History window. Like the Editor's: one ordinary frameless top-level window
// (title "Tarn history"), floated and centered by the window rule in tarn.lua,
// sized before its first show (Hyprland ignores later self-resizes) and shown with
// no animation. There is only ever one; it is built on the first open and kept.
#include <QObject>
#include <QQuickView>
#include <memory>

namespace tarn {

class HistoryWindow;
class Palette;

// What the QML window calls on its own window (context property `win`).
class HistoryWindowHandle : public QObject {
    Q_OBJECT
public:
    explicit HistoryWindowHandle(HistoryWindow &service, QObject *parent = nullptr) : QObject(parent), m_service(service) {}
    // Esc: closes the window.
    Q_INVOKABLE void close();
    // Esc: clears a typed search; with none, closes.
    Q_INVOKABLE void escape();
    // Dragging an empty part of a bar: the compositor moves the window.
    Q_INVOKABLE bool startMove();
    void setView(QQuickView *view) { m_view = view; }

private:
    HistoryWindow &m_service;
    QQuickView *m_view = nullptr;
};

class HistoryWindows : public QObject {
    Q_OBJECT
public:
    HistoryWindows(HistoryWindow &service, Palette &palette, QObject *parent = nullptr);
    // The window once it has been opened, or null.
    QQuickView *view() const { return m_view.get(); }


private:
    void onOpened();

    HistoryWindow &m_service;
    Palette &m_palette;
    HistoryWindowHandle m_handle;
    std::unique_ptr<QQuickView> m_view;
};

} // namespace tarn
