import QtQuick
import QtQuick.Dialogs

// The Thumbnail stack: bottom-left, newest in the corner, stacking upward. The
// surface spans from the screen's left edge so a card can slide off it, and is
// tall enough for the stack's maximum; the window's input region (inputRect)
// covers only the live cards.
Item {
    id: root
    width: Style.thumbEdge + Style.thumbW
    height: Style.thumbEdge + Style.thumbSlots * Style.thumbH + (Style.thumbSlots - 1) * Style.thumbGap

    // How many cards are on screen, including those still leaving.
    readonly property int cardCount: cards.count
    // The Thumbnails that have not started leaving, oldest first.
    property var liveIds: []
    // The part of the surface that takes the pointer: the live cards and their hover zones.
    readonly property rect inputRect: {
        const n = liveIds.length;
        if (n === 0)
            return Qt.rect(0, 0, 1, 1);
        const h = n * Style.thumbH + (n - 1) * Style.thumbGap;
        return Qt.rect(Style.thumbEdge, height - Style.thumbEdge - h - Style.thumbGap / 2, Style.thumbW, h + Style.thumbGap);
    }

    // y of the card at this rank (0 is the corner).
    function slotY(rank) {
        return height - Style.thumbEdge - Style.thumbH - rank * (Style.thumbH + Style.thumbGap);
    }

    ListModel { id: cards }
    property var items: ({})
    function register(card) { items[card.captureId] = card; }
    function unregister(card) { delete items[card.captureId]; }
    function refreshLive() {
        const live = [];
        for (let i = 0; i < cards.count; ++i) {
            const id = cards.get(i).captureId, card = items[id];
            if (!card || !card.leaving)
                live.push(id);
        }
        liveIds = live;
    }
    function remove(id) {
        for (let i = 0; i < cards.count; ++i)
            if (cards.get(i).captureId === id) {
                cards.remove(i);
                break;
            }
        refreshLive();
    }
    function append(id) {
        arrivals[id] = true;
        cards.append({ captureId: id });
        refreshLive();
    }
    // Cards still to make their entrance: new captures, not ones back from the Editor.
    property var arrivals: ({})
    function takeArrival(id) {
        const was = arrivals[id] === true;
        delete arrivals[id];
        return was;
    }

    // Save as… (a card's right-click menu): the system file dialog, PNG / JPEG / WebP,
    // starting in the last folder used. The stack does the saving and the closing.
    signal saveAsRequested(int id)
    onSaveAsRequested: (id) => {
        saveAsLoader.active = true;
        saveAsLoader.item.start(id);
    }
    Loader {
        id: saveAsLoader
        active: false
        sourceComponent: FileDialog {
            property int captureId: -1
            fileMode: FileDialog.SaveFile
            nameFilters: ["PNG (*.png)", "JPEG (*.jpg *.jpeg)", "WebP (*.webp)"]
            function start(id) {
                captureId = id;
                currentFolder = "file://" + thumbnails.saveAsFolder();
                selectedNameFilter.index = thumbnails.saveAsFilterIndex();
                currentFile = "file://" + thumbnails.saveAsSuggestion(id);
                open();
            }
            onAccepted: {
                let path = decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, ""));
                if (!/\.(png|jpe?g|webp)$/i.test(path))
                    path += "." + selectedNameFilter.extensions[0];
                thumbnails.saveAs(captureId, path);
            }
        }
    }

    // After a Thumbnail is shown again: add its card back in the stack's order.
    function reinsert() {
        const wanted = thumbnails.ids();
        for (let i = 0; i < wanted.length; ++i) {
            let at = -1;
            for (let j = 0; j < cards.count; ++j)
                if (cards.get(j).captureId === wanted[i])
                    at = j;
            if (at < 0)
                cards.insert(Math.min(i, cards.count), { captureId: wanted[i] });
        }
        refreshLive();
    }

    Component.onCompleted: thumbnails.ids().forEach(append)
    Connections {
        target: thumbnails
        function onAdded(id) { root.append(id); }
        function onClosed(id) { if (root.items[id]) root.items[id].closed(); }
        // Hidden while editing: gone at once, no exit animation; back in its place after.
        function onHidden(id) { root.remove(id); }
        function onRevealed(id) { root.reinsert(); }
        // Capture hide over: every card springs back in from the left edge together,
        // at its place now (one added meanwhile included). The surface left at once.
        function onCaptureHiddenChanged() {
            if (thumbnails.captureHidden)
                return;
            for (let i = 0; i < cards.count; ++i) {
                const card = root.items[cards.get(i).captureId];
                if (card)
                    card.springBack();
            }
        }
    }

    Repeater {
        model: cards
        ThumbnailCard { board: root }
    }
}
