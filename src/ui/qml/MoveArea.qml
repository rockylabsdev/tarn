import QtQuick

// Pressing an empty part of a bar moves the window: the compositor does the
// move (xdg_toplevel.move), so the Editor needs no title bar. Declared first in
// a bar so its buttons sit above it.
MouseArea {
    anchors.fill: parent
    onPressed: win.startMove()
}
