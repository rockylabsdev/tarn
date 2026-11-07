import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtCore

// The Background panel: 284 px on the Editor's left, it pushes the canvas
// over. It puts a Frame round the shot: None, plain colors (fixed swatches, the
// transparent swatch, a custom color, Palette swatches), then Padding, Inset,
// Auto-balance, Shadow, Corners, Alignment and Ratio. Everything it changes goes
// through the session (`session.frame` to read, `setFrame…` to write), so every
// change is one undo step. The tile sections (gradients, Palette gradients,
// wallpapers with "+", blurred shot) use `Tile`, which shows a session list entry.
// The Palette swatches and gradients show only while a Palette file is in use.
// Controls change state instantly, as everywhere in Tarn.
Rectangle {
    id: root
    objectName: "framePanel"
    width: Style.framePanelWidth
    color: theme.surface

    // "+": any image, from the system file dialog.
    Connections { target: session; function onBackgroundImageRequested() { wallpaperLoader.active = true; wallpaperLoader.item.open(); } }
    Loader {
        id: wallpaperLoader
        active: false
        sourceComponent: FileDialog {
            fileMode: FileDialog.OpenFile
            nameFilters: ["Pictures (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.svg)"]
            onAccepted: session.setFrameWallpaper(selectedFile.toString())
        }
    }
    // Look at the wallpaper and the folder again each time the panel opens.
    onVisibleChanged: if (visible) session.refreshBackgrounds()

    readonly property var f: session.frame
    readonly property var wm: f.watermark
    readonly property var range: session.frameRanges
    readonly property int inner: Style.framePanelInner

    Sep { vertical: true; anchors.right: parent.right }

    // A small gray section title.
    component Caption: Text {
        color: theme.text2
        font.family: Style.font
        font.pixelSize: 12
    }
    // The accent ring round the chosen swatch.
    component Ring: Rectangle {
        property bool on: false
        property real r: 10
        visible: on
        anchors.fill: parent
        anchors.margins: -3
        radius: r + 3
        color: "transparent"
        border.width: 2
        border.color: theme.accent
    }
    // A picture tile for a gradient, wallpaper or blurred shot (`entry` is a session list entry).
    component Tile: Rectangle {
        id: tile
        property var entry
        property bool on: false
        signal picked
        width: Style.frameTileWidth
        height: Style.frameTileHeight
        radius: 8
        color: theme.control
        clip: false
        Rounded {
            anchors.fill: parent
            radius: 8
            Image {
                objectName: "tileImage"
                anchors.fill: parent
                source: tile.entry.tile
                sourceSize: Qt.size(Style.frameTileWidth * 2, Style.frameTileHeight * 2)
                fillMode: Image.Stretch
                cache: false
            }
        }
        Rectangle { anchors.fill: parent; radius: 8; color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.2) }
        Ring { on: tile.on; r: 8 }
        Tip { visible: tileArea.containsMouse; text: tile.entry.label }
        MouseArea { id: tileArea; anchors.fill: parent; hoverEnabled: true; onClicked: tile.picked() }
    }
    // A slider over 0..1 of its range: a 4 px track, accent fill and a round thumb.
    // `moved` while dragging, `released` at the end (one undo step for the whole drag).
    component PSlider: Item {
        id: slider
        property real value: 0
        property string tipText: Math.round(value * 100)
        signal moved(real v)
        signal released
        height: 24
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 4
            radius: 2
            color: theme.track
            Rectangle { width: slider.value * parent.width; height: parent.height; radius: 2; color: theme.accent }
        }
        Rectangle {
            x: slider.value * (slider.width - width)
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            radius: 8
            color: theme.text
        }
        ValueTip { visible: dragArea.pressed; knobX: 8 + slider.value * (slider.width - 16); text: slider.tipText }
        MouseArea {
            id: dragArea
            anchors.fill: parent
            function set(mx) { slider.moved(Math.max(0, Math.min(1, (mx - 8) / (width - 16)))); }
            onPressed: (m) => set(m.x)
            onPositionChanged: (m) => { if (pressed) set(m.x); }
            onReleased: slider.released()
        }
    }

    // The W button with no Watermark remembered: scroll to the Watermark section.
    function showWatermark() {
        flick.contentY = Math.max(0, Math.min(watermarkSection.mapToItem(flick.contentItem, 0, 0).y, flick.contentHeight - flick.height));
    }

    Flickable {
        id: flick
        objectName: "frameScroll"
        anchors.fill: parent
        contentHeight: col.implicitHeight + 28
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: col
            x: (flick.width - root.inner) / 2
            y: 14
            width: root.inner
            spacing: 10

            // None: no Frame at all.
            Rectangle {
                objectName: "noneButton"
                width: root.inner
                height: 32
                radius: Style.radiusSm
                color: !root.f.on ? theme.accent : noneArea.pressed ? theme.pressed : noneArea.containsMouse ? theme.hover : theme.control
                Text {
                    anchors.centerIn: parent
                    text: "None"
                    color: !root.f.on ? theme.accentText : theme.text
                    font.family: Style.font
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
                MouseArea { id: noneArea; anchors.fill: parent; hoverEnabled: true; onClicked: session.setFrameBackground("none") }
            }

            Caption { text: "Plain color"; topPadding: 6 }
            // The fixed swatches, then the transparent one.
            Flow {
                width: root.inner
                spacing: 8
                Repeater {
                    model: ["#1c1c1e", "#ffffff", "#ff453a", "#ff9f0a", "#ffd60a", "#30d158", "#0a84ff", "#bf5af2", "#00000000"]
                    Rectangle {
                        id: sw
                        required property string modelData
                        required property int index
                        objectName: "frameSwatch-" + index
                        readonly property bool clear: modelData === "#00000000"
                        width: 24
                        height: 24
                        radius: 12
                        color: clear ? "#f2f2f2" : modelData
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.2)
                        Ring { on: root.f.on && root.f.kind === "plain" && Qt.colorEqual(root.f.color, sw.modelData); r: 12 }
                        // Transparent: a white disc with a red slash.
                        Rectangle { visible: sw.clear; anchors.centerIn: parent; width: 2; height: 20; rotation: 45; color: Style.recordRed }
                        MouseArea { anchors.fill: parent; onClicked: session.setFrameBackground("plain", { color: sw.modelData }) }
                    }
                }
                // A custom color: a rainbow swatch that opens the picker.
                Rectangle {
                    id: customSwatch
                    objectName: "customSwatch"
                    width: 24
                    height: 24
                    radius: 12
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.2)
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "#ff453a" }
                        GradientStop { position: 0.35; color: "#ffd60a" }
                        GradientStop { position: 0.65; color: "#30d158" }
                        GradientStop { position: 1; color: "#bf5af2" }
                    }
                    MouseArea { anchors.fill: parent; onClicked: custom.toggle() }
                }
            }

            // The Palette file's colors, live; left out while none is in use. The slot name is kept so a Preset can follow the wallpaper.
            Caption { objectName: "paletteSwatchesCaption"; text: "From your wallpaper"; visible: theme.fromFile }
            Flow {
                objectName: "paletteSwatches"
                visible: theme.fromFile
                width: root.inner
                spacing: 8
                Repeater {
                    model: [{ slot: "accent", c: theme.accent }, { slot: "text", c: theme.text }, { slot: "text2", c: theme.text2 },
                            { slot: "muted", c: theme.muted }, { slot: "canvas", c: theme.canvas }, { slot: "border", c: theme.border }]
                    Rectangle {
                        id: ps
                        required property var modelData
                        required property int index
                        objectName: "paletteSwatch-" + index
                        width: 24
                        height: 24
                        radius: 12
                        color: modelData.c
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.2)
                        Ring { on: root.f.on && root.f.kind === "plain" && root.f.slot === ps.modelData.slot; r: 12 }
                        MouseArea { anchors.fill: parent; onClicked: session.setFrameBackground("plain", { color: ps.modelData.c.toString(), slot: ps.modelData.slot }) }
                    }
                }
            }

            Caption { text: "Saved" }
            SavedRow {
                width: root.inner
                canSave: session.frameIsPlain
                current: root.f.on && root.f.kind === "plain" ? Qt.color(root.f.color) : "transparent"
                onPicked: (v) => session.setFrameBackground("plain", { color: v.toString() })
                onPlusClicked: session.saveFrameColor()
            }

            Caption { text: "Gradients" }
            Flow {
                width: root.inner
                spacing: 8
                Repeater {
                    model: session.gradients
                    Tile {
                        required property var modelData
                        required property int index
                        objectName: "gradientTile-" + index
                        entry: modelData
                        on: root.f.on && root.f.kind === "image-gradient" && root.f.background.name === modelData.id
                        onPicked: session.setFrameGradient(modelData.id)
                    }
                }
            }

            // Gradients from the Palette: only while a Palette file is in use.
            Caption { text: "Gradients from your wallpaper"; visible: session.paletteGradients.length > 0 }
            Flow {
                objectName: "paletteGradients"
                width: root.inner
                spacing: 8
                visible: session.paletteGradients.length > 0
                Repeater {
                    model: session.paletteGradients
                    Tile {
                        required property var modelData
                        required property int index
                        objectName: "paletteGradientTile-" + index
                        entry: modelData
                        on: root.f.on && root.f.kind === "gradient" && !!root.f.background.slots && root.f.background.id === modelData.id
                        onPicked: session.setFramePaletteGradient(modelData.id)
                    }
                }
            }

            // The current wallpaper first, then the wallpaper folder, then "+".
            Caption { text: "Wallpapers" }
            Flow {
                objectName: "wallpapers"
                width: root.inner
                spacing: 8
                Repeater {
                    model: session.wallpapers
                    Tile {
                        required property var modelData
                        required property int index
                        objectName: "wallpaperTile-" + index
                        entry: modelData
                        on: root.f.on && root.f.kind === "wallpaper" && root.f.background.path === modelData.id
                        onPicked: session.setFrameWallpaper(modelData.id, !!modelData.current)
                    }
                }
                Rectangle {
                    objectName: "addWallpaperTile"
                    width: Style.frameTileWidth
                    height: Style.frameTileHeight
                    radius: 8
                    color: plusArea.containsMouse ? theme.hover : theme.control
                    Icon { anchors.centerIn: parent; name: "plus"; size: 18; color: theme.text2 }
                    Tip { visible: plusArea.containsMouse; text: "Use any image" }
                    MouseArea { id: plusArea; anchors.fill: parent; hoverEnabled: true; onClicked: session.chooseBackgroundImage() }
                }
            }
            // A deleted wallpaper file falls back to a plain dark gradient; this is the only mark.
            Caption { objectName: "wallpaperMissing"; visible: !!root.f.missing; text: "That picture is gone; showing a plain fallback"; color: theme.muted; width: root.inner; wrapMode: Text.WordWrap }

            Caption { text: "Blurred shot" }
            Flow {
                objectName: "blurTiles"
                width: root.inner
                spacing: 8
                Repeater {
                    model: session.blurTiles
                    Tile {
                        required property var modelData
                        required property int index
                        objectName: "blurTile-" + index
                        entry: modelData
                        on: root.f.on && root.f.kind === "blur" && root.f.background.style === modelData.id
                        onPicked: session.setFrameBlur(modelData.id)
                    }
                }
            }

            Rectangle { width: root.inner; height: 1; color: theme.divider }

            Caption { text: "Padding"; topPadding: 6 }
            PSlider {
                objectName: "paddingSlider"
                width: root.inner
                value: root.f.padding / root.range.padding
                onMoved: (v) => session.dragFrameValue("padding", v * root.range.padding)
                onReleased: session.endFrameDrag()
            }
            Row {
                spacing: 16
                Column {
                    spacing: 4
                    Caption { text: "Inset" }
                    PSlider {
                        objectName: "insetSlider"
                        width: 116
                        value: root.f.inset / root.range.inset
                        onMoved: (v) => session.dragFrameValue("inset", v * root.range.inset)
                        onReleased: session.endFrameDrag()
                    }
                }
                // Auto-balance: each side of the Inset band takes its own edge's color.
                Row {
                    objectName: "autoBalance"
                    anchors.bottom: parent.bottom
                    spacing: 8
                    Rectangle {
                        width: 18
                        height: 18
                        radius: 5
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.f.autoBalance ? theme.accent : theme.control
                        border.color: root.f.autoBalance ? theme.accent : theme.muted
                        Icon { visible: root.f.autoBalance; anchors.centerIn: parent; name: "check"; size: 14; color: theme.accentText }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Auto-balance"
                        color: theme.text
                        font.family: Style.font
                        font.pixelSize: 13
                    }
                    TapHandler { onTapped: session.setFrameValue("autoBalance", !root.f.autoBalance) }
                }
            }
            Row {
                spacing: 16
                Column {
                    spacing: 4
                    Caption { text: "Shadow" }
                    PSlider {
                        objectName: "shadowSlider"
                        width: 116
                        value: root.f.shadow / root.range.shadow
                        onMoved: (v) => session.dragFrameValue("shadow", v * root.range.shadow)
                        onReleased: session.endFrameDrag()
                    }
                }
                Column {
                    spacing: 4
                    Caption { text: "Corners" }
                    PSlider {
                        objectName: "cornersSlider"
                        width: 116
                        value: root.f.corners / root.range.corners
                        onMoved: (v) => session.dragFrameValue("corners", v * root.range.corners)
                        onReleased: session.endFrameDrag()
                    }
                }
            }
            Row {
                spacing: 16
                Column {
                    spacing: 4
                    Caption { text: "Alignment" }
                    // The zones: an outlined frame with a block where the shot sits;
                    // the chosen zone is white.
                    Grid {
                        columns: 3
                        spacing: 6
                        Repeater {
                            model: 9
                            Item {
                                id: zone
                                required property int index
                                objectName: "alignZone-" + index
                                readonly property int ah: index % 3 - 1
                                readonly property int av: Math.floor(index / 3) - 1
                                readonly property bool on: root.f.ah === ah && root.f.av === av
                                readonly property color ink: on ? theme.text : za.containsMouse ? theme.text2 : theme.muted
                                width: 34
                                height: 24
                                Rectangle {
                                    width: 15
                                    height: 10
                                    radius: 2.5
                                    color: zone.ink
                                    x: 2 + (zone.ah < 0 ? 0 : zone.ah > 0 ? parent.width - 4 - width : (parent.width - 4 - width) / 2)
                                    y: 2 + (zone.av < 0 ? 0 : zone.av > 0 ? parent.height - 4 - height : (parent.height - 4 - height) / 2)
                                }
                                Rectangle { anchors.fill: parent; radius: 6; color: "transparent"; border.width: 1.5; border.color: zone.ink }
                                MouseArea {
                                    id: za
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: { session.setFrameValue("ah", zone.ah); session.setFrameValue("av", zone.av); }
                                }
                            }
                        }
                    }
                }
                Column {
                    spacing: 4
                    Caption { text: "Ratio" }
                    MenuPill {
                        objectName: "ratioMenu"
                        label: root.f.ratio
                        current: ratioList.indexOf(root.f.ratio) >= 0 ? root.f.ratio : "Custom"
                        readonly property var ratioList: ["Auto", "1:1", "4:3", "3:2", "16:9", "9:16"]
                        options: ratioList.map((r) => ({ v: r, label: r })).concat([{ v: "Custom", label: "Custom…" }])
                        onPicked: (v) => v === "Custom" ? customRatio.forceActiveFocus() : session.setFrameValue("ratio", v)
                    }
                    // Custom: type w:h (any two numbers) and press Enter.
                    Rectangle {
                        width: 112
                        height: 26
                        radius: 13
                        color: theme.control
                        border.color: customRatio.activeFocus ? theme.accent : "transparent"
                        TextInput {
                            id: customRatio
                            objectName: "customRatio"
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            verticalAlignment: TextInput.AlignVCenter
                            color: theme.text
                            font.family: Style.font
                            font.pixelSize: 12
                            selectByMouse: true
                            onAccepted: { session.setFrameValue("ratio", text); focus = false; }
                            Text { visible: !parent.text && !parent.activeFocus; text: "Custom w:h"; color: theme.muted; font: parent.font; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                }
            }

            Rectangle { width: root.inner; height: 1; color: theme.divider }

            // Presets: saved Frames. Click one to apply it; "Auto" marks the one that
            // frames every capture (B in the Overlay skips it once); x deletes it.
            Caption { text: "Presets"; topPadding: 6 }
            Column {
                objectName: "presets"
                width: root.inner
                spacing: 4
                Repeater {
                    model: session.presets
                    Rectangle {
                        id: presetRow
                        required property var modelData
                        required property int index
                        objectName: "presetRow-" + index
                        width: root.inner
                        height: 28
                        radius: 14
                        color: presetArea.containsMouse ? theme.hover : theme.control
                        MouseArea { id: presetArea; anchors.fill: parent; hoverEnabled: true; onClicked: session.applyPreset(presetRow.modelData.name) }
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.right: autoChip.left
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: presetRow.modelData.name + (presetRow.modelData.missing ? "  (picture gone)" : "")
                            color: theme.text
                            font.family: Style.font
                            font.pixelSize: 12
                        }
                        Rectangle {
                            id: autoChip
                            objectName: "presetAuto-" + presetRow.index
                            anchors.right: removeChip.left
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: 44
                            height: 20
                            radius: 10
                            color: presetRow.modelData.auto ? theme.accent : "transparent"
                            border.width: 1
                            border.color: presetRow.modelData.auto ? theme.accent : theme.border
                            Text { anchors.centerIn: parent; text: "Auto"; font.family: Style.font; font.pixelSize: 11; color: presetRow.modelData.auto ? theme.accentText : theme.text2 }
                            Tip { visible: autoArea.containsMouse; text: "Frame every capture with this" }
                            MouseArea { id: autoArea; anchors.fill: parent; hoverEnabled: true; onClicked: session.setAutoPreset(presetRow.modelData.auto ? "" : presetRow.modelData.name) }
                        }
                        Rectangle {
                            id: removeChip
                            objectName: "presetDelete-" + presetRow.index
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: 20
                            height: 20
                            radius: 10
                            color: removeArea.containsMouse ? theme.pressed : "transparent"
                            Icon { anchors.centerIn: parent; name: "x"; size: 12; color: theme.text2 }
                            Tip { visible: removeArea.containsMouse; text: "Delete this preset" }
                            MouseArea { id: removeArea; anchors.fill: parent; hoverEnabled: true; onClicked: session.deletePreset(presetRow.modelData.name) }
                        }
                    }
                }
                Row {
                    spacing: 6
                    Rectangle {
                        width: root.inner - 66
                        height: 26
                        radius: 13
                        color: theme.control
                        border.color: presetName.activeFocus ? theme.accent : "transparent"
                        TextInput {
                            id: presetName
                            objectName: "presetName"
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            verticalAlignment: TextInput.AlignVCenter
                            color: theme.text
                            font.family: Style.font
                            font.pixelSize: 12
                            selectByMouse: true
                            onAccepted: savePresetButton.save()
                            Text { visible: !parent.text && !parent.activeFocus; text: "Preset name"; color: theme.muted; font: parent.font; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                    Rectangle {
                        id: savePresetButton
                        objectName: "savePresetButton"
                        width: 60
                        height: 26
                        radius: 13
                        opacity: root.f.on && presetName.text.trim() !== "" ? 1 : 0.5
                        color: saveArea.containsMouse ? theme.hover : theme.control
                        function save() { if (root.f.on && session.savePreset(presetName.text)) { presetName.text = ""; presetName.focus = false; } }
                        Text { anchors.centerIn: parent; text: "Save"; font.family: Style.font; font.pixelSize: 12; color: theme.text }
                        MouseArea { id: saveArea; anchors.fill: parent; hoverEnabled: true; onClicked: savePresetButton.save() }
                    }
                }
            }

            // Watermark: an image or one line of text in a corner of the shot. Off / Image / Text,
            // a chooser (opens in Pictures) or a text field, the four-dot corner picker, Size and Opacity.
            // Each change is one undo step (sliders: the whole drag).
            Caption { text: "Watermark"; topPadding: 6 }
            Column {
                id: watermarkSection
                objectName: "watermarkSection"
                width: root.inner
                spacing: 8
                readonly property var w: root.f.watermark
                Row {
                    spacing: 4
                    Repeater {
                        model: [{ key: "off", label: "Off" }, { key: "image", label: "Image" }, { key: "text", label: "Text" }]
                        Rectangle {
                            required property var modelData
                            objectName: "watermarkMode-" + modelData.key
                            width: (root.inner - 8) / 3
                            height: 28
                            radius: 14
                            color: root.wm.mode === modelData.key ? theme.accent : modeArea.containsMouse ? theme.hover : theme.control
                            Text {
                                anchors.centerIn: parent
                                text: parent.modelData.label
                                color: root.wm.mode === parent.modelData.key ? theme.accentText : theme.text
                                font.family: Style.font
                                font.pixelSize: 12
                            }
                            MouseArea {
                                id: modeArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (parent.modelData.key === "image" && !root.wm.hasImage) watermarkLoader.openChooser();
                                    else session.setFrameValue("wmMode", parent.modelData.key);
                                }
                            }
                        }
                    }
                }
                Loader {
                    id: watermarkLoader
                    active: false
                    function openChooser() { active = true; item.open(); }
                    sourceComponent: FileDialog {
                        fileMode: FileDialog.OpenFile
                        currentFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)
                        nameFilters: ["Pictures (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.svg)"]
                        onAccepted: session.setWatermarkImage(selectedFile.toString())
                    }
                }
                Rectangle {
                    objectName: "watermarkChoose"
                    visible: root.wm.mode === "image"
                    width: root.inner
                    height: 28
                    radius: 14
                    color: chooseArea.containsMouse ? theme.hover : theme.control
                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 12
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideMiddle
                        text: root.wm.path ? root.wm.path.split("/").pop() : "Choose an image..."
                        color: theme.text
                        font.family: Style.font
                        font.pixelSize: 12
                    }
                    MouseArea { id: chooseArea; anchors.fill: parent; hoverEnabled: true; onClicked: watermarkLoader.openChooser() }
                }
                Rectangle {
                    visible: root.wm.mode === "text"
                    width: root.inner
                    height: 28
                    radius: 14
                    color: theme.control
                    border.color: watermarkText.activeFocus ? theme.accent : "transparent"
                    TextInput {
                        id: watermarkText
                        objectName: "watermarkText"
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter
                        color: theme.text
                        font.family: Style.font
                        font.pixelSize: 12
                        selectByMouse: true
                        text: root.wm.text
                        // Shown as you type, like a slider drag; Enter or leaving the field ends it (one undo step).
                        onTextEdited: session.dragFrameValue("wmText", text)
                        onEditingFinished: { session.endFrameDrag(); focus = false; }
                        Text { visible: !parent.text && !parent.activeFocus; text: "Your name or site"; color: theme.muted; font: parent.font; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
                Row {
                    visible: root.wm.mode !== "off"
                    spacing: 10
                    Caption { text: "Corner"; anchors.verticalCenter: parent.verticalCenter; width: 56 }
                    // The four-dot corner picker: one dot in each corner of a small shot.
                    Rectangle {
                        objectName: "watermarkCorners"
                        width: 56
                        height: 38
                        radius: 6
                        color: theme.control
                        Repeater {
                            model: 4
                            Rectangle {
                                required property int index
                                objectName: "watermarkCorner-" + index
                                x: (index % 2) ? parent.width - width - 4 : 4
                                y: (index >= 2) ? parent.height - height - 4 : 4
                                width: 12
                                height: 12
                                radius: 6
                                color: root.wm.corner === index ? theme.accent : theme.track
                                MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: session.setFrameValue("wmCorner", parent.index) }
                            }
                        }
                    }
                }
                Row {
                    visible: root.wm.mode !== "off"
                    spacing: 10
                    Caption { text: "Size"; anchors.verticalCenter: parent.verticalCenter; width: 56 }
                    PSlider {
                        objectName: "watermarkSize"
                        width: root.inner - 66
                        readonly property real lo: root.range.wmMinSize
                        readonly property real hi: root.range.wmMaxSize
                        value: (root.wm.size - lo) / (hi - lo)
                        tipText: Math.round(root.wm.size * 100) + "%"
                        onMoved: (v) => session.dragFrameValue("wmSize", lo + v * (hi - lo))
                        onReleased: session.endFrameDrag()
                    }
                }
                Row {
                    visible: root.wm.mode !== "off"
                    spacing: 10
                    Caption { text: "Opacity"; anchors.verticalCenter: parent.verticalCenter; width: 56 }
                    PSlider {
                        objectName: "watermarkOpacity"
                        width: root.inner - 66
                        readonly property real lo: root.range.wmMinOpacity
                        value: (root.wm.opacity - lo) / (1 - lo)
                        tipText: Math.round(root.wm.opacity * 100) + "%"
                        onMoved: (v) => session.dragFrameValue("wmOpacity", lo + v * (1 - lo))
                        onReleased: session.endFrameDrag()
                    }
                }
            }
        }
    }

    // The custom color picker. A drag is one undo step: it is recorded when the popover closes.
    Popover {
        id: custom
        objectName: "customColorPopover"
        parent: customSwatch
        property real hue: 0
        property real sat: 0.8
        property real val: 0.9
        property real alpha: 1
        function apply() { session.dragFrameBackground("plain", { color: Qt.hsva(hue / 360, sat, val, alpha).toString() }); }
        onAboutToShow: {
            const c = Qt.color(root.f.color || "#2d6cdf");
            if (c.hsvSaturation > 0) hue = c.hsvHue * 360;
            sat = c.hsvSaturation || 0.8;
            val = c.hsvValue || 0.9;
            alpha = c.a;
        }
        onClosed: session.endFrameDrag()
        contentItem: Column {
            spacing: 10
            ColorSlider {
                objectName: "frameHueSlider"
                tipText: Math.round(custom.hue) + "\u00b0"
                stops: ["#ff0000", "#ffff00", "#00ff00", "#00ffff", "#0000ff", "#ff00ff", "#ff0000"]
                value: custom.hue / 360
                onMoved: (v) => { custom.hue = v * 359.9; custom.apply(); }
            }
            ColorSlider {
                objectName: "frameSaturationSlider"
                stops: [Qt.hsva(custom.hue / 360, 0, custom.val, 1), Qt.hsva(custom.hue / 360, 1, custom.val, 1)]
                value: custom.sat
                onMoved: (v) => { custom.sat = v; custom.apply(); }
            }
            ColorSlider {
                objectName: "frameBrightnessSlider"
                stops: ["black", Qt.hsva(custom.hue / 360, custom.sat, 1, 1)]
                value: custom.val
                onMoved: (v) => { custom.val = v; custom.apply(); }
            }
            ColorSlider {
                objectName: "frameOpacitySlider"
                stops: [Qt.hsva(custom.hue / 360, custom.sat, custom.val, 0), Qt.hsva(custom.hue / 360, custom.sat, custom.val, 1)]
                value: custom.alpha
                onMoved: (v) => { custom.alpha = v; custom.apply(); }
            }
        }
    }
}
