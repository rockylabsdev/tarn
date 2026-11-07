pragma Singleton
import QtQuick

// Tarn's style values: radii, sizes and spacing. They live here, so Tarn
// works with no shell running. Controls change state instantly,
// with no crossfade: there is deliberately no duration for hover or press here.
QtObject {
    readonly property string font: "Geist"
    readonly property string fontMono: "Geist Mono"

    readonly property int radius: 10
    readonly property int radiusSm: 8
    readonly property int radiusMenu: 14
    readonly property int radiusPopover: 12      // the Editor's popovers (Popover.qml)
    readonly property int popoverPadding: 12
    readonly property int popoverGap: 6          // between a popover and the control that opens it

    readonly property int ctl: 36        // a standalone control
    readonly property int ctlC: 32       // a control inside a capsule, and the capsule's height
    readonly property int capsuleInset: 2
    readonly property int iconSize: 16   // Tabler icons in pills
    readonly property int iconSizeBox: 18

    readonly property int spacing: 8
    readonly property int sepLength: 20
    readonly property int modeBarMargin: 24      // the Mode bar's distance from the screen edge
    readonly property int modeBarGap: 6          // between the Mode bar's capsules
    readonly property int keyInsetTop: 5         // a key label's distance from a button's top
    readonly property int pillHeight: 28         // the "N taken" pill
    readonly property int fontKey: 9             // a key label on a button
    readonly property int fontSmall: 12
    readonly property int fontCountdown: 20      // the Countdown's seconds
    readonly property int countdownGap: 10       // between the clock and the seconds
    readonly property int countdownWidth: 104    // the Countdown pill (the digits are fixed-width, so it never resizes)
    // The Countdown pill's top comes from the daemon (Countdown::top: the space a bar reserves at
    // the top of the monitor, plus a gap), not from a value here.
    readonly property int countdownHeight: 44
    readonly property int countdownDrop: 8       // px above its place that the pill drops in from
    readonly property color countdownFill: recordRed // fixed, whatever the wallpaper
    readonly property color countdownInk: "#FFFFFF"  // the clock and the seconds

    // The Overlay's kept Selection: the dim outside it and the outline just outside its edge. The
    // Countdown draws the area being captured the same way.
    readonly property color selectionDim: Qt.rgba(0, 0, 0, 0.5)
    readonly property int selectionOutline: 3
    readonly property real selectionOutlineAlpha: 0.5 // of the accent

    // The Recording's red: its outline, stop and elapsed time (a fixed red, whatever the wallpaper).
    readonly property color recordRed: "#FF453A" // iOS dark-mode red

    // Motion for things that do move (slides), never for state changes.
    readonly property int slideMs: 300

    // The Thumbnail (spec, variant A).
    readonly property int thumbW: 240
    readonly property int thumbH: 150
    readonly property int thumbRadius: 12
    readonly property int thumbGap: 12
    readonly property int thumbEdge: 16          // from the left and bottom screen edges
    readonly property int thumbSlots: 4          // the stack's maximum; the surface is this tall
    // Motion is springs (Spring.qml): a response is about how long the move takes, in seconds.
    readonly property real thumbEnterResponse: 0.5   // springing in from the left edge
    readonly property real thumbEnterBounce: 0.15    // overshoots by about 2 px, then settles
    readonly property real thumbMoveResponse: 0.4    // the others moving up (no bounce)
    readonly property real thumbExitResponse: 0.9    // flicked off the edge: out of sight in about 0.4 s
    readonly property int thumbCheckMs: 200      // label to check
    readonly property int thumbCheckHoldMs: 450  // click to the start of the slide (the user picked 450 at the desk)
    readonly property int historyCheckHoldMs: 1000 // a History tile's check after Copy or Pin, then its icon again
    readonly property int scrimMs: 100
    readonly property real modeBarMorphResponse: 0.16 // seconds: the Mode bar's Text button growing or shrinking (a Spring)
    readonly property real toolRevealResponse: 0.18 // seconds: the Hand tool's slot opening or closing (a Spring)
    readonly property real scrimOpacity: 0.72

    // The Editor window.
    readonly property int barHeight: 52          // the top toolbar and the bottom bar
    readonly property int canvasMargin: 40       // clear space round the shot at Fit

    // The Background panel: pushes the canvas over.
    readonly property int framePanelWidth: 284
    readonly property int framePanelInner: 248
    readonly property int frameTileWidth: 56     // gradient, wallpaper and blurred tiles: four to a row
    readonly property int frameTileHeight: 40

    // The OSD motion and look, used by the auto-hiding Recording bar and the Countdown
    // (OsdFrame.qml). Springs are Spring.qml's `response` (a perceptual duration, in
    // seconds); both have bounce 0.
    readonly property int osdHeight: 48              // the pill's radius is half of it
    readonly property int osdOffset: 24              // from the monitor's bottom edge
    readonly property int osdRise: 16                // px it rises into place from
    readonly property int osdSink: 8                 // px it sinks while leaving
    readonly property real osdScaleFrom: 0.95        // the enter and leave scale
    readonly property real osdFadeBy: 0.6            // opacity is full by this share of the entrance
    readonly property real osdInResponse: 0.25       // rising in
    readonly property real osdOutResponse: 0.2       // sinking out
    readonly property real osdRestDelta: 0.001       // presence runs 0..1, so Spring's 0.1 default would end it at once
    readonly property int shadowMargin: 32           // room round the pill for shadow and lift
    // The pill's shadow: alpha 0.22, y 4, blur 16.
    readonly property color osdShadowColor: Qt.rgba(0, 0, 0, 0.22)
    readonly property int osdShadowOffset: 4
    readonly property int osdShadowBlur: 16
}
