#pragma once
// Everything Tarn asks of Hyprland (through hyprctl).
#include "overlay/window_info.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRegion>
#include <optional>

namespace tarn {

class Daemon;

struct MonitorInfo {
    QString name;        // e.g. "DP-1": what grim -o wants
    QRect logicalRect;   // position and size in Hyprland's layout (scaled pixels)
    double scale = 1.0;  // device pixels per logical pixel
    int reservedTop = 0; // px a bar keeps free at the top (`reserved`[1]); 0 when none is reported
};

class HyprlandBridge : public QObject {
    Q_OBJECT
public:
    explicit HyprlandBridge(Daemon &daemon) : m_daemon(daemon) {}

    // The monitor the pointer is on. On failure returns nullopt and sets *error.
    std::optional<MonitorInfo> monitorUnderPointer(QString *error);

    // Every monitor Hyprland lists. On failure returns an empty list and sets *error.
    QList<MonitorInfo> monitors(QString *error);

    // The monitor whose logical rectangle holds `point`. nullopt when none does
    // (check *error to tell that from a failed query).
    std::optional<MonitorInfo> monitorContaining(const QPoint &point, QString *error);

    // Floating windows by title, for Pins. `title` is the exact window title.
    // The rule that floats, pins and un-animates them is static, in tarn.lua; the
    // position and size of ONE window cannot be, so they are added here.
    //
    // Before the window first shows: a window rule that opens it at `layoutRect`
    // (Hyprland ignores where a client asks to be, and a client's own resize after
    // the first map). False after filling *error if Hyprland could not take it.
    // Hyprland's Lua cannot delete a window rule, so the rules use a pool of four
    // names that are reused (and the old holder disabled) instead of one new rule per Pin.
    bool openWindowAt(const QString &title, const QRect &layoutRect, QString *error);
    // Where the window is now (Hyprland layout, logical px), or nullopt.
    std::optional<QRect> windowRect(const QString &title, QString *error);
    // Moves and resizes a mapped window.
    bool setWindowRect(const QString &title, const QRect &layoutRect, QString *error);

    // Locked Pins: sets (or clears) the window's `no_focus` with
    // `hyprctl eval 'hl.dispatch(hl.dsp.window.set_prop({window="address:<addr>",
    // prop="no_focus", value="1"|"0"}))'` (the old `dispatch setprop` form is refused
    // since Hyprland reads dispatch arguments as Lua). Hyprland's hit-testing
    // skips such a window, so clicks reach whatever is beneath it.
    bool setWindowNoFocus(const QString &title, bool noFocus, QString *error);

    // The History window: whether the focused window has this exact title
    // (`hyprctl activewindow`), and bringing the window with that title forward
    // and focusing it. Both are false when Hyprland cannot answer.
    bool isWindowFocused(const QString &title);
    bool focusWindow(const QString &title, QString *error);

    // Moves the real pointer to a point in the layout (hl.dsp.cursor.move through
    // `hyprctl eval`: under the Lua config `hyprctl dispatch` takes Lua too).
    bool warpPointer(const QPoint &layoutPoint, QString *error);

    // Show clicks: non-consuming left, middle and right mouse binds that run
    // `command <button>` (button = left|middle|right), added with `hyprctl eval`
    // (Hyprland 0.56 Lua: hl.bind(key, hl.dsp.exec_cmd(..), {non_consuming=true})).
    // Removed with hl.unbind by key, so it also works after a crash, when no bind
    // handle survives. Both are safe to repeat.
    bool addClickBinds(const QString &command, QString *error);
    bool removeClickBinds(QString *error);
    // The pointer in the layout (logical px), or nullopt with *error.
    std::optional<QPointF> pointerPosition(QString *error);

    // True while Hyprland draws the pointer itself (`cursor:no_hardware_cursors`
    // is 1 or 2). Then it is baked into captures and Recordings and the recorder's
    // -cursor flag cannot remove it. Unreadable counts as hardware (false).
    bool softwarePointer();

    // The windows the pointer could pick on `monitor`, frontmost first, in
    // monitor-local logical pixels. Only mapped, visible windows on the workspace
    // (or special workspace) the monitor shows. Hyprland has no real z-order, so
    // the order is: special workspace, fullscreen, floating, tiled; within a
    // group the most recently focused first. `rect` is the window box, which
    // excludes the border and the gaps. On failure returns an empty list and sets *error.
    QList<WindowInfo> windows(const MonitorInfo &monitor, QString *error);

    // The window snapshot. Taken once, before the Overlay opens (and for a
    // full-screen capture), from `hyprctl clients`, `monitors` and `activewindow`.
    // Only windows on a workspace actually on screen are kept (the `visible`
    // field does not mean that): each monitor's active workspace, its special
    // workspace while shown, and pinned windows. If Hyprland cannot answer, the
    // snapshot is empty. It stays until the next takeSnapshot().
    void takeSnapshot();
    // The snapshot window `rect` (layout px, not monitor-local) overlaps most,
    // counting only what is visible of each: a window is hidden by those stacked
    // above it. Stacking, front first: special workspace, full screen, floating,
    // tiled; within a group a window later in Hyprland's list is above an earlier
    // one. nullopt over bare desktop. WindowInfo::rect is in layout px here.
    std::optional<WindowInfo> windowOverlapping(const QRect &rect) const;
    // The focused window, if it is in the snapshot; nullopt otherwise.
    std::optional<WindowInfo> focusedWindow() const { return m_focused; }

private:
    // `hyprctl monitors -j` and `cursorpos -j`, parsed. nullopt sets *error.
    std::optional<QJsonArray> queryMonitors(QString *error);
    // `hyprctl clients -j`, parsed: the one place the client list is read.
    std::optional<QJsonArray> queryClients(QString *error);
    // The client with exactly this title; nullopt sets *error (also when there is none).
    std::optional<QJsonObject> clientTitled(const QString &title, QString *error);
    std::optional<QPointF> queryCursor(QString *error);

    static constexpr int kPinRuleSlots = 4; // window-rule names openWindowAt cycles through
    Daemon &m_daemon;
    int m_ruleSlot = 0;
    QList<WindowInfo> m_snapshot; // front first; rect in layout px
    std::optional<WindowInfo> m_focused;
};

} // namespace tarn
