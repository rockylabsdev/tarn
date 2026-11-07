#pragma once
// The History window service: one window at a time, opened by `tarn history`
// (Alt+Print). The QML window is a thin layer (src/ui/history_windows.*) that
// listens for opened() / closed(); what it lists is the HistoryModel.
#include "history_model.h"

#include <QObject>

namespace tarn {

class Daemon;

class HistoryWindow : public QObject {
    Q_OBJECT
public:
    explicit HistoryWindow(Daemon &daemon);

    HistoryModel &model() { return m_model; }
    bool isOpen() const { return m_open; }
    Daemon &daemon() { return m_daemon; }

    // `tarn history`: opens the window; brings it forward when it is open; closes
    // it when it is open and focused.
    void toggle();
    // Reads History afresh and opens the window. Does nothing when already open.
    void open();
    void close();
    // Esc: clears a typed search first; with none, closes the window.
    void escape();
    // Gives the window the keyboard again (an Editor opened from it just closed). Does
    // nothing when the window is not open.
    void focus();
    // The window went away by some other route than close().
    void windowHidden() { m_open = false; }

    // The size the window first appears at: the usual 1200 x 780, no bigger than the monitor
    // under the pointer less an 80 px margin (Hyprland ignores a window's later resizes).
    QSize openingSize();

    static constexpr const char *kTitle = "Tarn history";

signals:
    void opened();
    void closed();
    void focusRequested();

private:
    Daemon &m_daemon;
    HistoryModel m_model;
    bool m_open = false;
};

} // namespace tarn
