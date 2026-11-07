import QtQuick
import QtQuick.Effects

// Clips its children to a rounded rectangle (a plain Rectangle's clip is square).
Item {
    id: root
    default property alias content: holder.data
    property real radius: 10
    Item {
        id: holder
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect { maskEnabled: true; maskSource: m; maskThresholdMin: 0.5; maskSpreadAtMin: 1.0 }
    }
    Rectangle { id: m; anchors.fill: parent; radius: root.radius; visible: false; layer.enabled: true }
}
