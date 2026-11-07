import QtQuick
import QtQuick.Shapes

// What stays on screen while a Recording runs, on a click-through layer surface
// that covers the monitor: the area's red outline (from the moment the Recording
// starts) and the Recording bar (once the recorder is writing). Both sit OUTSIDE
// the area, so neither is in the video, unless the bar fits nowhere outside (full
// screen): then it sits inside at the bottom center and auto-hides, and an
// invisible strip along the bottom edge brings it back. Coordinates are
// the monitor's logical px.
Item {
    id: root
    readonly property rect area: recording.area
    readonly property int outline: 2

    Rectangle {
        objectName: "recordingOutline"
        visible: recording.active
        x: root.area.x - root.outline; y: root.area.y - root.outline
        width: root.area.width + 2 * root.outline; height: root.area.height + 2 * root.outline
        color: "transparent"
        border.width: root.outline
        border.color: Style.recordRed
    }

    // The strip (auto-hiding bar only, while the Recording runs): the pointer on it shows the bar.
    Item {
        objectName: "barStrip"
        visible: recording.stripRect.width > 0
        x: recording.stripRect.x; y: recording.stripRect.y
        width: recording.stripRect.width; height: recording.stripRect.height
        HoverHandler { onHoveredChanged: recording.setPointerOnStrip(hovered) }
    }

    // The Recording bar: stop + time | pause/resume, restart, delete, grip (40 px
    // cells), in the OSD's look (OsdFrame: solid, shadowed, 48 px). An auto-hiding bar
    // rises into place and sinks out on springs, like the OSD; everything else is
    // instant: the area Recording's bar (it never hides), a stop, a restart, a delete. It
    // never takes focus (the surface is WindowDoesNotAcceptFocus and nothing here is
    // focusable) and ignores clicks for its first 0.5 s on screen, so the click that
    // started the Recording (or a stray one) cannot hit a control.
    OsdFrame {
        id: bar
        objectName: "recordingBar"
        up: recording.barVisible && recording.hasBar
        // Motion only while the Recording runs on with an auto-hiding bar.
        readonly property bool moves: recording.autoHide && (recording.canPause || recording.paused)
        animateIn: moves
        animateOut: moves
        x: recording.barRect.x; y: recording.barRect.y
        // After a GIF stop the bar springs narrower to the progress ring.
        readonly property bool encoding: recording.encoding
        readonly property int targetWidth: encoding ? ringRow.implicitWidth + 2 * 14 : row.implicitWidth + 2 * 4
        width: targetWidth
        Behavior on width {
            enabled: bar.encoding
            SpringAnimation { spring: 4; damping: 0.32; epsilon: 0.5 }
        }
        // The placement follows where the bar is going, not each frame of the spring.
        onTargetWidthChanged: recording.setBarSize(targetWidth, height)
        Component.onCompleted: recording.setBarSize(targetWidth, height)

        // Clicks count only after this; false again whenever the bar goes away.
        property bool armed: false
        onUpChanged: {
            armed = false;
            if (up)
                armTimer.restart();
            else
                armTimer.stop();
        }
        // Auto-hide: the pointer on the bar keeps it shown; leaving it (and the strip) starts the 1 s.
        HoverHandler { id: barHover; enabled: recording.autoHide; onHoveredChanged: recording.setPointerOnBar(hovered) }
        Timer { id: armTimer; interval: 500; onTriggered: bar.armed = true }

        // Milliseconds of the Recording so far (pauses excluded), refreshed while the bar is up.
        property int elapsed: 0
        Timer {
            running: bar.up
            repeat: true
            interval: 100
            triggeredOnStart: true
            onTriggered: bar.elapsed = recording.elapsedMs()
        }
        readonly property bool paused: recording.paused
        readonly property string timeText: {
            const s = Math.floor(elapsed / 1000);
            const sec = (s % 60 < 10 ? "0" : "") + (s % 60);
            if (s >= 3600)
                return Math.floor(s / 3600) + ":" + (Math.floor(s / 60) % 60 < 10 ? "0" : "") + (Math.floor(s / 60) % 60) + ":" + sec;
            return Math.floor(s / 60) + ":" + sec;
        }
        TextMetrics { id: timeMetrics; font.family: Style.fontMono; font.pixelSize: 14; font.weight: Font.Medium; text: bar.timeText }
        TextMetrics { id: minMetrics; font.family: Style.fontMono; font.pixelSize: 14; font.weight: Font.Medium; text: "0:00" }

        Row {
            id: row
            objectName: "barRow"
            visible: !bar.encoding
            anchors.centerIn: parent
            spacing: 1
            enabled: bar.armed

            // Stop: the red dot (a rounded square under the pointer, and while paused, so it
            // reads as stop: the pause/play cell beside it shows the pause) and the time.
            Rectangle {
                id: stopSeg
                objectName: "stopButton"
                width: 10 + 16 + 6 + Math.max(timeMetrics.advanceWidth, minMetrics.advanceWidth) + (recording.micOn ? 17 : 0) + 10
                height: 34
                radius: height / 2
                anchors.verticalCenter: parent.verticalCenter
                color: stopArea.pressed ? theme.pressed : stopArea.containsMouse ? theme.hover : "transparent"
                Item {
                    x: 10; width: 16; height: parent.height
                    Rectangle {
                        objectName: "stopDot"
                        anchors.centerIn: parent
                        readonly property bool morph: stopArea.containsMouse || bar.paused
                        width: morph ? 14 : 16; height: width
                        radius: morph ? 3 : width / 2
                        color: Style.recordRed
                    }
                }
                Text {
                    objectName: "elapsedLabel"
                    x: 10 + 16 + 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: bar.timeText
                    color: bar.paused ? theme.text2 : Style.recordRed
                    font.family: Style.fontMono
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }
                Icon {
                    objectName: "micGlyph"
                    visible: recording.micOn
                    x: parent.width - 10 - 14
                    anchors.verticalCenter: parent.verticalCenter
                    name: "microphone"; size: 14; color: theme.text2
                }
                MouseArea { id: stopArea; anchors.fill: parent; hoverEnabled: true; onClicked: recording.stopRecording() }
            }

            Sep { }

            // Pause is grayed until the recorder is writing: a pause signal sent earlier kills it.
            IconBtn {
                objectName: "pauseButton"
                pill: true; box: 34; boxW: 40
                anchors.verticalCenter: parent.verticalCenter
                icon: bar.paused ? "player-play" : "player-pause"
                tip: bar.paused ? "Resume" : "Pause"
                enabled: bar.paused || recording.canPause
                opacity: enabled ? 1 : 0.4
                onClicked: recording.togglePause()
            }
            IconBtn {
                objectName: "restartButton"
                pill: true; box: 34; boxW: 40
                anchors.verticalCenter: parent.verticalCenter
                icon: "rotate-clockwise"; tip: "Restart"
                onClicked: recording.restart()
            }
            IconBtn {
                objectName: "deleteButton"
                pill: true; box: 34; boxW: 40
                anchors.verticalCenter: parent.verticalCenter
                icon: "trash"; tip: "Delete"
                onClicked: recording.deleteRecording()
            }
            // The grip drags the bar, for this Recording only.
            Rectangle {
                id: grip
                objectName: "gripButton"
                width: 40; height: 34; radius: height / 2
                anchors.verticalCenter: parent.verticalCenter
                color: gripArea.pressed ? theme.pressed : gripArea.containsMouse ? theme.hover : "transparent"
                Icon { anchors.centerIn: parent; name: "grip-vertical"; color: theme.text2 }
                MouseArea {
                    id: gripArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    property real startX
                    property real startY
                    property point pressAt
                    onPressed: mouse => {
                        startX = recording.barRect.x; startY = recording.barRect.y;
                        pressAt = gripArea.mapToItem(root, mouse.x, mouse.y);
                    }
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return;
                        const p = gripArea.mapToItem(root, mouse.x, mouse.y);
                        recording.moveBarTo(Math.round(startX + p.x - pressAt.x), Math.round(startY + p.y - pressAt.y));
                    }
                }
            }
        }

        // GIF progress: a ring and "GIF 42%".
        Row {
            id: ringRow
            objectName: "gifProgress"
            visible: bar.encoding
            anchors.centerIn: parent
            spacing: 10
            Item {
                width: 24; height: 24
                anchors.verticalCenter: parent.verticalCenter
                Shape {
                    anchors.fill: parent
                    layer.enabled: true
                    layer.samples: 4
                    ShapePath {
                        strokeColor: theme.border; strokeWidth: 3; fillColor: "transparent"
                        PathAngleArc { centerX: 12; centerY: 12; radiusX: 10; radiusY: 10; startAngle: 0; sweepAngle: 360 }
                    }
                    ShapePath {
                        strokeColor: Style.recordRed; strokeWidth: 3; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                        PathAngleArc { centerX: 12; centerY: 12; radiusX: 10; radiusY: 10; startAngle: -90; sweepAngle: 360 * recording.encodingProgress }
                    }
                }
            }
            Text {
                objectName: "gifLabel"
                anchors.verticalCenter: parent.verticalCenter
                width: 72
                text: qsTr("GIF %1%").arg(Math.round(recording.encodingProgress * 100))
                color: theme.text
                font.family: Style.fontMono
                font.pixelSize: 14
                font.weight: Font.Medium
            }
        }
    }
}
