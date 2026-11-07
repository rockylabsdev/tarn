import QtQuick
import QtQuick.Dialogs

// The Editor window (layout A of the editor prototype, in the Palette's colors): a
// 52 px top toolbar (close, the tool capsule, Save as…, Done), the canvas, and a
// 52 px bottom bar (zoom menu on the left, Copy on the right). The context
// properties are `session` (EditorSession: everything the user does) and `win`
// (the window: startMove). More controls join the bars with their tickets.
Item {
    id: root
    objectName: "editor"
    focus: true
    Component.onCompleted: forceActiveFocus()

    Rectangle { anchors.fill: parent; color: theme.canvas }

    // Save as…: the system file dialog, PNG / JPEG / WebP, in the last folder used.
    function saveAs() {
        saveAsLoader.active = true;
        saveAsLoader.item.start();
    }
    Loader {
        id: saveAsLoader
        active: false
        sourceComponent: FileDialog {
            fileMode: FileDialog.SaveFile
            nameFilters: ["PNG (*.png)", "JPEG (*.jpg *.jpeg)", "WebP (*.webp)"]
            function start() {
                currentFolder = "file://" + session.saveAsFolder();
                selectedNameFilter.index = session.saveAsFilterIndex();
                currentFile = "file://" + session.saveAsSuggestion();
                open();
            }
            onAccepted: {
                let path = decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, ""));
                if (!/\.(png|jpe?g|webp)$/i.test(path))
                    path += "." + selectedNameFilter.extensions[0];
                session.saveAs(path);
            }
        }
    }

    // Add image (I): the system file dialog, any picture Qt can read. Also reached by
    // dropping a file on the canvas and by Ctrl+V; all three end in the session
    // (addImageFile / addImage).
    function chooseImage() {
        imageLoader.active = true;
        imageLoader.item.open();
    }
    Connections { target: session; function onImageFileRequested() { root.chooseImage(); } }
    Loader {
        id: imageLoader
        active: false
        sourceComponent: FileDialog {
            fileMode: FileDialog.OpenFile
            nameFilters: ["Pictures (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.svg)"]
            onAccepted: session.addImageFile(selectedFile.toString())
        }
    }

    // ── Top toolbar ─────────────────────────────────────────────────────────────────
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
            visible: !session.cropping // Crop mode swaps the toolbar (CropControls)
            CapBtn { objectName: "closeButton"; icon: "x"; tip: "Close (Esc): drop the edits"; onClicked: session.cancel() }
            // Background (B): opens the Frame panel on the left.
            CapBtn { objectName: "backgroundButton"; icon: "background"; tip: "Background (B)"; active: root.framePanelOpen; onClicked: root.framePanelOpen = !root.framePanelOpen }
            // Watermark (W): your last Watermark on or off; with none yet, opens the panel at its Watermark section.
            CapBtn { objectName: "watermarkButton"; icon: "copyright"; tip: "Watermark (W)"; active: session.frame.watermark.mode !== "off"; onClicked: session.toggleWatermark() }
            CapBtn { objectName: "cropButton"; icon: "crop"; tip: "Crop (C)"; onClicked: session.enterCrop() }
            // One button per kind of Annotation the registry knows: a new tool shows up here by itself.
            Capsule {
                objectName: "toolCapsule"
                Repeater {
                    model: session.tools
                    // Each button sits in a slot. The Hand's slot opens only while the shot is larger
                    // than the view. A Spring opens and closes it (and fades the button with it), so
                    // zooming back across Fit mid-move bends the motion instead of restarting it.
                    Item {
                        id: slot
                        required property var modelData
                        readonly property bool shown: modelData.name !== "hand" || session.handAvailable
                        Spring {
                            id: grow
                            to: slot.shown ? toolBtn.width : 0
                            response: Style.toolRevealResponse
                            bounce: 0
                            Component.onCompleted: snap() // as it is when the Editor opens: no motion
                        }
                        width: grow.value
                        height: toolBtn.height
                        visible: width > 0.5
                        clip: width < toolBtn.width - 0.5 // only while opening or closing: a tooltip draws outside the button
                        IconBtn {
                            id: toolBtn
                            objectName: "tool-" + slot.modelData.name
                            pill: true
                            box: 28
                            boxW: 34
                            opacity: toolBtn.width > 0 ? Math.min(1, grow.value / toolBtn.width) : 1
                            icon: slot.modelData.icon
                            tip: slot.modelData.name === "hand" ? "Hand (G) \u00b7 or hold Space and drag, or middle-drag" : slot.modelData.label + " (" + slot.modelData.key + ")"
                            active: session.tool === slot.modelData.name
                            onClicked: session.tool = slot.modelData.name
                        }
                    }
                }
            }
            CapBtn { objectName: "addImageButton"; icon: "photo-plus"; tip: "Add image (I)"; onClicked: session.chooseImage() }
            // Options: only the current tool's selects. The color is shared by every
            // tool; the size select follows the tool's size group and is absent for a
            // tool with no size.
            ColorButton { id: colorButton }
            // The Text tool's own selects: its look (plain, outlined, label) and the emoji picker.
            MenuPill {
                objectName: "textLookMenu"
                visible: session.tool === "text"
                tip: "Text style (plain, outlined or label)"
                label: ["Plain", "Outlined", "Label"][session.textLook]
                current: String(session.textLook)
                options: [{ v: "0", label: "Plain" }, { v: "1", label: "Outlined" }, { v: "2", label: "Label" }]
                onPicked: (v) => session.textLook = parseInt(v)
            }
            EmojiButton {
                id: emojiButton
                visible: session.tool === "text"
                onPicked: (e) => canvas.insertText(e)
            }
            MenuPill {
                objectName: "sizeMenu"
                visible: session.sizeGroup !== ""
                tip: "Size ([ smaller, ] larger)"
                label: session.sizeLabel
                current: String(session.sizeIndex)
                options: session.sizeSteps.map((v, i) => ({ v: String(i), label: v + " " + (session.sizeGroup === "text" || session.sizeGroup === "counter" ? "pt" : "px") }))
                onPicked: (v) => session.sizeIndex = parseInt(v)
            }
            MenuPill {
                objectName: "arrowStyleMenu"
                visible: session.tool === "arrow"
                tip: "Arrow style (tapered, open head or double-headed)"
                label: ({ tapered: "Tapered", open: "Open head", double: "Double-headed" })[session.arrowStyle]
                current: session.arrowStyle
                options: [{ v: "tapered", label: "Tapered" }, { v: "open", label: "Open head" }, { v: "double", label: "Double-headed" }]
                onPicked: (v) => session.arrowStyle = v
            }
            // Counter only: the number style and the value of the first number.
            MenuPill {
                objectName: "counterStyleMenu"
                visible: session.tool === "counter"
                tip: "Number style (1 2 3, A B C or i ii iii)"
                label: session.counterPreview
                current: String(session.counterStyle)
                options: [{ v: "0", label: "1 2 3" }, { v: "1", label: "A B C" }, { v: "2", label: "i ii iii" }]
                onPicked: (v) => session.counterStyle = parseInt(v)
            }
            Rectangle {
                objectName: "counterStart"
                visible: session.tool === "counter"
                height: Style.ctlC
                width: 64
                radius: height / 2
                color: theme.control
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Start"
                    color: theme.text2
                    font.family: Style.font
                    font.pixelSize: 12
                }
                TextInput {
                    objectName: "counterStartInput"
                    anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                    text: String(session.counterStart)
                    color: theme.text
                    font.family: Style.font
                    font.pixelSize: 13
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 0; top: 999 }
                    onEditingFinished: { session.counterStart = parseInt(text || "1"); text = String(session.counterStart); }
                }
            }
            // Extra selects of the current tool (Pixelate style and strength, Spotlight shape).
            Repeater {
                model: session.choices
                MenuPill {
                    required property var modelData
                    objectName: "choice-" + modelData.name
                    tip: modelData.tip
                    label: modelData.label
                    current: modelData.current
                    options: modelData.options
                    onPicked: (v) => session.setChoice(modelData.name, v)
                }
            }
        }
        CropControls {}
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: Style.spacing
            visible: !session.cropping
            TextBtn { objectName: "saveAsButton"; text: "Save as…"; tip: "Save as… (Ctrl+Shift+S)"; onClicked: root.saveAs() }
            TextBtn { objectName: "doneButton"; text: "Done"; primary: true; tip: "Done (Enter)"; onClicked: session.done() }
        }
    }

    // The Background panel: opens at once and pushes the canvas over.
    property bool framePanelOpen: false
    FramePanel {
        id: framePanel
        visible: root.framePanelOpen
        anchors { left: parent.left; top: top.bottom; bottom: bottom.top }
    }

    EditorCanvas {
        id: canvas
        anchors { left: root.framePanelOpen ? framePanel.right : parent.left; right: parent.right; top: top.bottom; bottom: bottom.top }
        spaceDown: root.spaceDown
    }

    // Dropping picture files on the canvas adds them, centered where they were dropped.
    DropArea {
        objectName: "imageDrop"
        anchors.fill: canvas
        keys: ["text/uri-list"]
        onDropped: (drop) => {
            const p = session.viewToDoc(drop.x, drop.y);
            for (let i = 0; i < drop.urls.length; i++)
                session.addImageFile(drop.urls[i].toString(), p.x + 20 * i, p.y + 20 * i);
        }
    }

    // ── Bottom bar ──────────────────────────────────────────────────────────────────
    Rectangle {
        id: bottom
        objectName: "bottomBar"
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Style.barHeight
        color: theme.surface
        MoveArea {}
        Sep { vertical: false }
        MenuPill {
            objectName: "zoomMenu"
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            up: true
            tip: "Zoom (Ctrl+scroll)"
            label: session.fit ? "Fit · " + session.zoomPercent + "%" : session.zoomPercent + "%"
            current: session.fit ? "Fit" : ""
            options: [{ v: "Fit", label: "Fit" }, { v: "25", label: "25%" }, { v: "50", label: "50%" }, { v: "100", label: "100%" }, { v: "200", label: "200%" }, { v: "400", label: "400%" }]
            onPicked: (v) => v === "Fit" ? session.zoomFit() : session.zoomTo(parseInt(v) / 100)
        }
        // Pin and Copy, right-aligned.
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: Style.spacing
            CapBtn { objectName: "pinButton"; icon: "pin"; tip: "Pin (Ctrl+P)"; onClicked: session.pin() }
            CapBtn { objectName: "copyButton"; icon: "copy"; tip: "Copy and close (Ctrl+C)"; onClicked: session.copyAndClose() }
        }
    }

    // ── Keys ────────────────────────────────────────────────────────────────────────
    // Space held turns a drag into a pan.
    property bool spaceDown: false
    Keys.onPressed: (e) => { if (e.key === Qt.Key_Space && !e.isAutoRepeat) { spaceDown = true; e.accepted = true; } }
    Keys.onReleased: (e) => { if (e.key === Qt.Key_Space && !e.isAutoRepeat) { spaceDown = false; e.accepted = true; } }

    // Esc steps back one level: an open Popover (Popover.qml), then the eyedropper, then
    // session.escape (a Text being typed, Crop mode, the selection, then the Editor).
    Shortcut { sequence: "Escape"; onActivated: { if (!Popovers.closeNewestIn(root)) session.eyedropper ? session.cancelEyedropper() : session.escape(); } }
    Shortcut { sequence: "Ctrl+."; enabled: session.tool === "text"; onActivated: emojiButton.open() }
    Connections { target: session; function onTextEditingChanged() { if (!session.textEditing) root.forceActiveFocus(); } }
    Shortcut { sequence: "["; onActivated: session.stepSize(-1) }
    Shortcut { sequence: "]"; onActivated: session.stepSize(1) }
    Shortcut { sequence: "K"; onActivated: colorButton.open() }
    Shortcut { sequence: "B"; onActivated: root.framePanelOpen = !root.framePanelOpen }
    Shortcut { sequence: "W"; onActivated: session.toggleWatermark() }
    Connections {
        target: session
        function onWatermarkSetupRequested() {
            root.framePanelOpen = true;
            Qt.callLater(framePanel.showWatermark);
        }
    }
    Shortcut { sequence: "E"; onActivated: session.beginEyedropper() }
    // Enter edits a selected Callout; otherwise it is Done, except while typing a Text (it adds a line) or searching emoji; in Crop mode it leaves Crop instead.
    // A panel's text field (the Watermark text) takes Enter itself: it ends the typing, not the Editor.
    readonly property bool typingInField: Window.activeFocusItem instanceof TextInput
    Shortcut { sequences: ["Return", "Enter"]; enabled: !session.textEditing && !emojiButton.popup.opened && !root.typingInField; onActivated: { if (!session.editSelectedCallout()) session.cropping ? session.leaveCrop() : session.done(); } }
    // Ctrl+Enter is Done even while typing a Text.
    Shortcut { sequences: ["Ctrl+Return", "Ctrl+Enter"]; onActivated: session.cropping ? session.leaveCrop() : session.done() }
    Shortcut { sequence: "C"; onActivated: session.enterCrop() }
    // Ctrl+C with Annotations selected copies them (Ctrl+V pastes, in any Editor);
    // with nothing selected it copies the image and closes.
    Shortcut { sequence: "Ctrl+C"; onActivated: session.selectionCount > 0 ? session.copySelection() : session.copyAndClose() }
    Shortcut { sequence: "Ctrl+V"; onActivated: session.pasteClipboard() }
    Shortcut { sequence: "I"; onActivated: session.chooseImage() }
    // Selecting and editing Annotations.
    Shortcut { sequence: "Ctrl+A"; onActivated: session.selectAll() }
    Shortcut { sequences: ["Delete", "Backspace"]; onActivated: session.deleteSelection() }
    Shortcut { sequence: "Ctrl+D"; onActivated: session.duplicateSelection() }
    Shortcut { sequence: "Ctrl+]"; onActivated: session.bringForward() }
    Shortcut { sequence: "Ctrl+["; onActivated: session.sendBackward() }
    Shortcut { sequence: "Left"; onActivated: session.nudge(-1, 0) }
    Shortcut { sequence: "Right"; onActivated: session.nudge(1, 0) }
    Shortcut { sequence: "Up"; onActivated: session.nudge(0, -1) }
    Shortcut { sequence: "Down"; onActivated: session.nudge(0, 1) }
    Shortcut { sequence: "Shift+Left"; onActivated: session.nudge(-1, 0, true) }
    Shortcut { sequence: "Shift+Right"; onActivated: session.nudge(1, 0, true) }
    Shortcut { sequence: "Shift+Up"; onActivated: session.nudge(0, -1, true) }
    Shortcut { sequence: "Shift+Down"; onActivated: session.nudge(0, 1, true) }
    // Unlimited undo and redo, through the Capture document's one history.
    Shortcut { sequence: "Ctrl+Z"; onActivated: session.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; onActivated: session.redo() }
    Shortcut { sequence: "Ctrl+Shift+C"; onActivated: session.copyKeepOpen() }
    Shortcut { sequence: "Ctrl+S"; onActivated: session.saveAndClose() }
    Shortcut { sequence: "Ctrl+P"; onActivated: session.pin() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: root.saveAs() }
    Shortcut { sequence: "Ctrl+0"; onActivated: session.zoomFit() }
    Shortcut { sequence: "Ctrl+1"; onActivated: session.zoomTo(1) }
    Shortcut { sequences: ["Ctrl+=", "Ctrl++"]; onActivated: session.zoomIn() }
    Shortcut { sequence: "Ctrl+-"; onActivated: session.zoomOut() }
    // Tool keys come from the registry too.
    Repeater {
        model: session.tools
        Item {
            required property var modelData
            Shortcut { sequence: modelData.key; onActivated: session.tool = modelData.name }
        }
    }
}
