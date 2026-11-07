import QtQuick

// The Timer's Countdown: a red pill at the top center, under the space a top bar reserves, and, for an area, the area still outlined with the rest of the monitor dimmed, drawn as
// the Overlay draws a kept Selection. The surface is the whole monitor and input-transparent,
// so clicks reach the windows beneath. The dim and outline appear at once; the pill drops in
// on OsdFrame's spring (mirrored: from above). Everything leaves at once when the Countdown
// hides: the shot must not see any of it. A full-screen Countdown has no dim or outline.
Item {
    id: root
    readonly property rect area: countdown.area
    readonly property bool framed: countdown.active && !countdown.fullScreen

    // Four rectangles round the hole, as the Overlay draws them.
    Item {
        anchors.fill: parent
        visible: root.framed
        Rectangle { color: Style.selectionDim; x: 0; y: 0; width: root.width; height: root.area.y }
        Rectangle { color: Style.selectionDim; x: 0; y: root.area.y; width: root.area.x; height: root.area.height }
        Rectangle {
            color: Style.selectionDim
            x: root.area.x + root.area.width; y: root.area.y; width: root.width - x; height: root.area.height
        }
        Rectangle { color: Style.selectionDim; x: 0; y: root.area.y + root.area.height; width: root.width; height: root.height - y }
        // Just outside the area, so the captured pixels are not covered.
        Rectangle {
            objectName: "countdownOutline"
            x: root.area.x - Style.selectionOutline; y: root.area.y - Style.selectionOutline
            width: root.area.width + 2 * Style.selectionOutline; height: root.area.height + 2 * Style.selectionOutline
            color: "transparent"
            border.width: Style.selectionOutline
            border.color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, Style.selectionOutlineAlpha)
        }
    }

    OsdFrame {
        id: frame
        objectName: "countdownLabel"
        up: countdown.active
        animateOut: false
        enterFrom: -Style.countdownDrop
        fill: Style.countdownFill
        borderWidth: 0
        anchors.horizontalCenter: parent.horizontalCenter
        y: countdown.top
        width: Style.countdownWidth
        height: Style.countdownHeight
        Row {
            anchors.centerIn: parent
            spacing: Style.countdownGap
            Icon {
                objectName: "countdownClock"
                anchors.verticalCenter: parent.verticalCenter
                name: "clock"
                color: Style.countdownInk
                size: Style.iconSizeBox
            }
            Text {
                objectName: "countdownText"
                anchors.verticalCenter: parent.verticalCenter
                text: countdown.seconds
                color: Style.countdownInk
                font.family: Style.fontMono // fixed-width digits: the pill never shifts as they change
                font.pixelSize: Style.fontCountdown
                font.weight: Font.Medium
            }
        }
    }
}
