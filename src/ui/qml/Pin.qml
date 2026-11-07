import QtQuick
import QtQuick.Window
import QtQuick.Dialogs

// A Pin: the capture in a frameless floating window. Context properties: `pins`
// (the Pins service), `pinId` (a number), and `win` (the window: startMove).
//   drag            moves the window (the compositor does it)
//   scroll          resizes it; Ctrl+scroll changes the opacity
//   middle-click, the hover x, Esc   close it
//   right-click     Copy, Save as…, Edit, Lock, Close all pins; Ctrl+C copies; L locks
//                   (a GIF Pin loops its own GIF file, with no Edit and Save as… writes a GIF)
//   Locked          a lock badge top-left; no clicks, scroll, hover x or keys
Item {
    id: root
    objectName: "pin"
    focus: true
    Component.onCompleted: forceActiveFocus()

    property real level: pins.opacity(pinId)
    Connections {
        target: pins
        function onOpacityChanged(id) { if (id === pinId) root.level = pins.opacity(id); }
    }
    property bool menuOpen: false
    // Locked: Hyprland skips the window when picking what the pointer is over, so
    // nothing here is ever reached; the QML also stops responding, for the instant
    // before the compositor has applied it.
    property bool locked: pins.isLocked(pinId)
    Connections {
        target: pins
        function onLockedChanged(id) { if (id === pinId) { root.locked = pins.isLocked(id); root.menuOpen = false; } }
    }

    readonly property bool gif: pins.isGif(pinId)

    // A GIF Pin: loops with no controls. cache: false, or every decoded frame stays in
    // memory (about 740 MB for a 10 s 1280x720 GIF); frames are scaled on the GPU.
    AnimatedImage {
        id: gifShot
        objectName: "gifShot"
        anchors.fill: parent
        visible: root.gif
        source: root.gif ? "file://" + pins.gifPath(pinId) : ""
        cache: false
        playing: root.gif
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignHCenter
        verticalAlignment: Image.AlignVCenter
        smooth: true
        opacity: root.level
    }

    Image {
        id: shot
        objectName: "shot"
        visible: !root.gif
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignHCenter
        verticalAlignment: Image.AlignVCenter
        smooth: true
        mipmap: false
        cache: false
        opacity: root.level
        // Resampled to the window's pixels once a resize settles (see PinImageProvider);
        // in between, the last picture is scaled on the GPU.
        sourceSize: Qt.size(settled.width * Screen.devicePixelRatio, settled.height * Screen.devicePixelRatio)
        source: root.gif ? "" : "image://pin/" + pinId
    }
    QtObject {
        id: settled
        property real width: root.width
        property real height: root.height
    }
    Timer {
        id: settle
        interval: 120
        onTriggered: { settled.width = root.width; settled.height = root.height }
    }
    onWidthChanged: settle.restart()
    onHeightChanged: settle.restart()

    // A white-at-10% outline keeps a dark capture's edge on a dark desktop.
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.1)
        opacity: root.level
    }

    HoverHandler { id: hover }

    MouseArea {
        id: area
        objectName: "pinArea"
        anchors.fill: parent
        enabled: !root.locked
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        onPressed: (m) => {
            if (m.button === Qt.LeftButton) {
                root.menuOpen = false;
                win.startMove();
            }
        }
        onClicked: (m) => {
            if (m.button === Qt.MiddleButton) {
                pins.close(pinId);
            } else if (m.button === Qt.RightButton) {
                menu.x = Math.max(0, Math.min(root.width - menu.width, m.x));
                menu.y = Math.max(0, Math.min(root.height - menu.height, m.y));
                root.menuOpen = true;
            }
        }
        onWheel: (w) => {
            if (w.modifiers & Qt.ControlModifier)
                pins.changeOpacity(pinId, w.angleDelta.y !== 0 ? w.angleDelta.y : w.angleDelta.x);
            else
                pins.resize(pinId, w.angleDelta.y);
            w.accepted = true;
        }
    }

    // Hover x, top-right. Hidden until the pointer is over the Pin.
    ThumbnailButton {
        objectName: "closeButton"
        x: root.width - width - 8
        y: 8
        icon: "x"
        tip: "Close"
        visible: hover.hovered && !root.locked
        onClicked: pins.close(pinId)
    }

    // Lock badge, top-left, the size of the x.
    Rectangle {
        objectName: "lockBadge"
        x: 8
        y: 8
        width: 28
        height: 28
        radius: 14
        color: "#E6E6E6"
        visible: root.locked
        Icon { anchors.centerIn: parent; name: "lock"; size: 15; color: "#1A1A1A" }
    }

    PinMenu {
        id: menu
        gif: root.gif
        visible: root.menuOpen
        onCopy: { root.menuOpen = false; pins.copy(pinId); }
        onSaveAs: { root.menuOpen = false; root.saveAs(); }
        onEdit: { root.menuOpen = false; pins.edit(pinId); }
        onLock: { root.menuOpen = false; pins.lock(pinId); }
        onCloseAll: { root.menuOpen = false; pins.closeAll(); }
    }

    // Save as…: the system file dialog, PNG / JPEG / WebP, in the last folder used.
    function saveAs() {
        saveAsLoader.active = true;
        saveAsLoader.item.start();
    }
    Loader {
        id: saveAsLoader
        active: false
        sourceComponent: FileDialog {
            fileMode: FileDialog.SaveFile
            nameFilters: root.gif ? ["GIF (*.gif)"] : ["PNG (*.png)", "JPEG (*.jpg *.jpeg)", "WebP (*.webp)"]
            function start() {
                currentFolder = "file://" + pins.saveAsFolder();
                selectedNameFilter.index = root.gif ? 0 : pins.saveAsFilterIndex();
                currentFile = "file://" + pins.saveAsSuggestion(pinId);
                open();
            }
            onAccepted: {
                let path = decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, ""));
                if (!(root.gif ? /\.gif$/i : /\.(png|jpe?g|webp)$/i).test(path))
                    path += "." + selectedNameFilter.extensions[0];
                pins.saveAs(pinId, path);
            }
        }
    }

    Shortcut { sequence: "Escape"; enabled: !root.locked; onActivated: { if (root.menuOpen) root.menuOpen = false; else pins.close(pinId); } }
    Shortcut { sequence: "Ctrl+C"; enabled: !root.locked; onActivated: pins.copy(pinId) }
    Shortcut { sequence: "L"; enabled: !root.locked; onActivated: pins.lock(pinId) }
}
