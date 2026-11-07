pragma Singleton
import QtQuick

// The open Popovers, oldest first. Each Popover adds itself as it opens and removes
// itself as it closes, so the Editor's Esc finds the open one without knowing which
// controls have one.
QtObject {
    property var shown: []

    function add(popover) {
        remove(popover);
        shown = shown.concat([popover]);
    }
    function remove(popover) {
        shown = shown.filter((p) => p !== popover);
    }
    // Closes the newest open Popover declared under `root` (an Editor window's root
    // item); false when there is none, so Esc can go on to its next step.
    function closeNewestIn(root) {
        for (let i = shown.length - 1; i >= 0; --i) {
            for (let it = shown[i].parent; it; it = it.parent) {
                if (it === root) {
                    shown[i].close();
                    return true;
                }
            }
        }
        return false;
    }
}
