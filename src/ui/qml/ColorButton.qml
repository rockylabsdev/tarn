import QtQuick
import QtQuick.Controls.Basic

// The color select of the top bar: a swatch of the one shared color; a click
// opens the color popover (8 fixed swatches, the Palette's swatches, Saved colors, a custom
// picker with opacity, and the eyedropper). The popover opens at once.
Rectangle {
    id: root
    objectName: "colorButton"
    property string tip: "Color (K)"
    property alias popup: pop
    height: Style.ctlC
    width: Style.ctlC + 14
    radius: height / 2
    color: ma.pressed ? theme.pressed : ma.containsMouse ? theme.hover : theme.control

    function open() { pop.open(); }
    function close() { pop.close(); }

    Rectangle {
        objectName: "colorDot"
        anchors.centerIn: parent
        width: 18
        height: 18
        radius: 9
        color: session.color
        border.color: Qt.rgba(1, 1, 1, 0.35)
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; onClicked: pop.toggle() }
    Tip { visible: ma.containsMouse && !pop.opened; text: root.tip }

    // One round swatch. `value` is the color it sets.
    component Swatch: Rectangle {
        id: sw
        property color value
        signal picked
        width: 24
        height: 24
        radius: 12
        color: value
        border.width: Qt.colorEqual(session.color, value) ? 2 : 1
        border.color: Qt.colorEqual(session.color, value) ? theme.accent : Qt.rgba(1, 1, 1, 0.25)
        MouseArea { anchors.fill: parent; onClicked: sw.picked() }
    }

    Popover {
        id: pop
        objectName: "colorPopover"

        // The custom picker keeps its own hue, in degrees, so gray and black do not lose
        // it, and a change it makes itself is not read back (the rounded color's hue
        // would nudge the knob off the pointer).
        property real hue: 0
        property bool applying: false
        function syncFromColor() {
            const c = session.color;
            if (!applying && c.hsvHue >= 0 && c.hsvSaturation > 0)
                hue = c.hsvHue * 360; // QML's hsvHue is 0..1
        }
        function apply(h, s, v, a) {
            applying = true;
            session.dragColor(Qt.hsva(h, s, v, a));
            applying = false;
        }
        onAboutToShow: syncFromColor()
        onClosed: session.endColorDrag()
        Connections { target: session; function onColorChanged() { if (pop.opened) pop.syncFromColor(); } }

        contentItem: Column {
            spacing: 10
            Text { text: "Colors"; color: theme.text2; font.family: Style.font; font.pixelSize: 11 }
            Row {
                spacing: 6
                Repeater {
                    model: session.swatches
                    Swatch {
                        required property int index
                        required property var modelData
                        objectName: "swatch-" + index
                        value: modelData
                        onPicked: session.color = value
                    }
                }
            }
            // The Palette file's accent and text tones, live. Left out (label and row, so no gap)
            // while no Palette file is in use: the built-in colors would mean nothing there.
            Text { objectName: "popoverPaletteLabel"; visible: theme.fromFile; text: "Palette"; color: theme.text2; font.family: Style.font; font.pixelSize: 11 }
            Row {
                objectName: "popoverPaletteSwatches"
                visible: theme.fromFile
                spacing: 6
                Repeater {
                    model: [theme.accent, theme.text, theme.text2, theme.muted, theme.canvas, theme.border]
                    Swatch {
                        required property int index
                        required property var modelData
                        objectName: "palette-" + index
                        value: modelData
                        onPicked: session.color = value
                    }
                }
            }
            Text { objectName: "savedLabel"; text: "Saved"; color: theme.text2; font.family: Style.font; font.pixelSize: 11 }
            SavedRow {
                width: 8 * 30
                current: session.color
                onPicked: (v) => session.color = v
                onPlusClicked: session.saveCurrentColor()
            }
            Text { text: "Custom"; color: theme.text2; font.family: Style.font; font.pixelSize: 11 }
            ColorSlider {
                objectName: "hueSlider"
                tipText: Math.round(pop.hue) + "\u00b0"
                stops: ["#ff0000", "#ffff00", "#00ff00", "#00ffff", "#0000ff", "#ff00ff", "#ff0000"]
                value: pop.hue / 360
                onMoved: (v) => { pop.hue = v * 359.9; pop.apply(pop.hue / 360, session.color.hsvSaturation, session.color.hsvValue, session.color.a); } // only the hue: a gray stays gray until saturation brings the hue in
                onReleased: session.endColorDrag()
            }
            ColorSlider {
                objectName: "saturationSlider"
                stops: [Qt.hsva(pop.hue / 360, 0, session.color.hsvValue, 1), Qt.hsva(pop.hue / 360, 1, session.color.hsvValue, 1)]
                value: session.color.hsvSaturation
                onMoved: (v) => pop.apply(pop.hue / 360, v, session.color.hsvValue, session.color.a)
                onReleased: session.endColorDrag()
            }
            ColorSlider {
                objectName: "brightnessSlider"
                stops: ["black", Qt.hsva(pop.hue / 360, session.color.hsvSaturation, 1, 1)]
                value: session.color.hsvValue
                onMoved: (v) => pop.apply(pop.hue / 360, session.color.hsvSaturation, v, session.color.a)
                onReleased: session.endColorDrag()
            }
            Row {
                spacing: 8
                ColorSlider {
                    objectName: "opacitySlider"
                    width: 170
                    stops: [Qt.rgba(session.color.r, session.color.g, session.color.b, 0), Qt.rgba(session.color.r, session.color.g, session.color.b, 1)]
                    value: session.color.a
                    onMoved: (v) => session.dragColor(Qt.rgba(session.color.r, session.color.g, session.color.b, v))
                    onReleased: session.endColorDrag()
                }
                Text {
                    objectName: "opacityLabel"
                    anchors.verticalCenter: parent.verticalCenter
                    text: Math.round(session.color.a * 100) + "%"
                    color: theme.text
                    font.family: Style.font
                    font.pixelSize: 12
                }
            }
            Row {
                spacing: 8
                IconBtn {
                    objectName: "eyedropperButton"
                    pill: true
                    flat: false
                    box: Style.ctlC
                    icon: "color-picker"
                    tip: "Eyedropper (E): pick a color from the shot"
                    onClicked: { pop.close(); session.beginEyedropper(); }
                }
                Text {
                    objectName: "hexLabel"
                    anchors.verticalCenter: parent.verticalCenter
                    text: session.color.toString().toUpperCase()
                    color: theme.text2
                    font.family: Style.font
                    font.pixelSize: 12
                }
            }
        }
    }
}
