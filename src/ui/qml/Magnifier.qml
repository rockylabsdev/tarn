import QtQuick
import QtQuick.Shapes
import QtQuick.Effects

// The Magnifier (variant A of "The Magnifier's look"): a 120 px tile showing 15
// device pixels at 8x with no smoothing, the Overlay's dim and outline redrawn
// at the same zoom, a 16% black grid and the pixel under the pointer boxed; a
// 10 px radius and a 2 px accent ring. An x, y pill sits 8 px under it. Where
// the tile and pill go is the model's (magnifierTile / magnifierPill).
Item {
    id: root
    required property var model
    required property Item source // the frozen image item

    readonly property int cells: 15
    readonly property int zoom: 8
    readonly property int size: cells * zoom
    readonly property int half: Math.floor(cells / 2)
    readonly property real devScale: model.pixelScale
    // The pixel under the pointer, in the frozen image's own (device) pixels.
    readonly property int dx: Math.floor(model.pointer.x * devScale)
    readonly property int dy: Math.floor(model.pointer.y * devScale)
    readonly property int ox: dx - half
    readonly property int oy: dy - half
    readonly property rect sel: model.selection
    readonly property bool has: model.hasSelection

    // Device pixel (sx, sy) -> tile coordinates.
    function lx(sx) { return (sx - ox) * zoom }
    function ly(sy) { return (sy - oy) * zoom }

    Item {
        id: tile
        objectName: "magnifierTile"
        x: root.model.magnifierTile.x
        y: root.model.magnifierTile.y
        width: root.size
        height: root.size

        // Clips the contents to a 10 px rounded square.
        Item {
            anchors.fill: parent
            layer.enabled: true
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: mask
                maskThresholdMin: 0.5
                maskSpreadAtMin: 1.0
            }

            Rectangle { anchors.fill: parent; color: "#0B0B0B" } // off the screen's edge

            ShaderEffectSource {
                anchors.fill: parent
                sourceItem: root.source
                sourceRect: Qt.rect(root.ox / root.devScale, root.oy / root.devScale, root.cells / root.devScale, root.cells / root.devScale)
                textureSize: Qt.size(root.cells, root.cells)
                smooth: false
                live: true
            }

            // The Overlay's dim, with the Selection cut out.
            Shape {
                anchors.fill: parent
                ShapePath {
                    fillColor: Qt.rgba(0, 0, 0, 0.5)
                    strokeWidth: -1
                    fillRule: ShapePath.OddEvenFill
                    PathRectangle { x: 0; y: 0; width: root.size; height: root.size }
                    PathRectangle {
                        x: root.has ? root.lx(root.sel.x * root.devScale) : -10
                        y: root.has ? root.ly(root.sel.y * root.devScale) : -10
                        width: root.has ? root.sel.width * root.devScale * root.zoom : 0
                        height: root.has ? root.sel.height * root.devScale * root.zoom : 0
                    }
                }
            }
            // The outline, just outside the Selection so no picked pixel is covered.
            Rectangle {
                visible: root.has
                x: root.has ? root.lx(root.sel.x * root.devScale) - 2 : 0
                y: root.has ? root.ly(root.sel.y * root.devScale) - 2 : 0
                width: root.has ? root.sel.width * root.devScale * root.zoom + 4 : 0
                height: root.has ? root.sel.height * root.devScale * root.zoom + 4 : 0
                color: "transparent"
                border.width: 2
                border.color: theme.accent
            }

            // Pixel grid, 16% black.
            Repeater {
                model: root.cells - 1
                Rectangle { required property int index; x: (index + 1) * root.zoom; width: 1; height: root.size; color: Qt.rgba(0, 0, 0, 0.16) }
            }
            Repeater {
                model: root.cells - 1
                Rectangle { required property int index; y: (index + 1) * root.zoom; height: 1; width: root.size; color: Qt.rgba(0, 0, 0, 0.16) }
            }

            // The pixel under the pointer, boxed white-on-black so it reads on any color.
            Rectangle {
                objectName: "magnifierCenter"
                x: root.half * root.zoom - 1; y: x
                width: root.zoom + 2; height: width
                color: "transparent"; border.width: 1; border.color: "white"
                Rectangle { anchors.fill: parent; anchors.margins: 1; color: "transparent"; border.width: 1; border.color: "black" }
            }

            // A hairline so the tile's edge reads on a black screen too.
            Rectangle { anchors.fill: parent; radius: 10; color: "transparent"; border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.14) }
        }
        Rectangle { id: mask; anchors.fill: parent; radius: 10; visible: false; layer.enabled: true }

        // The accent ring: 2 px, 1 px gap.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: 13
            color: "transparent"
            border.width: 2
            border.color: theme.accent
        }
    }

    Rectangle {
        objectName: "magnifierPill"
        x: root.model.magnifierPill.x + (root.size - width) / 2
        y: root.model.magnifierPill.y
        height: 24
        width: label.implicitWidth + 20 + (root.model.colorMode ? 16 : 0)
        radius: height / 2
        color: Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, 0.94)
        border.width: 1
        border.color: theme.border
        // Color Mode: a swatch of the color under the pointer before the hex.
        Rectangle {
            objectName: "magnifierSwatch"
            visible: root.model.colorMode
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 12; height: 12; radius: 3
            color: root.model.pointerColor
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.5)
        }
        Text {
            id: label
            objectName: "magnifierLabel"
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: root.model.colorMode ? 8 : 0
            text: root.model.magnifierLabel
            color: theme.text
            font.family: Style.fontMono
            font.pixelSize: 12
            font.weight: Font.Medium
        }
    }
}
