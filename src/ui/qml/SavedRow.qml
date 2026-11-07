import QtQuick

// The Saved colors row: up to 8 round swatches, newest on the left, then "+".
// The same list shows in the color popover and the Frame panel (session.savedColors).
// Left-click picks a swatch, right-click removes it at once. An empty row is only "+".
Flow {
    id: root
    objectName: "savedRow"
    property color current: "transparent" // the swatch with this color gets the accent ring
    property bool canSave: true            // false grays the "+" (the Frame isn't a plain color)
    signal picked(color value)
    signal plusClicked

    spacing: 6

    Repeater {
        model: session.savedColors
        Rectangle {
            id: sw
            required property int index
            required property string modelData
            objectName: "saved-" + index
            width: 24
            height: 24
            radius: 12
            color: modelData
            readonly property bool on: root.current.a > 0 && Qt.colorEqual(root.current, modelData)
            border.width: on ? 2 : 1
            border.color: on ? theme.accent : Qt.rgba(1, 1, 1, 0.25)
            Tip { visible: area.containsMouse; text: "Right-click to remove" }
            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: (m) => m.button === Qt.RightButton ? session.removeSavedColor(sw.index) : root.picked(sw.modelData)
            }
        }
    }

    Rectangle {
        objectName: "savedPlus"
        width: 24
        height: 24
        radius: 12
        opacity: root.canSave ? 1 : 0.4
        color: plusArea.containsMouse && root.canSave ? theme.hover : theme.control
        Icon { anchors.centerIn: parent; name: "plus"; size: 14; color: theme.text2 }
        Tip { visible: plusArea.containsMouse; text: "Save this color" }
        MouseArea { id: plusArea; anchors.fill: parent; hoverEnabled: true; onClicked: if (root.canSave) root.plusClicked() }
    }
}
