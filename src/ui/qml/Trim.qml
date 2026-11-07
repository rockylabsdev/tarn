import QtQuick
import QtQuick.Dialogs

// The Trim window (variant D of the trim prototype, in the Palette's colors): the
// Editor's 52 px top bar (close, the time, Start here / End here, what it keeps,
// Copy, Save as…, Done), the preview on the canvas, and a 104 px bottom bar with
// a 52 px filmstrip in an accent frame whose 14 px ends are the Handles. The
// context properties are `session` (TrimSession: everything the user does) and
// `win` (the window: startMove). Playback (Space, loops the kept range), the
// transport (to start, play/pause, to end) and the keys I, O, arrows, Home and End
// are the session's calls.
Item {
    id: root
    objectName: "trim"
    focus: true
    Component.onCompleted: forceActiveFocus()

    readonly property bool busy: session.trimming
    // The strip's place in the bottom bar (the prototype's geometry for D).
    readonly property real sx: 30
    readonly property real sw: Math.max(1, width - 60)
    readonly property real sy: 16
    readonly property real sh: 52
    readonly property real handleW: 14
    readonly property real out: 7           // the Handle's center sits this far outside the kept range
    readonly property real totalMs: session.frameCount * 1000 / session.fps
    // Which Handle (or the Playhead) is being dragged, and where the pointer hovers.
    property string dragging: ""
    property string hover: ""
    property real hoverFrame: 0

    function tx(frame) { return sx + frame / session.frameCount * sw; }
    function msAt(x) { return Math.max(0, Math.min(totalMs, (x - sx) / sw * totalMs)); }
    function frameAt(x) { return Math.round(msAt(x) * session.fps / 1000); }

    Rectangle { anchors.fill: parent; color: theme.canvas }

    // Save as…: the system file dialog, MP4 (GIF for a GIF), in the last folder used.
    function saveAs() {
        if (root.busy)
            return;
        saveAsLoader.active = true;
        saveAsLoader.item.start();
    }
    Loader {
        id: saveAsLoader
        active: false
        sourceComponent: FileDialog {
            fileMode: FileDialog.SaveFile
            nameFilters: [session.gif ? "GIF (*.gif)" : "MP4 (*.mp4)"]
            defaultSuffix: session.gif ? "gif" : "mp4"
            function start() {
                currentFolder = "file://" + session.saveAsFolder();
                open();
            }
            onAccepted: {
                let path = decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, ""));
                const suffix = session.gif ? ".gif" : ".mp4";
                if (!path.toLowerCase().endsWith(suffix))
                    path += suffix;
                session.saveAs(path);
            }
        }
    }

    // ── Top bar ─────────────────────────────────────────────────────────────────────
    Rectangle {
        id: top
        objectName: "topBar"
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Style.barHeight
        color: theme.surface
        MoveArea {}
        Sep { vertical: false; anchors.bottom: parent.bottom }

        Row {
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: Style.spacing
            CapBtn { objectName: "closeButton"; icon: "x"; tip: "Close (Esc): drop the trim"; onClicked: session.cancel() }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                leftPadding: 8
                Text {
                    objectName: "timeLabel"
                    text: session.timeText(session.playheadFrame)
                    color: theme.text
                    font { family: Style.font; pixelSize: 14; weight: Font.Medium; features: { "tnum": 1 } }
                }
                Text {
                    objectName: "durationLabel"
                    text: "/ " + session.durationText
                    color: theme.muted
                    font { family: Style.font; pixelSize: 14; features: { "tnum": 1 } }
                }
            }
            Capsule {
                opacity: root.busy ? 0.4 : 1
                enabled: !root.busy
                IconBtn { objectName: "toStartButton"; pill: true; box: 28; boxW: 34; icon: "player-skip-back"; tip: "To start (Home)"; onClicked: session.toStart() }
                IconBtn { objectName: "playButton"; pill: true; box: 28; boxW: 34; icon: session.playing ? "player-pause" : "player-play"; tip: session.playing ? "Pause (Space)" : "Play (Space)"; onClicked: session.togglePlay() }
                IconBtn { objectName: "toEndButton"; pill: true; box: 28; boxW: 34; icon: "player-skip-forward"; tip: "To end (End)"; onClicked: session.toEnd() }
            }
            Item { width: 4; height: 1 }
            Capsule {
                opacity: root.busy ? 0.4 : 1
                enabled: !root.busy
                IconBtn { objectName: "startHereButton"; pill: true; box: 28; boxW: 34; icon: "trim-start"; tip: "Start here (I)"; onClicked: session.startHere() }
                IconBtn { objectName: "endHereButton"; pill: true; box: 28; boxW: 34; icon: "trim-end"; tip: "End here (O)"; onClicked: session.endHere() }
            }
            Text {
                objectName: "keepsLabel"
                anchors.verticalCenter: parent.verticalCenter
                text: "Keeps " + session.keptText
                color: theme.text2
                font { family: Style.font; pixelSize: 13; features: { "tnum": 1 } }
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: Style.spacing
            opacity: root.busy ? 0.4 : 1
            enabled: !root.busy
            CapBtn { objectName: "copyButton"; icon: "copy"; tip: "Copy (Ctrl+C)"; onClicked: session.copyAndClose() }
            TextBtn { objectName: "saveAsButton"; text: "Save as…"; tip: "Save as… (Ctrl+S)"; onClicked: root.saveAs() }
            TextBtn { objectName: "doneButton"; text: "Done"; primary: true; tip: "Done (Enter)"; onClicked: session.done() }
        }
    }

    // ── Preview ─────────────────────────────────────────────────────────────────────
    Item {
        id: preview
        objectName: "preview"
        // True once the cache has a picture; re-read whenever frames arrive.
        readonly property bool cached: (session.cacheTick, session.cacheHasFrames())
        anchors { left: parent.left; right: parent.right; top: top.bottom; bottom: bottom.top }
        clip: true
        Item {
            anchors.fill: parent
            anchors.margins: Style.canvasMargin
            // Until the first frame of the cache is decoded: the one frame under the Playhead, from ffmpeg.
            Image {
                id: warmUp
                objectName: "warmUpImage"
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                smooth: true
                source: preview.cached ? ""
                        : "image://trim/" + session.captureId + "/" + session.generation + "/f" + session.previewFrame
            }
            // The preview: the cache's nearest frame, handed over synchronously (no blank in between).
            // `cacheTick` is in the URL so frames that arrive while the Playhead rests are picked up.
            Image {
                id: frame
                objectName: "previewImage"
                property int refineFrame: -1
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                asynchronous: false
                cache: false
                smooth: true
                visible: preview.cached
                source: !preview.cached ? ""
                        : "image://trimcache/" + session.captureId + "/" + session.generation + "/c" + session.previewFrame + "/" + session.cacheTick
            }
            // A long clip keeps every Nth frame: once the Playhead has rested a moment on one that is not
            // cached, the exact frame is made by ffmpeg (one run, never during a drag or playback).
            Timer {
                id: settle
                interval: 160
                onTriggered: frame.refineFrame = session.previewFrame
            }
            Connections {
                target: session
                function onPlayheadChanged() { settle.restart() }
            }
            Image {
                id: exact
                objectName: "exactImage"
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                smooth: true
                visible: status === Image.Ready && source != ""
                source: (session.cacheTick, !session.playing && frame.refineFrame === session.previewFrame && !session.previewExact())
                        ? "image://trim/" + session.captureId + "/" + session.generation + "/f" + session.previewFrame : ""
            }
        }
    }

    // ── Bottom bar ──────────────────────────────────────────────────────────────────
    Rectangle {
        id: bottom
        objectName: "bottomBar"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 104
        color: theme.surface
        MoveArea {}
        Sep { vertical: false }

        // The filmstrip, its dimming, the accent frame and the kept frames.
        Item {
            id: filmstrip
            objectName: "filmstrip"
            visible: !root.busy
            anchors.fill: parent

            readonly property real tileW: root.sh * session.videoSize.width / Math.max(1, session.videoSize.height)
            readonly property int tiles: Math.max(1, Math.ceil(root.sw / tileW))

            TrimStrip { x: root.sx; y: root.sy; width: root.sw; height: root.sh; tiles: filmstrip.tiles; tileW: filmstrip.tileW }
            Rectangle {   // frames outside the kept range stay dimmed
                objectName: "dim"
                x: root.sx; y: root.sy; width: root.sw; height: root.sh
                color: Qt.rgba(0, 0, 0, 0.62)
            }
            Rectangle {   // the accent frame; its two ends are the Handles
                objectName: "accentFrame"
                x: root.tx(session.startFrame) - root.handleW
                y: root.sy - 3
                width: root.tx(session.endFrame) - root.tx(session.startFrame) + 2 * root.handleW
                height: root.sh + 6
                radius: 8
                color: theme.accent
            }
            Item {        // the kept frames, undimmed
                x: root.tx(session.startFrame); y: root.sy
                width: root.tx(session.endFrame) - root.tx(session.startFrame); height: root.sh
                clip: true
                TrimStrip { x: -(root.tx(session.startFrame) - root.sx); width: root.sw; height: root.sh; tiles: filmstrip.tiles; tileW: filmstrip.tileW }
            }
            Repeater {
                model: ["start", "end"]
                Item {
                    required property string modelData
                    objectName: modelData + "Handle"
                    readonly property bool hot: root.hover === modelData || root.dragging === modelData
                    x: modelData === "start" ? root.tx(session.startFrame) - root.handleW : root.tx(session.endFrame)
                    y: root.sy - 3
                    width: root.handleW
                    height: root.sh + 6
                    Rectangle { anchors.fill: parent; radius: 6; color: "white"; opacity: parent.hot ? 0.28 : 0 }
                    Row {
                        anchors.centerIn: parent
                        spacing: 2
                        Repeater { model: 2; Rectangle { width: 1.5; height: 16; radius: 1; color: Qt.rgba(0, 0, 0, 0.55) } }
                    }
                }
            }
            Rectangle {   // the Playhead
                objectName: "playhead"
                visible: root.dragging !== "start" && root.dragging !== "end"
                x: Math.round(root.tx(session.playheadFrame)) - 1
                y: root.sy - 7
                width: 3
                height: root.sh + 14
                radius: 1.5
                color: "white"
                border { color: Qt.rgba(0, 0, 0, 0.45); width: 0.5 }
            }

            // Labeled ticks: about ten at most, at round times.
            readonly property real seconds: session.frameCount / session.fps
            readonly property real tickStep: {
                const candidates = [0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300];
                for (const s of candidates)
                    if (seconds / s <= 10)
                        return s;
                return 600;
            }
            Repeater {
                model: Math.floor(filmstrip.seconds / filmstrip.tickStep) + 1
                Item {
                    required property int index
                    readonly property real at: index * filmstrip.tickStep
                    objectName: "tick" + index
                    x: root.tx(at * session.fps)
                    y: 75
                    Rectangle { x: -0.5; width: 1; height: 5; color: theme.disabled }
                    Text {
                        y: 9
                        x: index === 0 ? 0 : -implicitWidth / 2
                        objectName: "tickLabel"
                        text: {
                            const m = Math.floor(parent.at / 60), s = parent.at - m * 60;
                            return m + ":" + (s < 10 ? "0" : "") + (filmstrip.tickStep < 1 ? s.toFixed(1) : s.toFixed(0));
                        }
                        color: theme.muted
                        font { family: Style.font; pixelSize: 11; features: { "tnum": 1 } }
                    }
                }
            }

            // A dark time pill rides above the dragged Handle, or where the pointer is on the strip.
            Rectangle {
                id: pill
                objectName: "timePill"
                readonly property real shownFrame: root.dragging === "start" ? session.startFrame
                    : root.dragging === "end" ? session.endFrame
                    : root.dragging === "playhead" ? session.playheadFrame : root.hoverFrame
                visible: root.dragging !== "" || root.hover === "track"
                width: pillText.implicitWidth + 14
                height: 20
                radius: 10
                color: theme.raised
                border { color: theme.border; width: 1 }
                x: Math.round((root.dragging === "start" ? root.tx(session.startFrame) - root.out
                               : root.dragging === "end" ? root.tx(session.endFrame) + root.out
                               : root.tx(shownFrame)) - width / 2)
                y: root.sy - 3 - 6 - height
                Text {
                    id: pillText
                    anchors.centerIn: parent
                    text: session.timeText(pill.shownFrame)
                    color: theme.text
                    font { family: Style.font; pixelSize: 11; weight: Font.Medium; features: { "tnum": 1 } }
                }
            }

            // Pointer: a Handle's end is grabbed within 12 px of its center; elsewhere the Playhead scrubs.
            MouseArea {
                id: strip
                objectName: "stripArea"
                x: root.sx - 24; width: root.sw + 48
                y: 8; height: 68
                hoverEnabled: true
                property string grab: ""
                function near(px) {
                    const ds = Math.abs(px - (root.tx(session.startFrame) - root.out));
                    const de = Math.abs(px - (root.tx(session.endFrame) + root.out));
                    if (Math.min(ds, de) > 12)
                        return "";
                    return ds <= de ? "start" : "end";
                }
                cursorShape: grab === "start" || grab === "end" || near(mouseX + x) !== "" ? Qt.SizeHorCursor : Qt.ArrowCursor
                function act(px) {
                    if (grab === "start")
                        session.dragStart(root.msAt(px + root.out));
                    else if (grab === "end")
                        session.dragEnd(root.msAt(px - root.out));
                    else if (grab === "playhead")
                        session.scrub(root.msAt(px));
                }
                onPressed: (m) => {
                    const px = m.x + x;
                    grab = near(px) || "playhead";
                    root.dragging = grab;
                    act(px);
                }
                onPositionChanged: (m) => {
                    const px = m.x + x;
                    if (grab !== "") {
                        act(px);
                    } else {
                        root.hover = near(px) || "track";
                        root.hoverFrame = root.frameAt(px);
                    }
                }
                onReleased: { grab = ""; root.dragging = ""; }
                onExited: if (grab === "") root.hover = ""
            }
        }

        // Trimming: the bar becomes progress and Cancel.
        Row {
            objectName: "trimmingBar"
            visible: root.busy
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 16
            Text {
                objectName: "trimmingLabel"
                anchors.verticalCenter: parent.verticalCenter
                width: 120
                text: "Trimming… " + Math.round(session.progress * 100) + "%"
                color: theme.text
                font { family: Style.font; pixelSize: 13; weight: Font.Medium; features: { "tnum": 1 } }
            }
            Rectangle {
                objectName: "progressTrack"
                anchors.verticalCenter: parent.verticalCenter
                width: bottom.width - 40 - 120 - 16 - 16 - 84
                height: 6
                radius: 3
                color: theme.track
                Rectangle {
                    objectName: "progressFill"
                    width: parent.width * session.progress
                    height: 6
                    radius: 3
                    color: theme.accent
                }
            }
            TextBtn { objectName: "cancelButton"; text: "Cancel"; tip: "Cancel the trim"; onClicked: session.cancelTrim() }
        }
    }

    // ── Keys ────────────────────────────────────────────────────────────────────────
    Shortcut { sequence: "Escape"; onActivated: session.cancel() }
    Shortcut { sequences: ["Return", "Enter", "Ctrl+Return", "Ctrl+Enter"]; onActivated: session.done() }
    Shortcut { sequence: "Ctrl+C"; onActivated: session.copyAndClose() }
    Shortcut { sequence: "Ctrl+S"; onActivated: root.saveAs() }
    Shortcut { sequence: "Space"; onActivated: session.togglePlay() }
    Shortcut { sequence: "I"; onActivated: session.startHere() }
    Shortcut { sequence: "O"; onActivated: session.endHere() }
    Shortcut { sequence: "Left"; onActivated: session.stepFrames(-1) }
    Shortcut { sequence: "Right"; onActivated: session.stepFrames(1) }
    Shortcut { sequence: "Shift+Left"; onActivated: session.stepSeconds(-1) }
    Shortcut { sequence: "Shift+Right"; onActivated: session.stepSeconds(1) }
    Shortcut { sequence: "Home"; onActivated: session.toStart() }
    Shortcut { sequence: "End"; onActivated: session.toEnd() }
}
