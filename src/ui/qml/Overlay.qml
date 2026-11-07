import QtQuick

// The Overlay (variant D): the frozen screen with the area outside the
// Selection dimmed 50%, an accent outline round the Selection, a dark size pill
// above it, and the system crosshair pointer (no crosshair lines). Coordinates
// are the monitor's logical pixels, so they are the model's.
Item {
    id: root
    focus: true
    readonly property var model: overlay.model
    readonly property rect sel: model.selection
    readonly property bool has: model.hasSelection
    readonly property int outline: Style.selectionOutline // the Selection's accent outline (50%); the window highlight keeps 2

    // Nothing is drawn while the model is not active (that is the warm-up).
    Image {
        id: frozenImage
        anchors.fill: parent
        visible: root.model.active
        source: root.model.active ? "image://frozen/" + overlay.generation : ""
        cache: false
        smooth: false
    }

    Item {
        anchors.fill: parent
        visible: root.model.active
        z: 1 // above the pointer area, so the pills can be clicked
        readonly property color dim: root.model.colorMode ? "transparent" : Style.selectionDim

        // Four rectangles round the hole (the whole screen while nothing is selected).
        Rectangle { color: parent.dim; x: 0; y: 0; width: root.width; height: root.has ? root.sel.y : root.height }
        Rectangle {
            visible: root.has; color: parent.dim
            x: 0; y: root.sel.y; width: root.sel.x; height: root.sel.height
        }
        Rectangle {
            visible: root.has; color: parent.dim
            x: root.sel.x + root.sel.width; y: root.sel.y; width: root.width - x; height: root.sel.height
        }
        Rectangle {
            visible: root.has; color: parent.dim
            x: 0; y: root.sel.y + root.sel.height; width: root.width; height: root.height - y
        }

        // The window under the pointer (Area mode): an outline that follows its rounded
        // corners, and a dark pill naming the app. Gone as soon as an area is dragged.
        Rectangle {
            objectName: "windowOutline"
            visible: root.model.hasHighlight && !root.has
            x: root.model.highlight.x; y: root.model.highlight.y
            width: root.model.highlight.width; height: root.model.highlight.height
            radius: root.model.highlightRounding
            color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12)
            border.width: 2
            border.color: theme.accent
        }
        Rectangle {
            objectName: "appPill"
            visible: root.model.hasHighlight && !root.has
            readonly property rect win: root.model.highlight
            height: 24
            width: appLabel.implicitWidth + 20
            radius: height / 2
            color: Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, 0.94)
            border.width: 1
            border.color: theme.border
            // Centered over the window's top edge; inside it when there is no room above.
            x: Math.max(0, Math.min(root.width - width, win.x + (win.width - width) / 2))
            y: win.y - root.outline - 8 - height >= 0 ? win.y - root.outline - 8 - height : win.y + 8
            Text {
                id: appLabel
                objectName: "appLabel"
                anchors.centerIn: parent
                text: root.model.highlightApp
                color: theme.text
                font.family: Style.fontMono
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }

        // Drawn just outside the Selection, so the picked pixels are not covered.
        Rectangle {
            objectName: "outline"
            visible: root.has
            x: root.sel.x - root.outline; y: root.sel.y - root.outline
            width: root.sel.width + 2 * root.outline; height: root.sel.height + 2 * root.outline
            color: "transparent"
            border.width: root.outline
            border.color: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, Style.selectionOutlineAlpha)
        }

        Rectangle {
            id: pill
            objectName: "sizePill"
            visible: root.has
            readonly property int gap: 8
            height: 24
            width: label.implicitWidth + 20 + (root.model.timer ? clock.width + 4 : 0)
            radius: height / 2
            color: Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, 0.94)
            border.width: 1
            border.color: theme.border
            // Above the Selection; inside its top edge when there is no room above.
            x: Math.max(0, Math.min(root.width - width, root.sel.x + (root.sel.width - width) / 2))
            y: root.sel.y - root.outline - gap - height >= 0 ? root.sel.y - root.outline - gap - height : root.sel.y + gap
            // The Timer is on: a clock, and the delay before the size ("3 s   700 × 400").
            Icon {
                id: clock
                objectName: "timerClock"
                visible: root.model.timer
                name: "clock"
                size: 14
                color: theme.text
                x: 10
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                id: label
                objectName: "sizeLabel"
                anchors.verticalCenter: parent.verticalCenter
                x: root.model.timer ? clock.x + clock.width + 4 : 10
                text: (root.model.timer ? root.model.timerLabel + "   " : "") +
                      (root.model.kept ? root.model.positionLabel + "   " + root.model.sizeLabel : root.model.sizeLabel)
                color: theme.text
                font.family: Style.fontMono
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }

        // Handles of a kept Selection: white 14 px dots with a 2 px accent rim.
        Repeater {
            model: root.model.kept ? 8 : 0
            Rectangle {
                objectName: "handle" + index
                readonly property var cx: [0, 1, 1, 0, 0.5, 1, 0.5, 0]
                readonly property var cy: [0, 0, 1, 1, 0, 0.5, 1, 0.5]
                width: 14; height: 14; radius: 7
                x: root.sel.x + root.sel.width * cx[index] - width / 2
                y: root.sel.y + root.sel.height * cy[index] - height / 2
                color: "white"
                border.width: 2
                border.color: theme.accent
            }
        }

        // Dark Cancel / Capture pills under the Selection (above it at the screen's foot).
        Row {
            objectName: "keptPills"
            visible: root.model.kept && !root.model.recordMode
            spacing: 8
            x: Math.max(0, Math.min(root.width - width, root.sel.x + (root.sel.width - width) / 2))
            y: root.sel.y + root.sel.height + 16 + height <= root.height ? root.sel.y + root.sel.height + 16 : Math.max(0, root.sel.y + root.sel.height - 16 - height)
            Repeater {
                model: [{ name: "cancel", label: qsTr("Cancel") }, { name: "capture", label: qsTr("Capture") }]
                Rectangle {
                    objectName: modelData.name + "Pill"
                    height: 28
                    width: pillText.implicitWidth + 28
                    radius: height / 2
                    color: Qt.rgba(0.08, 0.08, 0.09, 0.94)
                    Text {
                        id: pillText
                        anchors.centerIn: parent
                        text: modelData.label
                        color: "white"
                        font.family: Style.fontMono
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: modelData.name === "cancel" ? root.model.cancel() : root.model.confirm()
                    }
                }
            }
        }

        // The Record panel (record mode, kept Selection): RecordPanel.qml.
        RecordPanel {
            id: recordPanel
            objectName: "recordPanel"
            model: root.model
            sel: root.sel
            outline: root.outline
        }
    }

    // The Mode bar appears with the Overlay, instantly, and jumps with no animation.
    ModeBar {
        z: 3
        visible: root.model.active
        model: root.model
    }

    Magnifier {
        objectName: "magnifier"
        anchors.fill: parent
        z: 2
        visible: root.model.active && root.model.magnifier
        model: root.model
        source: frozenImage
    }

    // B skipped the auto-applied Preset for this capture. Appears at once (a key
    // opened it: no fade).
    Rectangle {
        objectName: "noFramePill"
        z: 2
        visible: root.model.active && root.model.noFrame
        height: 28
        width: noFrameLabel.implicitWidth + 28
        radius: height / 2
        anchors.horizontalCenter: parent.horizontalCenter
        y: 24 + 44 + 12 // under the Mode bar
        color: Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, 0.94)
        border.width: 1
        border.color: theme.border
        Text {
            id: noFrameLabel
            anchors.centerIn: parent
            text: "No frame"
            color: theme.text
            font.family: Style.fontMono
            font.pixelSize: 12
            font.weight: Font.Medium
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        cursorShape: Qt.CrossCursor
        onPositionChanged: mouse => root.model.pointerMoved(mouse.x, mouse.y, mouse.modifiers)
        onPressed: mouse => {
            root.forceActiveFocus() // takes the keys back from a W x H field
            root.model.pointerPressed(mouse.x, mouse.y, mouse.button, mouse.modifiers)
        }
        onDoubleClicked: mouse => root.model.pointerDoubleClicked(mouse.x, mouse.y)
        onReleased: mouse => root.model.pointerReleased(mouse.x, mouse.y, mouse.button, mouse.modifiers)
    }

    Keys.onPressed: event => {
        root.model.keyPressed(event.key, event.modifiers)
        event.accepted = true
    }
    Keys.onReleased: event => {
        if (!event.isAutoRepeat)
            root.model.keyReleased(event.key, event.modifiers)
        event.accepted = true
    }
}
