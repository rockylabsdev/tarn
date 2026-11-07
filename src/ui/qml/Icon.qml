import QtQuick

// A Tabler icon (MIT, assets/icons/LICENSE), tinted by rewriting currentColor in
// the SVG text. Icons are compiled in: assets/icons/<name>.svg.
Image {
    id: root
    property string name
    property int size: Style.iconSize
    property color color: theme.text
    width: size
    height: size
    sourceSize.width: size * 2
    sourceSize.height: size * 2
    readonly property string svg: {
        return name ? icons.svg(name) : "";
    }
    source: svg === "" ? "" : "data:image/svg+xml;utf8," + encodeURIComponent(
        svg.replace(/stroke="currentColor"/g, `stroke="${Qt.rgba(color.r, color.g, color.b, 1)}"`)
           .replace(/fill="currentColor"/g, `fill="${Qt.rgba(color.r, color.g, color.b, 1)}"`))
}
