import QtQuick

// The text field a Text is typed in: a TextEdit laid over the canvas at the
// Text's place, scaled with the zoom so its lines fall where the Annotation
// paints them. Its own letters are invisible: the document canvas paints the
// Text (in its look), so what is typed is what is exported. Enter adds a line
// (the Editor's Enter shortcut yields to a text field); Esc is the Editor's, and
// finishes the Text.
TextEdit {
    id: edit
    objectName: "textEditor"
    visible: session.textEditing
    // Through docToView (not the arithmetic) so a Crop or Frame that moves the shot is followed.
    // The counter re-evaluates it when a Crop, turn or Frame change moves the shot.
    property int geometryTick: 0
    Connections {
        target: session
        function onCropChanged() { edit.geometryTick++; }
        function onDocumentChanged() { edit.geometryTick++; }
        function onViewChanged() { edit.geometryTick++; }
    }
    readonly property point place: { geometryTick; session.origin; session.zoom; return session.docToView(session.textOrigin.x, session.textOrigin.y); }
    x: place.x
    y: place.y
    scale: session.zoom
    transformOrigin: Item.TopLeft
    font.family: Style.font
    font.weight: Font.Medium
    font.pixelSize: session.textSize
    color: "transparent"
    selectedTextColor: "transparent"
    selectionColor: Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.4)
    selectByMouse: true
    persistentSelection: false
    wrapMode: TextEdit.NoWrap
    cursorDelegate: Rectangle {
        width: 2 / session.zoom
        color: theme.accent
    }

    // A dashed-looking hairline round the field, so an empty one can be seen.
    Rectangle {
        objectName: "textBox"
        x: -6
        y: -3
        width: edit.contentWidth + 12
        height: edit.contentHeight + 6
        z: -1
        color: "transparent"
        border.width: 1 / session.zoom
        border.color: theme.accent
        opacity: 0.7
    }

    onTextChanged: if (session.textEditing) session.text = text

    Connections {
        target: session
        function onTextEditingChanged() {
            if (!session.textEditing)
                return;
            edit.text = session.text;
            edit.cursorPosition = edit.length;
            edit.forceActiveFocus();
        }
    }

    // Puts `s` (an emoji) at the cursor.
    function insertText(s) {
        edit.insert(edit.cursorPosition, s);
        edit.forceActiveFocus();
    }
}
