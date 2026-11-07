import QtQuick
import QtQuick.Controls.Basic

// A capsule showing the current choice and a chevron; a click opens a list.
// options: [{ v, label }]. `up` opens the list above (for the bottom bar).
Rectangle {
    id: root
    property var options: []
    property string current
    property string label
    property string tip
    property bool up: false
    property alias popup: menu
    signal picked(string v)
    height: Style.ctlC
    width: row.implicitWidth + 26
    radius: height / 2
    color: ma.pressed ? theme.pressed : ma.containsMouse ? theme.hover : theme.control

    Row {
        id: row
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        Text {
            objectName: "label"
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: theme.text
            font.family: Style.font
            font.pixelSize: 13
            font.weight: Font.Medium
        }
        Icon { name: "chevron-down"; size: 12; color: theme.text2; anchors.verticalCenter: parent.verticalCenter }
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; onClicked: menu.toggle() }
    Tip { visible: ma.containsMouse && root.tip !== "" && !menu.opened; text: root.tip }

    Popover {
        id: menu
        objectName: "menu"
        y: root.up ? -implicitHeight - Style.popoverGap : root.height + Style.popoverGap
        padding: 5
        contentItem: Column {
            Repeater {
                model: root.options
                Rectangle {
                    required property var modelData
                    objectName: "option-" + modelData.v
                    width: 150
                    height: Style.ctlC
                    radius: Style.radiusSm
                    color: m.containsMouse ? theme.hover : "transparent"
                    Row {
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Icon { name: "check"; size: 14; color: theme.accent; opacity: modelData.v === root.current ? 1 : 0; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: modelData.label; color: theme.text; font.family: Style.font; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                    }
                    MouseArea { id: m; anchors.fill: parent; hoverEnabled: true; onClicked: { root.picked(modelData.v); menu.close(); } }
                }
            }
        }
    }
}
