import QtQuick

// One number pulled towards `to` by a spring. Bind something to `value`.
//
// It keeps its speed when `to` changes mid-move, so a second capture taken
// while the stack is still moving bends the motion instead of restarting it.
// Each frame is solved exactly (a damped spring has a closed form), so the
// motion is the same at 60 Hz or 144 Hz.
//
// An iOS-style spring (perceptual duration and bounce: stiffness (2π/d)², damping
// ratio 1 - bounce; here `response` is its duration d), solved in closed form instead
// of integrated. A reduced-motion snap is left out (Tarn has no such signal yet);
// snap() is where it would go.
//
// Tuned the way Apple tunes springs: `response` is about how long the move
// takes, in seconds; `bounce` 0 settles with no overshoot, 0.1 to 0.2 overshoots
// by a hair and reads as "springy" without wobbling.
FrameAnimation {
    id: spring

    property real to: 0
    property real value: 0
    property real velocity: 0 // units per second
    property real response: 0.4
    property real bounce: 0

    // Moves of less than this (in pixels, or whatever `value` is) count as at rest.
    property real restDelta: 0.1
    signal settled()

    // Jump to `to` and stand still there.
    function snap() {
        stop();
        value = to;
        velocity = 0;
    }

    onToChanged: if (value !== to || velocity !== 0) start()

    // A late first frame (the window mapping) would otherwise jump the spring forward.
    onTriggered: step(Math.min(frameTime, 1 / 30))

    function step(dt) {
        const w = 2 * Math.PI / Math.max(0.01, response);
        const z = Math.max(0, Math.min(1, 1 - bounce)); // damping ratio
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
