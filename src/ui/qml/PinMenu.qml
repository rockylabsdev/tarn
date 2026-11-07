import QtQuick

// The right-click menu on a Pin: Copy, Save as…, Edit, Lock, Close all pins (a GIF
// Pin has no Edit). It sits
// inside the Pin's own window and appears at once, no animation.
Rectangle {
    id: root
    objectName: "menu"
    signal copy
    signal saveAs
    signal edit
    signal closeAll
    signal lock
    property bool gif: false

    readonly property var rows: [
        { label: "Copy", name: "menuCopy" },
        { label: "Save as…", name: "menuSaveAs" },
        { label: "Edit", name: "menuEdit" },
        { label: "Lock", name: "menuLock" },
        { label: "Close all pins", name: "menuCloseAll" }
    ].filter(r => !(gif && r.name === "menuEdit"))
    width: 148
    height: rows.length * 30 + 8
    radius: 10
    color: theme.surface
    border.width: 1
    border.color: theme.border

    Column {
        anchors.fill: parent
        anchors.margins: 4
        Repeater {
            model: root.rows
            Rectangle {
                required property var modelData
                required property int index
                objectName: modelData.name
                width: parent.width
                height: 30
                radius: 6
                color: item.containsMouse ? theme.hover : "transparent"
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 10
                    text: parent.modelData.label
                    color: theme.text
                    font.family: Style.font
                    font.pixelSize: 13
                }
                MouseArea {
                    id: item
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: ({ menuCopy: root.copy, menuSaveAs: root.saveAs, menuEdit: root.edit, menuLock: root.lock, menuCloseAll: root.closeAll })[parent.modelData.name]()
                }
            }
        }
    }
}
