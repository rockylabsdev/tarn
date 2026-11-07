import QtQuick

// A selected Annotation's Handles (spec "Annotation Handles"): the Overlay's
// white 14 px dots with a 2 px accent rim, a 3 px accent outline at 50%, drawn at screen
// size above every Annotation; a dot under the pointer grows to 18 px; a hovered
// unselected Annotation shows its selection at 40%. All geometry comes from
// `session.chrome` (view pixels), so what is drawn is what the core decided.
// It sits above the canvas picture and takes no input: the canvas MouseArea
// under it still gets every click.
Item {
    enabled: false

    Repeater {
        model: session.chrome
        Rectangle {
            required property var modelData
            readonly property bool isDot: modelData.type === "dot"
            readonly property bool isMarquee: modelData.type === "marquee"
            objectName: isDot ? "chromeDot" + modelData.handle : isMarquee ? "chromeMarquee" : "chromeOutline"
            width: isDot ? modelData.size : modelData.w
            height: isDot ? modelData.size : modelData.h
            x: isDot ? modelData.x - width / 2 : modelData.x
            y: isDot ? modelData.y - height / 2 : modelData.y
            radius: isDot ? width / 2 : 0
            color: isDot ? "white" : isMarquee ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.12) : "transparent"
            border.width: isDot ? 2 : 3
            border.color: isDot ? theme.accent : Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.5)
            opacity: modelData.opacity
        }
    }
}
