import QtQuick

// PROTOTYPE (#3): src/ui/qml/Spring.qml's math, driven by Motion.tick instead of a
// FrameAnimation, so off-screen shots can step it frame by frame.
QtObject {
    id: spring
    property real to: 0
    property real value: 0
    property real velocity: 0
    property real response: 0.4
    property real bounce: 0
    property real restDelta: 0.1
    property bool running: false
    signal settled()

    function start() { running = true; }
    function stop() { running = false; }
    function snap() {
        stop();
        value = to;
        velocity = 0;
    }
    onToChanged: if (value !== to || velocity !== 0) start()

    property Connections clock: Connections {
        target: Motion
        function onTick(dt) { if (spring.running) spring.step(dt); }
    }

    function step(dt) {
        const w = 2 * Math.PI / Math.max(0.01, response);
        const z = Math.max(0, Math.min(1, 1 - bounce));
        const d = value - to, v = velocity;
        const e = Math.exp(-z * w * dt);
        let nd, nv;
        if (z < 1) {
            const wd = w * Math.sqrt(1 - z * z);
            const c = Math.cos(wd * dt), s = Math.sin(wd * dt);
            const b = (v + z * w * d) / wd;
            nd = e * (d * c + b * s);
            nv = e * (-z * w * (d * c + b * s) + wd * (b * c - d * s));
        } else {
            const b = v + w * d;
            nd = e * (d + b * dt);
            nv = e * (b - w * (d + b * dt));
        }
        if (Math.abs(nd) < restDelta && Math.abs(nv) < restDelta * 20) {
            stop();
            velocity = 0;
            value = to;
            settled();
            return;
        }
        velocity = nv;
        value = to + nd;
    }
}
