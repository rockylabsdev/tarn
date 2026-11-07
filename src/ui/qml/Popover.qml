import QtQuick
import QtQuick.Controls.Basic

// One of the Editor's popovers (the color popover, the emoji picker, the selects, the
// Frame panel's custom color), opened by the control it is declared in (its parent),
// just below it. A press on that control is not 'outside', so the control's click can
// `toggle()`. Esc is the Editor's: while open, a Popover is listed in `Popovers`, and
// Esc closes the newest one in its window before anything else.
Popup {
    id: pop
    closePolicy: Popup.CloseOnPressOutsideParent
    y: parent ? parent.height + Style.popoverGap : 0
    padding: Style.popoverPadding
    background: Rectangle { color: theme.raised; radius: Style.radiusPopover; border.color: theme.border }

    function toggle() { opened ? close() : open(); }

    onAboutToShow: Popovers.add(pop)
    onAboutToHide: Popovers.remove(pop)
    Component.onDestruction: Popovers.remove(pop)
}
