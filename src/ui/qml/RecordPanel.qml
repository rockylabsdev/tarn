import QtQuick

// The Record panel (record mode, kept Selection): inside the Selection at its
// bottom center, or just outside it when the Selection is small. Appears
// instantly (it is opened from the keyboard: no animation).
//
//   row one   W x H fields (resize the Selection live, top-left fixed, Tab between) | settings
//   row two   Microphone | System audio | Show clicks   (audio grayed for a GIF)
//   menu      Record Video ↩ / Record GIF ⌥↩
//
// Layout follows prototype-indicator (RecFlow.qml), with Show clicks as a third
// cell of row two.
Item {
    id: panel
    required property var model       // the OverlayModel
    required property rect sel        // its Selection, logical px
    required property int outline
    readonly property var options: overlay.recordOptions
    readonly property int inset: 16
    property bool gifHover: false     // the pointer is on "Record GIF": the audio toggles gray out
    readonly property bool note: options.softwarePointer
    readonly property bool small: sel.width < width + 2 * inset || sel.height < height + 2 * inset

    visible: model.kept && model.recordMode
    width: 232
    height: 145 + (note ? 8 + noteText.implicitHeight : 0)
    // Where it sits: centered under the Selection's bottom edge, or just outside a small
    // Selection. If that would touch the Mode bar (a Selection near the bottom edge sends
    // the bar there too), the panel stacks on the bar instead: the same center line, a
    // 12 px gap, on the side towards the middle of the screen, so the two read as one group.
    readonly property point place: {
        const x = Math.max(0, Math.min(parent.width - width, sel.x + (sel.width - width) / 2));
        let y;
        if (!small) {
            y = sel.y + sel.height - inset - height;
        } else {
            const below = sel.y + sel.height + outline + inset;
            y = below + height <= parent.height ? below : Math.max(0, sel.y - outline - inset - height);
        }
        const bar = model.modeBarRect;
        const gap = 12;
        const overlaps = x < bar.x + bar.width + gap && x + width > bar.x - gap && y < bar.y + bar.height + gap && y + height > bar.y - gap;
        if (!overlaps)
            return Qt.point(x, y);
        const stackedX = Math.max(0, Math.min(parent.width - width, bar.x + (bar.width - width) / 2));
        const stackedY = bar.y > parent.height / 2 ? Math.max(0, bar.y - gap - height) : Math.min(parent.height - height, bar.y + bar.height + gap);
        return Qt.point(stackedX, stackedY);
    }
    x: place.x
    y: place.y

    component NumField: Rectangle {
        id: nf
        property int value
        property Item next: null
        property alias input: ti
        signal edited(int v)
        signal done
        width: 64; height: 24; radius: 8
        color: theme.raised
        border.width: 1
        border.color: ti.activeFocus ? theme.accent : theme.border
        TextInput {
            id: ti
            anchors.fill: parent; anchors.margins: 4
            horizontalAlignment: TextInput.AlignHCenter; verticalAlignment: TextInput.AlignVCenter
            color: theme.text; selectionColor: theme.accent; selectedTextColor: theme.accentText
            font.family: Style.fontMono; font.pixelSize: 13
            validator: IntValidator { bottom: 1; top: 9999 }
            selectByMouse: true
            KeyNavigation.tab: nf.next
            onActiveFocusChanged: if (activeFocus) selectAll()
            onTextEdited: { const v = parseInt(text); if (!isNaN(v)) nf.edited(v); }
            // Enter and Esc hand the keys back to the Overlay (Esc does not cancel it).
            Keys.onReturnPressed: event => { event.accepted = true; nf.done(); }
            Keys.onEnterPressed: event => { event.accepted = true; nf.done(); }
            Keys.onEscapePressed: event => { event.accepted = true; text = String(nf.value); nf.done(); }
        }
        Binding { target: ti; property: "text"; value: String(nf.value); when: !ti.activeFocus }
    }

    Rectangle {
        id: block
        width: 232; height: 65; radius: Style.radiusMenu
        color: theme.control
        NumField {
            id: wField
            objectName: "widthField"
            x: 16; y: 4
            value: panel.model.pixelWidth
            next: hField.input
            onEdited: v => panel.model.setPixelSize(v, panel.model.pixelHeight)
            onDone: panel.parent.forceActiveFocus()
        }
        Text {
            x: 16 + 64; y: 0; width: 24; height: 32
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            text: "×"; color: theme.muted; font.family: Style.font; font.pixelSize: 14
        }
        NumField {
            id: hField
            objectName: "heightField"
            x: 16 + 64 + 24; y: 4
            value: panel.model.pixelHeight
            next: wField.input
            onEdited: v => panel.model.setPixelSize(panel.model.pixelWidth, v)
            onDone: panel.parent.forceActiveFocus()
        }
        Item {
            x: 184; y: 0; width: 1; height: 32
            Sep { height: 32; anchors.verticalCenter: undefined }
        }
        IconBtn {
            objectName: "settingsButton"
            x: 184 + 6; y: 2
            pill: true; box: 28; boxW: 36
            icon: "adjustments-horizontal"
            tip: qsTr("Opens Tarn's settings")
            onClicked: overlay.openSettings()
        }
        Item {
            y: 32; width: 232; height: 1
            Sep { vertical: false }
        }

        // Row two: three equal cells. The audio toggles gray out over Record GIF.
        readonly property real cell: 232 / 3
        Item {
            x: block.cell; y: 33; width: 1; height: 32
            Sep { height: 32; anchors.verticalCenter: undefined }
        }
        Item {
            x: block.cell * 2; y: 33; width: 1; height: 32
            Sep { height: 32; anchors.verticalCenter: undefined }
        }
        IconBtn {
            objectName: "micToggle"
            x: block.cell / 2 - width / 2; y: 35
            pill: true; box: 28; boxW: 40
            icon: "microphone"
            active: panel.options.micOn && !panel.gifHover
            enabled: !panel.gifHover
            opacity: panel.gifHover ? 0.35 : 1
            tip: qsTr("Microphone")
            onClicked: panel.options.micOn = !panel.options.micOn
        }
        IconBtn {
            objectName: "systemAudioToggle"
            x: block.cell * 1.5 - width / 2; y: 35
            pill: true; box: 28; boxW: 40
            icon: "volume"
            active: panel.options.systemAudio && !panel.gifHover
            enabled: !panel.gifHover
            opacity: panel.gifHover ? 0.35 : 1
            tip: qsTr("System audio")
            onClicked: panel.options.systemAudio = !panel.options.systemAudio
        }
        IconBtn {
            objectName: "showClicksToggle"
            x: block.cell * 2.5 - width / 2; y: 35
            pill: true; box: 28; boxW: 40
            icon: "pointer"
            active: panel.options.showClicks
            tip: qsTr("Show clicks")
            onClicked: panel.options.showClicks = !panel.options.showClicks
        }
        Tip { parent: block; visible: panel.gifHover && panel.visible; text: qsTr("GIFs have no sound") }
    }

    Rectangle {
        y: 65 + 8
        width: 232; height: 72; radius: Style.radiusMenu
        color: theme.control
        Column {
            anchors.fill: parent; anchors.margins: 4
            Repeater {
                model: [{ name: "recordVideoButton", label: qsTr("Record Video"), hint: "↩", gif: false },
                        { name: "recordGifButton", label: qsTr("Record GIF"), hint: "⌥↩", gif: true }]
                Rectangle {
                    required property var modelData
                    width: parent.width; height: 32; radius: Style.radiusSm
                    color: pm.pressed ? theme.pressed : pm.containsMouse ? theme.hover : "transparent"
                    Rectangle {
                        x: 12; anchors.verticalCenter: parent.verticalCenter
                        visible: !parent.modelData.gif
                        width: 12; height: 12; radius: 6; color: Style.recordRed
                    }
                    Rectangle {
                        x: 7; anchors.verticalCenter: parent.verticalCenter
                        visible: parent.modelData.gif
                        width: 22; height: 14; radius: 4; color: "transparent"; border.color: theme.text2; border.width: 1
                        Text { anchors.centerIn: parent; text: "GIF"; color: theme.text2; font.family: Style.font; font.pixelSize: 8; font.weight: Font.Bold }
                    }
                    Text {
                        x: 40; anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData.label; color: theme.text
                        font.family: Style.font; font.pixelSize: 13; font.weight: Font.Medium
                    }
                    Text {
                        anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData.hint; color: theme.muted; font.family: Style.fontMono; font.pixelSize: 12
                    }
                    MouseArea {
                        id: pm
                        objectName: parent.modelData.name
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: parent.modelData.gif ? panel.model.confirmGif() : panel.model.confirm()
                        onEntered: if (parent.modelData.gif) panel.gifHover = true
                        onExited: if (parent.modelData.gif) panel.gifHover = false
                    }
                }
            }
        }
    }

    // Under Hyprland's software pointer the Setting "Show the pointer in Recordings" is moot.
    Text {
        id: noteText
        objectName: "pointerNote"
        visible: panel.note
        y: 145 + 8
        width: parent.width
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("Hyprland draws the pointer itself, so it is in every Recording, whatever the pointer Setting says.")
        color: theme.text2
        font.family: Style.font; font.pixelSize: 11
    }
}
