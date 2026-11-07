import QtQuick

// A divider: 1 x 20 vertical inside a capsule, or 1 px across the parent.
Rectangle {
    property bool vertical: true
    width: vertical ? 1 : parent.width
    height: vertical ? Style.sepLength : 1
    color: theme.divider
    anchors.verticalCenter: vertical && parent ? parent.verticalCenter : undefined
}
