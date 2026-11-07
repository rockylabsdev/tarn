import QtQuick

// Show clicks: one Ripple per press inside the recorded area. A ring grows from
// the press point to 48 px and fades over 400 ms, in the accent with a faint
// fill; the same for every button. The surface is click-through.
//
// Tuned with the animate guide: it is an entrance of something that was not
// there, so it grows from 40% (never from nothing) on a strong ease-out
// (OutExpo: fast start, long settle), while the fade uses the gentler
// ease-out-quad so the ring is still visible as it reaches full size. Linear
// would look lifeless; ease-in would delay the moment the viewer is watching.
// Ripples are frequent and decorative, short and non-blocking: each is its own
// item, so rapid clicks overlap instead of restarting one another.
Item {
    id: root

    readonly property int diameter: 48
    readonly property real startScale: 0.4
    readonly property int duration: 400

    Component {
        id: rippleComponent
        Rectangle {
            id: ring
            objectName: "ripple"
            width: root.diameter
            height: root.diameter
            radius: width / 2
            color: Qt.alpha(theme.accent, 0.18)
            border.width: 2
            border.color: theme.accent
            scale: root.startScale
            ParallelAnimation {
                running: true
                NumberAnimation { target: ring; property: "scale"; from: root.startScale; to: 1; duration: root.duration; easing.type: Easing.OutExpo }
                NumberAnimation { target: ring; property: "opacity"; from: 1; to: 0; duration: root.duration; easing.type: Easing.OutQuad }
                onFinished: Qt.callLater(ring.destroy)
            }
        }
    }

    Connections {
        target: ripples
        function onRippled(position) {
            const r = rippleComponent.createObject(root)
            r.x = position.x - root.diameter / 2
            r.y = position.y - root.diameter / 2
        }
    }
}
