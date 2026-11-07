import QtQuick

// A text button; `primary` takes the accent fill (Done).
Rectangle {
    id: root
    property string text
    property string tip
    property bool primary: false
    signal clicked
    height: Style.ctlC
    width: label.implicitWidth + 24
    radius: height / 2
    color: primary ? (area.pressed ? Qt.darker(theme.accent, 1.15) : area.containsMouse ? Qt.lighter(theme.accent, 1.08) : theme.accent)
                   : area.pressed ? theme.pressed : area.containsMouse ? theme.hover : theme.control
    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.primary ? theme.accentText : theme.text
        font.family: Style.font
        font.pixelSize: 14
        font.weight: Font.Medium
    }
    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: root.clicked() }
    Tip { visible: area.containsMouse && root.tip !== ""; text: root.tip }
}
