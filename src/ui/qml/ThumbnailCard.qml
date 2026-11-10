import QtQuick
import QtQuick.Effects

// One Thumbnail: 240 x 150, the capture cropped to fill, a scrim and controls on
// hover, and its motion: a new one springs in from the left edge, the others
// spring up to make room, and a closed one is flicked back off the same edge.
// All three are springs (Spring.qml), so a capture taken mid-move bends the
// motion instead of restarting it. Its place in the stack comes from `board`
// (Thumbnails.qml); the stack model has already dropped it by the time it
// leaves, so everything it needs to leave is held here.
Item {
    id: card
    objectName: "card"
    required property int captureId
    required property var board

    // "", "copy" or "save": the button that shows its check while the card waits to leave.
    property string confirming: ""
    property bool leaving: false
    property bool menuOpen: false
    // "MP4 0:06" for a Recording, "" for a screenshot.
    readonly property string chip: thumbnails.chip(captureId)
    readonly property bool hovered: hover.hovered && !leaving
    readonly property bool exposed: hovered || confirming !== ""
    // Where it sits: 0 is the corner (the newest).
    readonly property int rank: {
        const live = board.liveIds, i = live.indexOf(captureId);
        return i < 0 ? 0 : live.length - 1 - i;
    }

    // Just past the screen's left edge: where a card comes in from and goes back to.
    readonly property real offX: -Style.thumbW - Style.thumbEdge

    width: Style.thumbW
    height: Style.thumbH
    x: across.value
    y: up.value
    // Fades with its place, so coming in and going out are mirror images: clear
    // while it is (nearly) off the edge, solid once 60% of the way on.
    opacity: {
        const shown = (x + Style.thumbW) / (Style.thumbEdge + Style.thumbW);
        return Math.max(0, Math.min(1, (shown - 0.05) / 0.55));
    }

    Spring {
        id: across
        to: Style.thumbEdge
        value: Style.thumbEdge
        response: Style.thumbEnterResponse
        bounce: Style.thumbEnterBounce
        // Gone once nothing of it is on screen; no need to wait for the spring to rest.
        onValueChanged: if (card.leaving && value <= -Style.thumbW) card.gone()
        onSettled: if (card.leaving) card.gone()
    }
    // A leaving card keeps its place: the others close the gap, it does not follow them.
    property real leavingY: 0
    Spring {
        id: up
        to: card.leaving ? card.leavingY : card.board.slotY(card.rank)
        response: Style.thumbMoveResponse
        bounce: 0
    }

    Component.onCompleted: {
        board.register(card);
        up.snap(); // the corner at once; only the others move
        // New captures come in from the edge; one back from the Editor is simply there again.
        if (board.takeArrival(captureId)) {
            across.value = offX;
            across.start();
        }
    }
    Component.onDestruction: board.unregister(card)

    // Back from a Capture hide: in from the left edge on the enter spring, straight
    // at its place in the stack. A card already leaving keeps leaving.
    function springBack() {
        if (leaving)
            return;
        up.snap();
        across.response = Style.thumbEnterResponse;
        across.bounce = Style.thumbEnterBounce;
        across.velocity = 0;
        across.value = offX;
        across.to = Style.thumbEdge;
        across.start();
    }

    // The stack closed this Thumbnail (Save, Copy, ×, or anything else to come).
    // Copy and Save first show their check.
    function closed() {
        if (confirming !== "")
            hold.start();
        else
            slide();
    }
    function slide() {
        if (leaving)
            return;
        leavingY = up.to;
        leaving = true;
        board.refreshLive();
        // Out on a critically damped spring with a flick: it leaves at full speed
        // and eases off as it crosses the edge, never winding up first. (A flick of
        // exactly the spring's own rate makes the move a clean exponential glide.)
        const w = 2 * Math.PI / Style.thumbExitResponse;
        across.response = Style.thumbExitResponse;
        across.bounce = 0;
        across.velocity = Math.min(across.velocity, -w * (across.value - offX));
        across.to = offX;
        across.start();
    }
    property bool removed: false
    function gone() {
        if (removed)
            return;
        removed = true;
        across.stop();
        up.stop();
        // Removed after this returns: the springs are part of the card it deletes.
        // (A fresh function each time: callLater would merge several cards leaving together into one call.)
        const id = card.captureId, board = card.board;
        Qt.callLater(() => board.remove(id));
    }
    Timer { id: hold; interval: Style.thumbCheckHoldMs; onTriggered: card.slide() }

    onHoveredChanged: if (!hovered) menuOpen = false

    function act(which) {
        if (leaving || confirming !== "")
            return;
        confirming = which;
        // The stack closes it itself on success, which calls closed() above.
        const ok = which === "copy" ? thumbnails.copy(captureId) : thumbnails.save(captureId) !== "";
        if (!ok) {
            confirming = "";
            hold.stop();
        }
    }

    RectangularShadow {
        anchors.fill: frame
        radius: Style.thumbRadius
        blur: 22
        offset.y: 6
        color: Qt.rgba(0, 0, 0, 0.5)
    }
    Rounded {
        id: frame
        anchors.fill: parent
        radius: Style.thumbRadius
        Rectangle { objectName: "backdrop"; anchors.fill: parent; color: theme.canvas }
        Image {
            id: shot
            objectName: "shot"
            anchors.fill: parent
            source: "image://capture/" + card.captureId
            fillMode: Image.PreserveAspectCrop
            // A tall capture keeps its top (a window's title bar); anything else its middle.
            verticalAlignment: implicitHeight / Math.max(1, implicitWidth) > Style.thumbH / Style.thumbW ? Image.AlignTop : Image.AlignVCenter
            smooth: true
            mipmap: true
            cache: false
        }
        Rectangle {
            id: scrim
            objectName: "scrim"
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, Style.scrimOpacity)
            opacity: card.exposed ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: Style.scrimMs; easing.type: Easing.BezierSpline; easing.bezierCurve: [0.25, 0.1, 0.25, 1, 1, 1] }
            }
        }
    }
    // A white-at-10% outline keeps a dark capture's edge on a dark desktop.
    Rectangle {
        anchors.fill: parent
        radius: Style.thumbRadius
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.1)
    }

    // Pressing does nothing until release; releasing on the card (not on a button,
    // which sits above this) opens the Editor.
    Item {
        objectName: "openArea"
        anchors.fill: parent
        // A handler, not a MouseArea: a MouseArea here broke the card's hover.
        TapHandler {
            acceptedButtons: Qt.LeftButton
            enabled: !card.leaving && card.confirming === "" // the Editor for a screenshot, Trim for a Recording
            onTapped: thumbnails.edit(card.captureId)
        }
    }

    // The controls fade with the scrim, and are invisible (to hover too) once faded out.
    Item {
        id: controls
        objectName: "controls"
        anchors.fill: parent
        visible: opacity > 0
        opacity: scrim.opacity
        enabled: !card.leaving
        Column {
            anchors.centerIn: parent
            spacing: 8
            ThumbnailButton {
                objectName: "copyButton"
                text: "Copy"
                done: card.confirming === "copy"
                onClicked: card.act("copy")
            }
            ThumbnailButton {
                objectName: "saveButton"
                text: "Save"
                done: card.confirming === "save"
                onClicked: card.act("save")
            }
        }
        // Pin floats the capture above every window (a screenshot or a GIF Recording; not an MP4).
        ThumbnailButton {
            objectName: "pinButton"
            x: card.width - width - 8
            y: 8
            icon: "pin"
            tip: "Pin"
            visible: thumbnails.pinnable(card.captureId)
            enabled: card.confirming === ""
            onClicked: thumbnails.requestPin(card.captureId)
        }
        // A Recording's chip, "MP4 0:06", bottom-right. 
        Rectangle {
            objectName: "recordingChip"
            visible: card.chip !== ""
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 8
            height: 24
            width: chipText.implicitWidth + 20
            radius: height / 2
            color: Qt.rgba(0, 0, 0, 0.6)
            Text {
                id: chipText
                objectName: "chipText"
                anchors.centerIn: parent
                text: card.chip
                color: "white"
                font.family: Style.fontMono
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }
        // Close without saving.
        ThumbnailButton {
            objectName: "closeButton"
            x: 8
            y: 8
            icon: "x"
            tip: "Close"
            enabled: card.confirming === ""
            onClicked: thumbnails.close(card.captureId)
        }
        // Edit opens the Editor; on a Recording the same place is Trim (scissors).
        ThumbnailButton {
            objectName: "editButton"
            x: 8
            y: card.height - 36
            icon: card.chip === "" ? "pencil" : "scissors"
            tip: card.chip === "" ? "Edit" : "Trim"
            enabled: card.confirming === ""
            onClicked: thumbnails.edit(card.captureId)
        }
    }

    // Right-click: Save as…, Delete, Close all.
    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: !card.leaving && card.confirming === ""
        onTapped: (point) => {
            menu.x = Math.max(0, Math.min(card.width - menu.width, point.position.x));
            menu.y = Math.max(0, Math.min(card.height - menu.height, point.position.y));
            card.menuOpen = true;
        }
    }
    ThumbnailMenu {
        id: menu
        recording: card.chip !== ""
        visible: card.menuOpen && !card.leaving
        onSaveAs: { card.menuOpen = false; card.board.saveAsRequested(card.captureId); }
        onDeleteIt: { card.menuOpen = false; thumbnails.trash(card.captureId); }
        onCloseAll: { card.menuOpen = false; thumbnails.closeAll(); }
    }

    // One zone per card, reaching half the gap above and below so neighboring
    // zones meet: the pointer goes straight from one card to the next, never
    // through "none". Last, so it sits above everything in the card (the
    // right-click menu included) and sees the pointer first: below the menu, the
    // menu's rows took the hover and the card closed its menu under the pointer.
    // It only watches (a HoverHandler is passive), so the buttons and the menu
    // still get their hover and clicks. Pressing the card does nothing: a press
    // here is not handled at all.
    Item {
        y: -Style.thumbGap / 2
        width: parent.width
        height: parent.height + Style.thumbGap
        HoverHandler { id: hover }
    }
}
