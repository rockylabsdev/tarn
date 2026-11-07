import QtQuick

// The Mode bar: one capsule at the top center with the four Modes, the
// Timer and Sequence toggles, Full screen and Last area. Each button shows its
// key in a small corner label and a tooltip names it. It jumps to the bottom
// edge (no animation) while a dragged or kept Selection lies within 24 px of
// its top spot; the model decides, and the pointer's hover never moves it.
// Sequence shows an "N taken" pill at the bar's right end while it is on.
// Last area comes in a later ticket and is grayed here.
Rectangle {
    id: bar
    objectName: "modeBar"
    required property var model // the OverlayModel
    readonly property rect spot: model.modeBarTopSpot
    readonly property int margin: Style.modeBarMargin

    // Centered on its own width, so while the width morphs both ends move evenly.
    x: (parent.width - width) / 2
    y: model.modeBarAtBottom ? parent.height - margin - height : margin
    // A Mode change or K resizes the bar: only the Text button changes width (it hugs the
    // Text layout's name), on a Spring, and the bar follows its buttons frame by frame,
    // 6 px each side. At rest that is the daemon's spot.width (a test keeps them equal);
    // bound to spot.width it jumped ahead of the gliding buttons and they spilled out.
    width: buttons.width + 2 * Style.modeBarGap
    height: spot.height
    radius: height / 2
    color: Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, 0.94)
    border.width: 1
    border.color: theme.border

    // Swallows presses on the bar's gaps, so they never start a drag.
    MouseArea { anchors.fill: parent; hoverEnabled: true }

    component ModeBtn: IconBtn {
        id: btn
        property string key
        property string tipName
        property bool wired: true
        pill: true
        // The icon is centered in the left 40 px; the 6 px beyond hold the key letter,
        // set in from the pill's rounded end so the accent fill wraps it with room.
        boxW: 46
        iconSlot: 40
        enabled: wired
        opacity: wired ? 1 : 0.4
        tip: tipName + "   " + key
        readonly property real keyWidth: keyText.implicitWidth
        Text {
            id: keyText
            objectName: "keyLabel"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: Style.spacing
            anchors.topMargin: Style.keyInsetTop
            text: btn.key
            color: btn.active ? theme.accentText : theme.muted
            font.family: Style.fontMono
            font.pixelSize: Style.fontKey
            font.weight: Font.Medium
        }
    }

    // "N taken" at the bar's right end, just outside it, while Sequence is on.
    Rectangle {
        objectName: "sequencePill"
        visible: bar.model.sequence
        anchors.left: parent.right
        anchors.leftMargin: Style.spacing
        anchors.verticalCenter: parent.verticalCenter
        width: takenText.implicitWidth + 24
        height: Style.pillHeight
        radius: height / 2
        color: theme.accent
        Text {
            id: takenText
            objectName: "sequenceTaken"
            anchors.centerIn: parent
            text: qsTr("%1 taken").arg(bar.model.taken)
            color: theme.accentText
            font.family: Style.fontMono
            font.pixelSize: Style.fontSmall
            font.weight: Font.Medium
        }
        // Swallows presses on the pill, so they never start a drag on the Overlay.
        MouseArea { anchors.fill: parent; hoverEnabled: true }
    }

    Row {
        id: buttons
        objectName: "modeBarRow"
        anchors.centerIn: parent
        spacing: Style.modeBarGap
        Capsule {
            ModeBtn {
                objectName: "modeBtnScreenshot"; icon: "screenshot"; key: "1"; tipName: qsTr("Screenshot")
                active: bar.model.mode === 0; onClicked: bar.model.setMode(0)
            }
            ModeBtn {
                objectName: "modeBtnRecord"; icon: "video"; key: "2"; tipName: qsTr("Record")
                active: bar.model.mode === 1; onClicked: bar.model.setMode(1)
            }
            ModeBtn {
                id: textBtn
                objectName: "modeBtnText"; icon: "text-scan-2"; key: "3"; tipName: qsTr("Text")
                // In Text Mode the button names the Text layout beside its icon (the icon
                // already says Text) and hugs it: 6 px from the name to the key letter.
                // K's hint is in the tooltip. The daemon knows these widths too
                // (OverlayModel::textButtonExtra); a test keeps the two equal.
                readonly property bool inText: bar.model.mode === 2
                boxW: inText ? Math.ceil(36 + layoutLabel.implicitWidth + 6 + keyWidth + 8) : 46
                // A Spring, so a second K or Mode change mid-move bends the glide instead of
                // restarting it (CODING_STANDARDS.md, Motion). It starts settled: no motion on open.
                width: textGlide.value
                Spring {
                    id: textGlide
                    to: textBtn.boxW
                    response: Style.modeBarMorphResponse
                    bounce: 0
                    Component.onCompleted: snap()
                }
                tip: inText ? qsTr("Text layout   K") : tipName + "   " + key
                active: inText; onClicked: bar.model.setMode(2)
                // The name swaps at once while the button morphs to its new width, so it
                // is clipped to the room inside the pill: a longer name is revealed as the
                // pill grows and never shows outside it.
                Item {
                    objectName: "textLayoutClip"
                    visible: parent.inText
                    clip: true
                    x: 36
                    height: parent.height
                    width: Math.max(0, parent.width - x - 6 - parent.keyWidth - 8)
                    Text {
                        id: layoutLabel
                        objectName: "textLayoutLabel"
                        anchors.verticalCenter: parent.verticalCenter
                        text: bar.model.textLayoutName
                        color: theme.accentText
                        font.family: Style.font
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.Medium
                    }
                }
            }
            ModeBtn { objectName: "modeBtnColor"; icon: "color-picker"; key: "4"; tipName: qsTr("Color"); wired: true
                active: bar.model.mode === 3; onClicked: bar.model.setMode(3) }
        }
        Sep {}
        Capsule {
            ModeBtn {
                objectName: "modeBtnTimer"; icon: "clock"; key: "T"; tipName: qsTr("Timer")
                wired: bar.model.timerAvailable
                active: bar.model.timer; onClicked: bar.model.toggleTimer()
            }
            ModeBtn {
                objectName: "modeBtnSequence"; icon: "stack-2"; key: "S"; tipName: qsTr("Sequence")
                active: bar.model.sequence; onClicked: bar.model.toggleSequence()
            }
        }
        Sep {}
        Capsule {
            ModeBtn {
                objectName: "modeBtnFull"; icon: "maximize"; key: "F"; tipName: qsTr("Full screen")
                onClicked: bar.model.takeFullScreen()
            }
            ModeBtn { objectName: "modeBtnLast"; icon: "history"; key: "L"; tipName: qsTr("Last area"); onClicked: bar.model.restoreLastArea() }
        }
    }
}
