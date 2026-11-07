import QtQuick

// Crop mode over the canvas: the part outside the Crop dimmed, the Crop's frame
// and its eight Handles (the same white dots with an accent rim as everywhere).
// All geometry comes from `session.cropChrome` (view pixels); it takes no input,
// the canvas MouseArea under it gets every click.
Item {
    enabled: false
    visible: session.cropping

    Repeater {
        model: session.cropChrome
        Rectangle {
            required property var modelData
            readonly property bool isDot: modelData.type === "dot"
            readonly property bool isDim: modelData.type === "dim"
            objectName: isDot ? "cropDot" + modelData.handle : isDim ? "cropDim" : "cropFrame"
            width: isDot ? (modelData.hot ? 14 : 10) : modelData.w
            height: isDot ? (modelData.hot ? 14 : 10) : modelData.h
            x: isDot ? modelData.x - width / 2 : modelData.x
            y: isDot ? modelData.y - height / 2 : modelData.y
            radius: isDot ? width / 2 : 0
            color: isDot ? "white" : isDim ? Qt.rgba(0, 0, 0, 0.55) : "transparent"
            border.width: isDot ? 2 : isDim ? 0 : 1
            border.color: isDot ? theme.accent : "white"
        }
    }
}
