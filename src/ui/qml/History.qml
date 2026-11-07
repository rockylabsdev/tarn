import QtQuick
import QtQuick.Dialogs

// The History window (variant A of the history prototype, in the Palette's colors):
// 52 px top and bottom bars, the grid of captures under 15 px semibold day
// headings (tiles at least 200 px wide, 14 px gaps), a 2 px accent ring for the
// selection. Context properties: `historyModel` (HistoryModel: the rows, the
// selection, the summary) and `win` (the window: close). Actions in the bottom bar
// arrive with later tickets. State changes are instant: no Behavior anywhere.
Item {
    id: root
    objectName: "history"
    focus: true
    Component.onCompleted: forceActiveFocus()

    readonly property int gap: 14
    readonly property int minTile: 200
    readonly property int margin: 20
    readonly property real availW: grid.width - 2 * margin
    readonly property int columns: Math.max(1, Math.floor((availW + gap) / (minTile + gap)))
    readonly property real tileW: (availW - (columns - 1) * gap) / columns
    readonly property real imageH: Math.round(tileW * 0.625)

    Rectangle { anchors.fill: parent; color: theme.canvas }

    // Esc closes the menu, then clears a search, then closes the window. Ctrl+F or / starts a search.
    // The window is hidden and reused, not rebuilt: what was left open in it closes with it.
    Connections {
        target: root.Window.window
        function onVisibleChanged() {
            if (!root.Window.window.visible) {
                appList.visible = false;
                menu.visible = false;
            }
        }
    }
    Keys.onEscapePressed: { if (menu.visible) menu.visible = false; else win.escape(); }
    Keys.onPressed: event => {
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0;
        const plain = (event.modifiers & ~Qt.KeypadModifier) === Qt.NoModifier;
        if ((event.key === Qt.Key_F && ctrl) || (event.key === Qt.Key_Slash && !ctrl)) searchField.forceActiveFocus();
        else if (ctrl && event.key === Qt.Key_C) root.act("copy");
        else if (ctrl && event.key === Qt.Key_S) root.act("saveAs");
        else if (ctrl && event.key === Qt.Key_A) historyModel.selectAll();
        else if (plain && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) root.act("edit");
        else if (plain && event.key === Qt.Key_P) root.act("pin");
        else if (plain && event.key === Qt.Key_R) root.act("restore");
        else if (plain && event.key === Qt.Key_Delete) root.act("delete");
        else return;
        event.accepted = true;
    }

    // One door for every action (keys, hover buttons, menu, bottom bar): single-entry
    // actions do nothing on a selection of several. The window stays open.
    // A Copy or Pin that worked shows a check on that tile's button for a moment.
    function act(action) {
        menu.visible = false;
        const one = historyModel.singleSelected;
        if (action === "edit" && one) historyModel.edit();
        else if (action === "revert" && one) historyModel.revertToOriginal();
        else if ((action === "copy" || action === "pin") && one) {
            const id = historyModel.selectedId;
            if (action === "copy" ? historyModel.copy() : historyModel.pin()) confirm(id, action);
        }
        else if (action === "showInFiles" && one) historyModel.showInFiles();
        else if (action === "saveAs" && one) { saveAsLoader.active = true; saveAsLoader.item.start(); }
        else if (action === "restore" && historyModel.selectedCount > 0) historyModel.restore();
        else if (action === "delete" && historyModel.selectedCount > 0) historyModel.trashSelected();
        root.forceActiveFocus();
    }

    property string confirmedId
    property string confirmedAction
    function confirm(id, action) {
        confirmedId = id;
        confirmedAction = action;
        confirmTimer.restart();
    }
    Timer {
        id: confirmTimer
        interval: Style.historyCheckHoldMs
        onTriggered: root.confirmedAction = ""
    }

    // Save as…: the system file dialog in the last folder used, named by the File name
    // pattern, the filter preselected from the Image format (PNG, JPEG or WebP).
    Loader {
        id: saveAsLoader
        active: false
        sourceComponent: FileDialog {
            fileMode: FileDialog.SaveFile
            nameFilters: ["PNG (*.png)", "JPEG (*.jpg *.jpeg)", "WebP (*.webp)"]
            function start() {
                currentFolder = "file://" + historyModel.saveAsFolder();
                selectedNameFilter.index = historyModel.saveAsFilterIndex();
                currentFile = "file://" + historyModel.saveAsSuggestion();
                open();
            }
            onAccepted: {
                let path = decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, ""));
                if (!/\.(png|jpe?g|webp)$/i.test(path))
                    path += "." + selectedNameFilter.extensions[0];
                historyModel.saveAs(path);
            }
        }
    }
    Keys.onLeftPressed: historyModel.move("left", columns)
    Keys.onRightPressed: historyModel.move("right", columns)
    Keys.onUpPressed: historyModel.move("up", columns)
    Keys.onDownPressed: historyModel.move("down", columns)

    // A selected tile scrolls into view.
    function reveal(item) {
        const top = item.y, bottom = item.y + item.height + margin;
        if (top - margin < grid.contentY)
            grid.contentY = Math.max(0, top - margin);
        else if (bottom > grid.contentY + grid.height)
            grid.contentY = bottom - grid.height;
    }

    Rectangle {
        id: topBar
        z: 5
        objectName: "topBar"
        width: parent.width
        height: Style.barHeight
        color: theme.canvas
        MouseArea { anchors.fill: parent; onPressed: win.startMove() }
        Text {
            anchors { left: parent.left; leftMargin: root.margin; verticalCenter: parent.verticalCenter }
            text: "History"
            color: theme.text
            font { family: Style.font; pixelSize: 15; weight: Font.DemiBold }
        }
        Text {
            objectName: "countLabel"
            anchors { right: parent.right; rightMargin: root.margin; verticalCenter: parent.verticalCenter }
            text: historyModel.countText
            color: theme.muted
            font { family: Style.font; pixelSize: 13 }
        }
        Row {
            anchors { left: parent.left; leftMargin: 100; verticalCenter: parent.verticalCenter }
            spacing: 8
            Rectangle {
                id: searchBox
                objectName: "searchBox"
                width: 280
                height: Style.ctlC
                radius: Style.radiusSm
                color: theme.control
                TextInput {
                    id: searchField
                    objectName: "searchField"
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    verticalAlignment: TextInput.AlignVCenter
                    clip: true
                    color: theme.text
                    selectionColor: theme.accent
                    font { family: Style.font; pixelSize: 13 }
                    text: historyModel.search
                    onTextEdited: historyModel.search = text
                    Keys.onEscapePressed: { win.escape(); root.forceActiveFocus(); }
                    Keys.onDownPressed: root.forceActiveFocus()
                    Text {
                        objectName: "searchPlaceholder"
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: searchField.text === ""
                        text: historyModel.searchPlaceholder
                        elide: Text.ElideRight
                        color: theme.muted
                        font: searchField.font
                    }
                }
            }
            Repeater {
                model: [{label: "All", kind: ""}, {label: "Area", kind: "area"}, {label: "Window", kind: "window"}, {label: "Full screen", kind: "fullscreen"}]
                delegate: Rectangle {
                    required property var modelData
                    objectName: "kindFilter"
                    readonly property bool active: historyModel.kindFilter === modelData.kind
                    width: kindText.width + 20
                    height: Style.ctlC
                    radius: Style.radiusSm
                    color: active ? theme.accent : theme.control
                    Text {
                        id: kindText
                        anchors.centerIn: parent
                        text: parent.modelData.label
                        color: parent.active ? theme.canvas : theme.text
                        font { family: Style.font; pixelSize: 13 }
                    }
                    MouseArea { anchors.fill: parent; onPressed: { historyModel.kindFilter = parent.modelData.kind; root.forceActiveFocus(); } }
                }
            }
            Rectangle {
                id: appButton
                objectName: "appFilter"
                width: appText.width + 20
                height: Style.ctlC
                radius: Style.radiusSm
                color: historyModel.appFilter !== "" ? theme.accent : theme.control
                Text {
                    id: appText
                    anchors.centerIn: parent
                    text: historyModel.appFilter !== "" ? historyModel.appFilter : "All apps"
                    color: historyModel.appFilter !== "" ? theme.canvas : theme.text
                    font { family: Style.font; pixelSize: 13 }
                }
                MouseArea { anchors.fill: parent; onPressed: { appList.visible = !appList.visible; root.forceActiveFocus(); } }

                // The app list: most used first, with counts.
                Rectangle {
                    id: appList
                    objectName: "appList"
                    visible: false
                    x: 0
                    y: parent.height + 4
                    width: 220
                    height: appColumn.height + 16
                    radius: Style.radiusSm
                    color: theme.control
                    Column {
                        id: appColumn
                        x: 8; y: 8
                        width: parent.width - 16
                        Repeater {
                            model: [{name: "", count: historyModel.captureCount}].concat(historyModel.apps)
                            // A row lights up under the pointer, like the right-click menu's.
                            delegate: Rectangle {
                                id: appRow
                                required property var modelData
                                required property int index
                                objectName: "appRow-" + index
                                width: appColumn.width
                                height: 28
                                radius: Style.radiusSm
                                color: appArea.containsMouse ? theme.hover : "transparent"
                                Text {
                                    anchors { left: parent.left; leftMargin: Style.spacing; verticalCenter: parent.verticalCenter }
                                    text: appRow.modelData.name === "" ? "All apps" : appRow.modelData.name
                                    color: theme.text
                                    font { family: Style.font; pixelSize: 13 }
                                }
                                Text {
                                    anchors { right: parent.right; rightMargin: Style.spacing; verticalCenter: parent.verticalCenter }
                                    text: appRow.modelData.count
                                    color: theme.muted
                                    font { family: Style.font; pixelSize: 12 }
                                }
                                MouseArea {
                                    id: appArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onPressed: { historyModel.appFilter = appRow.modelData.name; appList.visible = false; }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Flickable {
        id: grid
        objectName: "grid"
        anchors { top: topBar.bottom; bottom: bottomBar.top; left: parent.left; right: parent.right }
        contentWidth: width
        contentHeight: flow.height + 2 * root.margin
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        visible: historyModel.shownCount > 0

        Flow {
            id: flow
            x: root.margin
            y: root.margin
            width: root.availW
            spacing: root.gap

            Repeater {
                id: repeater
                objectName: "rows"
                model: historyModel
                delegate: Item {
                    id: row
                    required property bool heading
                    required property string day
                    required property string entryId
                    required property string app
                    required property string windowTitle
                    required property string time
                    required property string kind
                    required property string path
                    required property bool selected
                    required property bool edited
                    required property int revision
                    objectName: heading ? "dayHeading" : "tile"
                    width: heading ? flow.width : root.tileW
                    height: heading ? 22 : root.imageH + 44
                    onSelectedChanged: if (selected) root.reveal(row)
                    Component.onCompleted: if (selected) root.reveal(row)

                    Text {
                        visible: row.heading
                        anchors { left: parent.left; bottom: parent.bottom }
                        text: row.day
                        color: theme.text
                        font { family: Style.font; pixelSize: 15; weight: Font.DemiBold }
                    }

                    Item {
                        visible: !row.heading
                        anchors.fill: parent
                        Rectangle {
                            id: picture
                            width: parent.width
                            height: root.imageH
                            radius: Style.radiusSm
                            color: theme.control
                            clip: true
                            Image {
                                anchors.fill: parent
                                // An edited entry shows its finished picture (rebuilt from the Project file).
                                source: row.heading ? "" : row.edited ? "image://historypicture/" + row.entryId + "/" + row.revision : "file://" + row.path
                                sourceSize.width: 480
                                fillMode: Image.PreserveAspectFit
                                asynchronous: !row.edited
                                cache: false
                            }
                        }
                        Rectangle { // under the pencil disc and the selection ring
                            id: tileScrim
                            objectName: "tileScrim"
                            anchors.fill: picture
                            radius: picture.radius
                            color: Qt.rgba(0, 0, 0, Style.scrimOpacity)
                            readonly property bool confirming: root.confirmedAction !== "" && root.confirmedId === row.entryId
                            opacity: hover.hovered || confirming ? 1 : 0
                            Behavior on opacity {
                                NumberAnimation { duration: Style.scrimMs; easing.type: Easing.OutQuad }
                            }
                        }
                        Rectangle { // the pencil disc of an Edited entry
                            objectName: "editedMark"
                            visible: row.edited
                            width: 24; height: 24; radius: 12
                            anchors { left: picture.left; bottom: picture.bottom; margins: 8 }
                            color: theme.accent
                            Icon { anchors.centerIn: parent; name: "pencil"; size: 14; color: theme.accentText }
                        }
                        Rectangle { // the selection ring
                            objectName: "ring"
                            anchors.fill: picture
                            radius: Style.radiusSm
                            color: "transparent"
                            border { width: 2; color: theme.accent }
                            visible: row.selected
                        }
                        Text {
                            id: appLabel
                            anchors { left: parent.left; top: picture.bottom; topMargin: 6 }
                            width: parent.width - timeLabel.width - 8
                            text: row.app !== "" ? row.app : row.kind === "fullscreen" ? "Full screen" : "Area"
                            elide: Text.ElideRight
                            color: theme.text
                            font { family: Style.font; pixelSize: 13; weight: Font.Medium }
                        }
                        Text {
                            id: timeLabel
                            anchors { right: parent.right; baseline: appLabel.baseline }
                            text: row.time
                            color: theme.muted
                            font { family: Style.font; pixelSize: 12 }
                        }
                        Text {
                            anchors { left: parent.left; right: parent.right; top: appLabel.bottom; topMargin: 2 }
                            text: row.windowTitle
                            elide: Text.ElideRight
                            color: theme.muted
                            font { family: Style.font; pixelSize: 12 }
                        }
                        HoverHandler { id: hover }
                        MouseArea {
                            objectName: "tileArea"
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onPressed: mouse => {
                                menu.visible = false;
                                if (mouse.button === Qt.RightButton) {
                                    if (!row.selected) historyModel.select(row.entryId);
                                    const p = mapToItem(root, mouse.x, mouse.y);
                                    menu.x = Math.max(0, Math.min(root.width - menu.width, p.x));
                                    menu.y = Math.max(0, Math.min(root.height - menu.height, p.y));
                                    menu.visible = true;
                                } else if (mouse.modifiers & Qt.ControlModifier) {
                                    historyModel.toggle(row.entryId);
                                } else if (mouse.modifiers & Qt.ShiftModifier) {
                                    historyModel.extendTo(row.entryId);
                                } else {
                                    historyModel.select(row.entryId);
                                }
                                root.forceActiveFocus();
                            }
                            onDoubleClicked: mouse => {
                                if (mouse.button === Qt.LeftButton && mouse.modifiers === Qt.NoModifier) {
                                    historyModel.select(row.entryId);
                                    root.act("edit");
                                }
                            }
                        }
                        // Hovering a tile: the Thumbnail's light round buttons over its scrim, so
                        // they read on any picture and stand apart.
                        Row {
                            objectName: "hoverButtons"
                            visible: tileScrim.opacity > 0 || tileScrim.confirming
                            opacity: tileScrim.opacity
                            anchors { top: picture.top; right: picture.right; margins: 8 }
                            spacing: 8
                            ThumbnailButton {
                                objectName: "hoverCopy"
                                icon: "copy"
                                tip: "Copy"
                                done: tileScrim.confirming && root.confirmedAction === "copy"
                                onClicked: { historyModel.select(row.entryId); root.act("copy"); }
                            }
                            ThumbnailButton {
                                objectName: "hoverPin"
                                icon: "pin"
                                tip: "Pin"
                                done: tileScrim.confirming && root.confirmedAction === "pin"
                                onClicked: { historyModel.select(row.entryId); root.act("pin"); }
                            }
                            ThumbnailButton {
                                objectName: "hoverEdit"
                                icon: "pencil"
                                tip: "Edit"
                                onClicked: { historyModel.select(row.entryId); root.act("edit"); }
                            }
                        }
                    }
                }
            }
        }
    }

    Column {
        objectName: "emptyState"
        anchors { centerIn: parent }
        spacing: 6
        visible: historyModel.captureCount === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "No captures yet"
            color: theme.text
            font { family: Style.font; pixelSize: 17; weight: Font.DemiBold }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Take a screenshot with Print and it will show up here."
            color: theme.muted
            font { family: Style.font; pixelSize: 13 }
        }
    }

    Column {
        objectName: "nothingMatches"
        anchors { centerIn: parent }
        spacing: 10
        visible: historyModel.nothingMatches
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: historyModel.nothingMatchesText
            color: theme.text
            font { family: Style.font; pixelSize: 17; weight: Font.DemiBold }
        }
        Rectangle {
            objectName: "showEverything"
            anchors.horizontalCenter: parent.horizontalCenter
            width: showText.width + 24
            height: Style.ctl
            radius: Style.radiusSm
            color: theme.control
            Text { id: showText; anchors.centerIn: parent; text: "Show everything"; color: theme.text; font { family: Style.font; pixelSize: 13 } }
            MouseArea { anchors.fill: parent; onPressed: { historyModel.showEverything(); root.forceActiveFocus(); } }
        }
    }

    Rectangle {
        id: bottomBar
        objectName: "bottomBar"
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: Style.barHeight
        color: theme.canvas
        MouseArea { anchors.fill: parent; onPressed: win.startMove() }
        Text {
            objectName: "summary"
            anchors { left: parent.left; leftMargin: root.margin; right: actions.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
            text: historyModel.summary
            elide: Text.ElideRight
            color: historyModel.selectedCount === 0 ? theme.muted : theme.text
            font { family: Style.font; pixelSize: 13 }
        }
        Row {
            id: actions
            objectName: "actions"
            anchors { right: parent.right; rightMargin: root.margin; verticalCenter: parent.verticalCenter }
            spacing: 8
            readonly property bool one: historyModel.singleSelected
            readonly property bool any: historyModel.selectedCount > 0
            TextBtn { objectName: "actEdit"; text: "Edit"; tip: "Edit (Enter)"; primary: true; enabled: actions.one; opacity: enabled ? 1 : 0.4; onClicked: root.act("edit") }
            TextBtn { objectName: "actCopy"; text: "Copy"; tip: "Copy (Ctrl+C)"; enabled: actions.one; opacity: enabled ? 1 : 0.4; onClicked: root.act("copy") }
            TextBtn { objectName: "actPin"; text: "Pin"; tip: "Pin (P)"; enabled: actions.one; opacity: enabled ? 1 : 0.4; onClicked: root.act("pin") }
            TextBtn { objectName: "actSaveAs"; text: "Save as…"; tip: "Save as… (Ctrl+S)"; enabled: actions.one; opacity: enabled ? 1 : 0.4; onClicked: root.act("saveAs") }
            TextBtn { objectName: "actRestore"; text: "Restore"; tip: "Restore (R)"; enabled: actions.any; opacity: enabled ? 1 : 0.4; onClicked: root.act("restore") }
            TextBtn { objectName: "actDelete"; text: "Delete"; tip: "Delete (Delete)"; enabled: actions.any; opacity: enabled ? 1 : 0.4; onClicked: root.act("delete") }
        }
    }

    // Right-click menu, above everything; a click anywhere else closes it.
    MouseArea {
        anchors.fill: parent
        visible: menu.visible
        acceptedButtons: Qt.AllButtons
        onPressed: menu.visible = false
    }
    HistoryMenu {
        id: menu
        visible: false
        single: historyModel.singleSelected
        edited: historyModel.selectedEdited
        onChosen: action => root.act(action)
    }
}
