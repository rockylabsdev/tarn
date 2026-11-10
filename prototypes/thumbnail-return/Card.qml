import QtQuick
import QtQuick.Effects

// PROTOTYPE (#3): a stand-in Thumbnail with ThumbnailCard.qml's motion (enter
// spring from the left edge, others springing up to make room, opacity from its
// place), plus the ways back the prototype compares.
Item {
    id: card
    required property int cid
    required property real hue
    required property int index
    property var board
    readonly property int rank: board ? board.count - 1 - index : 0
    readonly property real offX: -Style.thumbW - Style.thumbEdge

    width: Style.thumbW
    height: Style.thumbH
    x: across.value + nudge.value
    y: up.value
    opacity: {
        const shown = (across.value + Style.thumbW) / (Style.thumbEdge + Style.thumbW);
        return Math.max(0, Math.min(1, (shown - 0.05) / 0.55)) * fade.value;
    }

    Spring { id: across; to: Style.thumbEdge; value: Style.thumbEdge; response: Style.thumbEnterResponse; bounce: Style.thumbEnterBounce }
    Spring { id: up; to: card.board ? card.board.slotY(card.rank) : 0; response: Style.thumbMoveResponse }
    Spring { id: nudge; to: 0; value: 0; response: 0.25 }
    Spring { id: fade; to: 1; value: 1; response: 0.2; restDelta: 0.002 }

    // Tarn's own entrance: in from just past the left edge.
    function enterFromEdge() {
        across.value = offX;
        across.velocity = 0;
        across.start();
    }
    // Variant D: a short fade with a 20 px nudge from the left.
    function fadeNudge() {
        fade.value = 0;
        fade.start();
        nudge.value = -20;
        nudge.start();
    }
    function placeAtSlot() { up.snap(); }
    Component.onCompleted: up.snap() // in its place at once; only a rank change moves it
    function hideForEntrance() { across.value = offX; across.stop(); }

    RectangularShadow {
        anchors.fill: frame
        radius: Style.thumbRadius
        blur: 22
        offset.y: 6
        color: Qt.rgba(0, 0, 0, 0.5)
    }
    Rectangle {
        id: frame
        anchors.fill: parent
        radius: Style.thumbRadius
        clip: true
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.hsla(card.hue, 0.45, 0.55, 1) }
            GradientStop { position: 1; color: Qt.hsla((card.hue + 0.12) % 1, 0.5, 0.3, 1) }
        }
        // A made-up window inside the capture.
        Rectangle {
            x: 26; y: 22; width: 150; height: 96; radius: 6
            color: Qt.rgba(1, 1, 1, 0.85)
            Rectangle { width: parent.width; height: 14; radius: 6; color: Qt.rgba(0, 0, 0, 0.15) }
            Column {
                x: 10; y: 24; spacing: 8
                Repeater { model: 4; Rectangle { width: 110 - index * 18; height: 6; radius: 3; color: Qt.rgba(0, 0, 0, 0.18) } }
            }
        }
        Text { anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 8; text: "Capture " + card.cid; color: "white"; font.pixelSize: 12; opacity: 0.8 }
    }
    Rectangle {
        anchors.fill: parent
        radius: Style.thumbRadius
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.1)
    }
}
