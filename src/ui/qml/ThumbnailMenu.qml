import QtQuick

// The right-click menu on a Thumbnail: Save as…, Delete, Close all. It sits
// inside the card's own rectangle (so the surface's input mask already covers
// it) and appears at once, no animation.
Rectangle {
    id: root
    objectName: "menu"
    signal saveAs
    signal deleteIt
    signal closeAll

    // A Recording is saved as the file it is and is not in History, so it has no
    // Save as… or Delete.
    property bool recording: false
    readonly property var rows: recording ? [
        { label: "Close all", name: "menuCloseAll" }
    ] : [
        { label: "Save as…", name: "menuSaveAs" },
        { label: "Delete", name: "menuDelete" },
        { label: "Close all", name: "menuCloseAll" }
    ]
    width: 132
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
                    onClicked: ({ menuSaveAs: root.saveAs, menuDelete: root.deleteIt, menuCloseAll: root.closeAll })[parent.objectName]()
                }
            }
        }
    }
}
