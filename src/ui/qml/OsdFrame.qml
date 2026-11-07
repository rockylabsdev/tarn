import QtQuick
import QtQuick.Effects

// The OSD's look and motion: a solid
// pill (the theme's surface at full opacity, a 1 px border, radius = height / 2) with a soft
// drop shadow, that rises into place and sinks out on two springs.
//   presence: 0..1, `osdInResponse` while up, `osdOutResponse` while leaving; the scale and
//             the opacity come from it (opacity is full by `osdFadeBy` of it).
//   lift:     px below the rest line: 0 when up, `osdSink` when away; it enters from `osdRise`.
// A change of `up` mid-move retargets both springs and keeps their speed, so a reversal bends.
// While away (presence settled at 0) the frame is invisible. Put the contents inside it.
//
// `animateIn` / `animateOut` false make that way a cut. The Recording bar animates only for
// the auto-hide; the Countdown must be gone before the grab, so it never animates out.
Item {
    id: root

    default property alias content: pill.data
    property bool up: false
    property bool animateIn: true
    property bool animateOut: true
    // The look and the entrance, overridable: the Countdown is red, borderless and drops in from above.
    property color fill: Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, 1)
    property color borderColor: theme.border
    property int borderWidth: 1
    property real enterFrom: Style.osdRise // px below (positive) or above (negative) the rest line it enters from

    // Away: nothing to draw, and nothing for a layer mask to catch.
    readonly property bool closed: presence.value === 0 && presence.to === 0
    readonly property real progress: Math.max(0, Math.min(1, presence.value))

    height: Style.osdHeight
    visible: !closed
    opacity: Math.min(1, progress / Style.osdFadeBy)
    scale: Style.osdScaleFrom + (1 - Style.osdScaleFrom) * progress
    transform: Translate { y: lift.value }

    // The target values are set here as well as bound: the order of the two is not defined.
    onUpChanged: {
        if (up) {
            if (!animateIn) {
                presence.stop(); lift.stop();
                presence.value = 1; presence.velocity = 0;
                lift.value = 0; lift.velocity = 0;
            } else if (presence.value === 0 && presence.velocity === 0) {
                lift.value = enterFrom; // from nothing: rises into place
                lift.velocity = 0;
            }
        } else if (!animateOut) {
            presence.stop(); lift.stop();
            presence.value = 0; presence.velocity = 0;
            lift.value = Style.osdSink; lift.velocity = 0;
        }
    }

    Spring {
        id: presence
        to: root.up ? 1 : 0
        response: root.up ? Style.osdInResponse : Style.osdOutResponse
        bounce: 0
        restDelta: Style.osdRestDelta
    }
    Spring {
        id: lift
        to: root.up ? 0 : Style.osdSink
        response: root.up ? Style.osdInResponse : Style.osdOutResponse
        bounce: 0
    }

    RectangularShadow {
        anchors.fill: pill
        radius: pill.radius
        offset.y: Style.osdShadowOffset
        blur: Style.osdShadowBlur
        color: Style.osdShadowColor
    }
    Rectangle {
        id: pill
        objectName: "osdPill"
        anchors.fill: parent
        radius: height / 2
        color: root.fill
        border.width: root.borderWidth
        border.color: root.borderColor
    }
}
