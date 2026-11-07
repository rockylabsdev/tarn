import QtQuick

// The right-click menu on a History entry: every action, with Show in Files. It
// appears at once, no animation. Single-entry actions are grayed on a multi-selection
// (`single` false); Restore and Delete work on any selection.
Rectangle {
    id: root
    objectName: "historyMenu"
    property bool single: true
    // The one selected entry carries edits: Revert to original is in the menu.
    property bool edited: false
    // Emitted with the row's name: edit, copy, pin, saveAs, restore, showInFiles, revert, delete.
    signal chosen(string action)

    readonly property var rows: [
        { label: "Edit", name: "edit", key: "Enter", needsOne: true },
        { label: "Copy", name: "copy", key: "Ctrl+C", needsOne: true },
        { label: "Pin", name: "pin", key: "P", needsOne: true },
        { label: "Save as…", name: "saveAs", key: "Ctrl+S", needsOne: true },
        { label: "Restore", name: "restore", key: "R", needsOne: false },
        { label: "Show in Files", name: "showInFiles", key: "", needsOne: true },
        { label: "Revert to original", name: "revert", key: "", needsOne: true, onlyEdited: true },
        { label: "Delete", name: "delete", key: "Del", needsOne: false }
    ]
    width: 200
    readonly property var shown: rows.filter(r => !r.onlyEdited || edited)
    height: shown.length * 30 + 8
    radius: 10
    color: theme.surface
    border.width: 1
    border.color: theme.border

    Column {
        anchors.fill: parent
        anchors.margins: 4
        Repeater {
            model: root.shown
            Rectangle {
                id: item
                required property var modelData
                readonly property bool enabledRow: !modelData.needsOne || root.single
                objectName: "menu_" + modelData.name
                width: parent.width
                height: 30
                radius: 6
                color: enabledRow && area.containsMouse ? theme.hover : "transparent"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 10
                    text: item.modelData.label
                    color: theme.text
                    opacity: item.enabledRow ? 1 : 0.4
                    font { family: Style.font; pixelSize: 13 }
                }
                Text {
                    anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 10 }
                    text: item.modelData.key
                    color: theme.muted
                    opacity: item.enabledRow ? 1 : 0.4
                    font { family: Style.font; pixelSize: 12 }
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: if (item.enabledRow) root.chosen(item.modelData.name)
                }
            }
        }
    }
}
