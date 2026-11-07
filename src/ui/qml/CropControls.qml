import QtQuick

// The top toolbar while cropping (it replaces the tool bar; prototype-editor
// CropControls): aspect select, W x H fields, rotate and flip, the output size,
// Reset and Done. Everything calls the session, which records each change as one
// undo step. Esc, Enter or Done leaves Crop mode and keeps the Crop.
Item {
    id: root
    objectName: "cropControls"
    anchors.fill: parent
    visible: session.cropping

    // A number field. Enter or leaving the field sets the value (a number outside
    // the shot is clamped by the session, then the field shows what was set).
    component Field: Rectangle {
        id: fld
        property int value
        property string tip
        signal edited(int v)
        width: 64
        height: Style.ctlC
        radius: height / 2
        color: theme.control
        TextInput {
            id: input
            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
            text: String(fld.value)
            color: theme.text
            selectionColor: theme.accent
            selectedTextColor: theme.accentText
            font.family: Style.font
            font.pixelSize: 13
            selectByMouse: true
            validator: IntValidator { bottom: 1; top: 99999 }
            // Enter commits the field (the Editor's Enter shortcut would otherwise leave Crop mode).
            Keys.onShortcutOverride: (e) => e.accepted = e.key === Qt.Key_Return || e.key === Qt.Key_Enter
            onEditingFinished: {
                const v = parseInt(text);
                if (!isNaN(v))
                    fld.edited(v);
                text = Qt.binding(() => String(fld.value));
                focus = false;
            }
        }
        HoverHandler { id: hover }
        Tip { visible: hover.hovered && !input.activeFocus; text: fld.tip }
    }

    // Keys that only work while cropping.
    Shortcut { enabled: session.cropping; sequence: "Ctrl+R"; onActivated: session.rotateCrop(1) }
    Shortcut { enabled: session.cropping; sequence: "Ctrl+Shift+R"; onActivated: session.rotateCrop(-1) }
    Shortcut { enabled: session.cropping; sequence: "Ctrl+H"; onActivated: session.flipCropH() }
    Shortcut { enabled: session.cropping; sequence: "Ctrl+Shift+H"; onActivated: session.flipCropV() }
    Shortcut { enabled: session.cropping; sequence: "Ctrl+Backspace"; onActivated: session.resetCrop() }

    Row {
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: Style.spacing
        MenuPill {
            objectName: "cropAspectMenu"
            tip: "Aspect ratio (click to choose)"
            label: session.cropAspect
            current: session.cropAspect
            options: session.cropPresets.map(r => ({ v: r, label: r }))
            onPicked: (v) => session.setCropAspect(v)
        }
        Row {
            spacing: 6
            Field { objectName: "cropWidthField"; value: session.cropWidth; tip: "Width in px (type, then Enter)"; onEdited: (v) => session.setCropWidth(v) }
            Text { text: "×"; color: theme.text2; anchors.verticalCenter: parent.verticalCenter; font.pixelSize: 14 }
            Field { objectName: "cropHeightField"; value: session.cropHeight; tip: "Height in px (type, then Enter)"; onEdited: (v) => session.setCropHeight(v) }
        }
        Capsule {
            IconBtn { objectName: "rotateLeftButton"; pill: true; box: 28; boxW: 34; icon: "rotate"; tip: "Rotate left (Ctrl+Shift+R)"; onClicked: session.rotateCrop(-1) }
            IconBtn { objectName: "rotateRightButton"; pill: true; box: 28; boxW: 34; icon: "rotate-clockwise"; tip: "Rotate right (Ctrl+R)"; onClicked: session.rotateCrop(1) }
            Sep {}
            IconBtn { objectName: "flipHButton"; pill: true; box: 28; boxW: 34; icon: "flip-vertical"; tip: "Flip horizontally (Ctrl+H)"; onClicked: session.flipCropH() }
            IconBtn { objectName: "flipVButton"; pill: true; box: 28; boxW: 34; icon: "flip-horizontal"; tip: "Flip vertically (Ctrl+Shift+H)"; onClicked: session.flipCropV() }
        }
        MenuPill {
            objectName: "outputScaleMenu"
            tip: "Output size (click to choose)"
            label: session.outputSize.width + " × " + session.outputSize.height + " px"
            current: String(Math.round(session.outputScale * 100))
            options: [25, 50, 75, 100, 150, 200].map(p => ({ v: String(p), label: p + "%" }))
            onPicked: (v) => session.setOutputScale(parseInt(v) / 100)
        }
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: Style.spacing
        TextBtn { objectName: "cropResetButton"; text: "Reset"; tip: "Reset the Crop, turn, flip and size (Ctrl+Backspace)"; onClicked: session.resetCrop() }
        TextBtn { objectName: "cropDoneButton"; text: "Done"; primary: true; tip: "Done (Enter or Esc)"; onClicked: session.leaveCrop() }
    }
}
