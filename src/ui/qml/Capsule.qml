import QtQuick

// The toolbar's one shape: a 32 px capsule holding buttons, 2 px in from its edge.
Rectangle {
    default property alias content: row.data
    height: Style.ctlC
    width: row.implicitWidth + 2 * Style.capsuleInset
    radius: height / 2
    color: theme.control
    Row { id: row; anchors.centerIn: parent; spacing: 0 }
}
