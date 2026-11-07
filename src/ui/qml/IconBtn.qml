import QtQuick

// An icon button. Inside a capsule it is a smaller capsule (pill); the accent
// fill when active. State changes are instant: no Behavior, no crossfade.
Rectangle {
    id: root
    property string icon
    property string tip
    property bool active: false
    property bool flat: true
    property int box: Style.ctlC
    property int boxW: box
    property bool pill: false
    property int iconSlot: boxW // the icon is centered in this much of the button
    property int iconSize: pill ? Style.iconSize : Style.iconSizeBox
    property color iconColor: active ? theme.accentText : theme.text
    readonly property bool hovered: area.containsMouse
    signal clicked
    width: boxW
    height: box
    radius: pill ? height / 2 : Style.radiusSm
    color: active ? theme.accent : area.pressed ? theme.pressed : area.containsMouse ? theme.hover : flat ? "transparent" : theme.control
    Icon { x: (root.iconSlot - width) / 2; anchors.verticalCenter: parent.verticalCenter; name: root.icon; color: root.iconColor; size: root.iconSize }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
    }
    Tip { visible: area.containsMouse && root.tip !== ""; text: root.tip }
}
