// Three bars on the nested bench, each reserving a different size on its own edge, so
// `hyprctl monitors -j` shows which slot of `reserved` belongs to which edge.
// Run with: quickshell -p scripts/bench-bars.qml (scripts/bench-inside.sh does).
import Quickshell

ShellRoot {
    PanelWindow {
        screen: Quickshell.screens.find(s => s.name === "BENCH-2") ?? null
        anchors { top: true; left: true; right: true }
        implicitHeight: 30
    }
    PanelWindow {
        screen: Quickshell.screens.find(s => s.name === "BENCH-2") ?? null
        anchors { bottom: true; left: true; right: true }
        implicitHeight: 20
    }
    PanelWindow {
        screen: Quickshell.screens.find(s => s.name === "BENCH-2") ?? null
        anchors { left: true; top: true; bottom: true }
        implicitWidth: 10
    }
}
