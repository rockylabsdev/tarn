import QtQuick
import QtQuick.Controls.Basic

// The emoji picker of the Text tool: a button that opens a popover with a
// search box and a grid of emoji. Typing narrows the grid by name and keyword
// ("heart", "thumbs"); a click inserts the emoji at the Text's cursor.
IconBtn {
    id: root
    objectName: "emojiButton"
    pill: true
    flat: false
    box: Style.ctlC
    boxW: 40
    icon: "mood-smile"
    tip: "Emoji (Ctrl+.)"
    property alias popup: pop
    property string hoveredName
    signal picked(string emoji)

    function open() { pop.open(); }
    function close() { pop.close(); }
    onClicked: pop.toggle()

    EmojiIndex { id: emojis }

    Popover {
        id: pop
        objectName: "emojiPopover"
        padding: 10
        onAboutToShow: { search.text = ""; }
        onOpened: search.forceActiveFocus()

        contentItem: Column {
            spacing: 8
            Rectangle {
                width: 8 * 36
                height: Style.ctlC
                radius: Style.radiusSm
                color: theme.control
                TextInput {
                    id: search
                    objectName: "emojiSearch"
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    verticalAlignment: TextInput.AlignVCenter
                    color: theme.text
                    font.family: Style.font
                    font.pixelSize: 13
                    selectByMouse: true
                    clip: true
                    // Enter picks the first match.
                    Keys.onReturnPressed: pickFirst()
                    Keys.onEnterPressed: pickFirst()
                    function pickFirst() {
                        if (results.length > 0) {
                            root.picked(results[0].emoji);
                            pop.close();
                        }
                    }
                    onTextChanged: root.hoveredName = ""
                    readonly property var results: emojis.search(text)
                }
                Text {
                    visible: search.text === "" && !search.activeFocus
                    anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                    text: "Search emoji"
                    color: theme.muted
                    font.family: Style.font
                    font.pixelSize: 13
                }
            }
            GridView {
                id: grid
                objectName: "emojiGrid"
                width: 8 * 36
                height: 6 * 36
                cellWidth: 36
                cellHeight: 36
                clip: true
                model: search.results
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    objectName: "emojiCell-" + index
                    width: 36
                    height: 36
                    radius: Style.radiusSm
                    color: cell.containsMouse ? theme.hover : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: modelData.emoji
                        font.pixelSize: 22
                    }
                    MouseArea {
                        id: cell
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: { root.picked(modelData.emoji); pop.close(); }
                    }
                    property bool hot: cell.containsMouse
                    onHotChanged: if (hot) root.hoveredName = modelData.name
                }
            }
            Text {
                id: status
                objectName: "emojiStatus"
                width: 8 * 36
                elide: Text.ElideRight
                text: grid.count === 0 ? "No emoji found" : root.hoveredName
                color: theme.text2
                font.family: Style.font
                font.pixelSize: 12
            }
        }
    }
}
