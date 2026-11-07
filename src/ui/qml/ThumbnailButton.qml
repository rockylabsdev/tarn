import QtQuick

// A light round or pill button on a Thumbnail's scrim (spec: variant A). It
// shows either its icon/label, or, once `done`, a check: opacity, scale
// 1 to 0.25 and a 0 to 4 px blur over 200 ms on cubic-bezier(0.2, 0, 0, 1).
Item {
    id: root
    property string text          // a pill's label; empty for a round icon button
    property string icon          // the round button's icon
    property string tip
    property bool done: false     // show the check
    readonly property bool hovered: area.containsMouse
    signal clicked

    width: text !== "" ? 86 : 28
    height: text !== "" ? 30 : 28

    readonly property var checkCurve: [0.2, 0, 0, 1, 1, 1]
    readonly property color ink: "#1A1A1A"

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: area.containsMouse || root.done ? "#FFFFFF" : "#E6E6E6"
        scale: area.pressed ? 0.96 : 1
        Behavior on scale {
            NumberAnimation { duration: 150; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.25, 0.46, 0.45, 0.94, 1, 1] }
        }

        Item {
            id: face
            anchors.fill: parent
            opacity: root.done ? 0 : 1
            scale: root.done ? 0.25 : 1
            Behavior on opacity { NumberAnimation { duration: Style.thumbCheckMs; easing.type: Easing.BezierSpline; easing.bezierCurve: root.checkCurve } }
            Behavior on scale { NumberAnimation { duration: Style.thumbCheckMs; easing.type: Easing.BezierSpline; easing.bezierCurve: root.checkCurve } }
            Text {
                visible: root.text !== ""
                anchors.centerIn: parent
                text: root.text
                color: root.ink
                font.family: Style.font
                font.pixelSize: 14
                font.weight: Font.Medium
            }
            Icon { visible: root.text === ""; anchors.centerIn: parent; name: root.icon; size: 15; color: root.ink }
        }
        Item {
            objectName: "check"
            anchors.fill: parent
            opacity: root.done ? 1 : 0
            scale: root.done ? 1 : 0.25
            Behavior on opacity { NumberAnimation { duration: Style.thumbCheckMs; easing.type: Easing.BezierSpline; easing.bezierCurve: root.checkCurve } }
            Behavior on scale { NumberAnimation { duration: Style.thumbCheckMs; easing.type: Easing.BezierSpline; easing.bezierCurve: root.checkCurve } }
            Icon { anchors.centerIn: parent; name: "check"; size: 18; color: root.ink }
        }
    }
    // A 36 px hit area round a 28 px round button.
    MouseArea {
        id: area
        anchors.fill: parent
        anchors.margins: root.text === "" ? -4 : 0
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: if (!root.done) root.clicked()
    }
    Tip { visible: area.containsMouse && root.tip !== "" && !root.done; text: root.tip }
}
