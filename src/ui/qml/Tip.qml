import QtQuick
import QtQuick.Controls.Basic

ToolTip {
    id: tt
    delay: 500
    timeout: -1
    contentItem: Text { text: tt.text; color: theme.text; font.family: Style.font; font.pixelSize: 12 }
    background: Rectangle { color: theme.raised; radius: 8; border.color: theme.border }
}
