import QtQuick

// The small value tip a slider shows above its knob while it is dragged (and
// only then). `knobX` is the knob's center in the slider's own coordinates.
Tip {
    property real knobX: 0
    objectName: "valueTip"
    delay: 0
    x: knobX - width / 2
    y: -height - 6
}
