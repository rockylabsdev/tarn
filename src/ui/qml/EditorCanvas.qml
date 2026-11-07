import QtQuick

// The Editor's canvas: the shot (painted by DocumentCanvas, the same drawing the
// export uses) with no outline round it (a hairline would only add noise), at least 40 px
// of margin at Fit, zoom with Ctrl+scroll, and pan with Space-drag or middle-drag.
Item {
    id: view
    objectName: "canvas"
    clip: true
    property bool spaceDown: false

    onWidthChanged: session.setViewportSize(width, height)
    onHeightChanged: session.setViewportSize(width, height)
    Component.onCompleted: session.setViewportSize(width, height)

    DocumentCanvas {
        objectName: "documentCanvas"
        anchors.fill: parent
        editing: session
    }

    // The selection's Handles and the hover ghost, at screen size above every Annotation.
    SelectionChrome {
        objectName: "selectionChrome"
        anchors.fill: parent
    }

    // Crop mode: the Crop's frame, Handles and the dimmed rest (shown only while cropping).
    CropOverlay {
        objectName: "cropOverlay"
        anchors.fill: parent
    }

    MouseArea {
        id: mouse
        objectName: "canvasMouse"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        hoverEnabled: true
        property bool panning: false
        property bool drawing: false
        property point last
        cursorShape: panning ? Qt.ClosedHandCursor : view.spaceDown ? Qt.OpenHandCursor : session.eyedropper ? Qt.PointingHandCursor : session.cursor
        onPressed: (m) => {
            if (m.button === Qt.MiddleButton || view.spaceDown || session.tool === "hand") {
                panning = true;
                last = Qt.point(m.x, m.y);
            } else if (m.button === Qt.LeftButton) {
                drawing = true;
                const p = session.viewToDoc(m.x, m.y);
                session.pointerPressed(p.x, p.y, m.modifiers);
            }
        }
        onExited: session.pointerLeft()
        onDoubleClicked: (m) => { if (m.button === Qt.LeftButton) { const p = session.viewToDoc(m.x, m.y); if (!session.straightenAt(p.x, p.y)) session.editTextAt(p.x, p.y); } }
        onPositionChanged: (m) => {
            if (!panning && !drawing) {
                const h = session.viewToDoc(m.x, m.y);
                session.pointerHovered(h.x, h.y); // the hover ghost and resize pointers
            }
            if (panning) {
                session.panBy(m.x - last.x, m.y - last.y);
                last = Qt.point(m.x, m.y);
            } else if (drawing) {
                const p = session.viewToDoc(m.x, m.y);
                session.pointerMoved(p.x, p.y, m.modifiers);
            }
        }
        onReleased: (m) => {
            if (drawing) {
                const p = session.viewToDoc(m.x, m.y);
                session.pointerReleased(p.x, p.y, m.modifiers);
            }
            panning = false;
            drawing = false;
        }
    }
    // The Text tool's text field, above the mouse area so it takes clicks on the text.
    TextEditor { id: textEditor }
    // Inserts an emoji at the Text's cursor, starting a Text in the middle of the view first.
    function insertText(s) {
        if (!session.textEditing) {
            const c = session.viewToDoc(width / 2, height / 2);
            session.beginText(c.x, c.y);
        }
        textEditor.insertText(s);
    }
    // Ctrl+scroll zooms about the pointer; plain scroll pans. Both take any pointer: Wayland
    // doesn't say what a seat's pointer is, and Qt reports Hyprland's (a mouse wheel too)
    // as a touchpad, which a WheelHandler declines by default.
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedModifiers: Qt.ControlModifier
        onWheel: (e) => session.wheelZoom(e.angleDelta.y, point.position.x, point.position.y)
    }
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedModifiers: Qt.NoModifier
        onWheel: (e) => session.panBy(e.angleDelta.x / 2, e.angleDelta.y / 2)
    }
}
