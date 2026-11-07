import QtQuick

// A horizontal slider for the custom color picker: value 0..1 on a gradient
// track. `stops` is the gradient. Dragging emits `moved(value)`.
Item {
    id: root
    property real value: 0
    property var stops: []
    property string label
    property string tipText: Math.round(value * 100)
    signal moved(real value)
    signal released()
    width: 220
    height: 22

    Rectangle {
        id: track
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        height: 12
        radius: 6
        border.color: theme.border
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: root.stops.length ? root.stops[0] : "black" }
            GradientStop { position: 1; color: root.stops.length ? root.stops[root.stops.length - 1] : "white" }
        }
        // Extra stops (the hue track) are painted as a second layer of segments.
        Row {
            anchors.fill: parent
            visible: root.stops.length > 2
            Repeater {
                model: Math.max(0, root.stops.length - 1)
                Rectangle {
                    required property int index
                    width: track.width / (root.stops.length - 1)
                    height: track.height
                    radius: 0
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: root.stops[index] }
                        GradientStop { position: 1; color: root.stops[index + 1] }
                    }
                }
            }
        }
    }
    Rectangle {
        objectName: "knob"
        x: root.value * (root.width - width)
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        radius: 8
        color: "white"
        border.color: Qt.rgba(0, 0, 0, 0.35)
    }
    ValueTip { visible: dragArea.pressed; knobX: 8 + root.value * (root.width - 16); text: root.tipText }
    MouseArea {
        id: dragArea
        anchors.fill: parent
        function set(mx) { root.moved(Math.max(0, Math.min(1, (mx - 8) / (root.width - 16)))); }
        onPressed: (m) => set(m.x)
        onPositionChanged: (m) => { if (pressed) set(m.x); }
        onReleased: root.released()
        onCanceled: root.released()
    }
}
