import QtQuick
import QtQuick.Window

// PROTOTYPE (rockylabsdev/tarn#3, throwaway): how the Thumbnails should come back
// after Tarn hides them for a capture. A made-up desktop with Thumbnails in the
// bottom-left corner; Space takes a capture. The stack vanishes at once (it must:
// the shot is being taken), stays gone for the measured gap, then comes back with
// the new Thumbnail in one of four ways.
//   qml6 Main.qml -- live [A|B|C|D] [gap ms]     one window (run.sh)
//   qml6 Main.qml -- shot <out folder>           frames for the contact sheets (shoot.sh)
Window {
    id: win
    width: 1600
    height: 900
    visible: true
    color: "#0e1116"
    title: "Tarn thumbnail return prototype"

    readonly property var cli: {
        const a = Qt.application.arguments, i = a.indexOf("--");
        return i < 0 ? [] : a.slice(i + 1);
    }
    readonly property string mode: cli[0] === "shot" ? "shot" : "live"

    readonly property var variants: ({
        A: "Instant: back in one frame, no motion",
        B: "Spring, together: every card springs in from the left edge (Tarn's enter spring)",
        C: "Spring, staggered: the same, newest first, 60 ms apart",
        D: "Quick fade: fade in with a 20 px nudge from the left, about 0.2 s"
    })
    property string variant: mode === "live" && cli[1] ? cli[1] : "B"
    property int gap: mode === "live" && cli[2] ? parseInt(cli[2]) : 116
    property string kind: "full" // "full": Print, full screen. "overlay": an Overlay capture.
    property real slow: 1
    property bool auto: false

    // --- the virtual clock: everything (springs, the gap, the Overlay) runs on it ---
    property real now: 0 // ms
    property var events: []
    function at(ms, fn) { events.push({ t: now + ms, fn: fn }); }
    function advance(dt) {
        now += dt * 1000;
        for (;;) {
            events.sort((a, b) => a.t - b.t);
            if (!events.length || events[0].t > now)
                break;
            events.shift().fn();
        }
        Motion.tick(dt);
    }
    FrameAnimation {
        running: win.mode === "live"
        onTriggered: win.advance(Math.min(frameTime, 1 / 30) / win.slow)
    }

    // --- the stack ---
    ListModel { id: cards }
    property int nextId: 1
    readonly property int count: cards.count
    readonly property int stackH: Style.thumbEdge + Style.thumbSlots * Style.thumbH + (Style.thumbSlots - 1) * Style.thumbGap
    function slotY(rank) { return stackH - Style.thumbEdge - Style.thumbH - rank * (Style.thumbH + Style.thumbGap); }
    property bool stackShown: true
    property bool overlayShown: false
    property bool busy: false
    property string phase: "ready"
    property real pressedAt: 0

    function reset() {
        events = [];
        cards.clear();
        nextId = 1;
        for (let i = 0; i < 3; ++i)
            cards.append({ cid: nextId, hue: (nextId++ * 0.17) % 1 });
        for (let i = 0; i < cards.count; ++i)
            stack.itemAt(i).placeAtSlot(); // at rest in their places, as before any capture
        stackShown = true;
        overlayShown = false;
        busy = false;
        phase = "ready";
    }

    function capture() {
        if (busy)
            return;
        busy = true;
        pressedAt = now;
        stackShown = false; // in one frame: the shot is being taken
        phase = "hidden (the shot is being taken)";
        if (kind === "full") {
            at(gap, () => comeBack());
        } else {
            at(gap, () => {
                overlayShown = true;
                phase = "Overlay open on the frozen shot";
                at(1500, () => { overlayShown = false; comeBack(); });
            });
        }
    }

    function comeBack() {
        if (cards.count >= Style.thumbSlots)
            cards.remove(0); // overflow: the oldest goes
        const olds = [];
        for (let i = 0; i < cards.count; ++i)
            olds.push(stack.itemAt(i));
        cards.append({ cid: nextId, hue: (nextId++ * 0.17) % 1 });
        const fresh = stack.itemAt(cards.count - 1);
        fresh.enterFromEdge(); // a new Thumbnail always springs in
        for (const c of olds)
            c.placeAtSlot(); // the older ones come back already in their new places
        if (variant === "B") {
            for (const c of olds) c.enterFromEdge();
        } else if (variant === "C") {
            for (const c of olds) {
                c.hideForEntrance();
                at(c.rank * 60, () => c.enterFromEdge());
            }
        } else if (variant === "D") {
            for (const c of olds) c.fadeNudge();
        }
        stackShown = true;
        phase = "back after " + Math.round(now - pressedAt) + " ms (" + variant + ")";
        at(900, () => { busy = false; phase = "ready (last: hidden for " + gap + " ms)"; });
    }

    Timer { interval: 2500; repeat: true; running: win.auto && win.mode === "live"; onTriggered: win.capture() }

    // --- a made-up desktop ---
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#1d2b3a" }
            GradientStop { position: 1; color: "#3a2a44" }
        }
    }
    Repeater {
        model: [{ x: 420, y: 130, w: 760, h: 520, t: "Notes" }, { x: 980, y: 300, w: 520, h: 420, t: "Browser" }]
        Rectangle {
            required property var modelData
            x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h
            radius: 10; color: "#f3f1ec"; border.color: Qt.rgba(0, 0, 0, 0.3)
            Rectangle { width: parent.width; height: 34; radius: 10; color: "#dcd8cf"
                Text { anchors.centerIn: parent; text: modelData.t; color: "#333"; font.pixelSize: 14 } }
            Column {
                x: 28; y: 64; spacing: 14
                Repeater { model: 12; Rectangle { width: (modelData.w - 80) * (0.5 + ((index * 37) % 50) / 100); height: 9; radius: 4; color: "#cfcac0" } }
            }
        }
    }

    // --- the Thumbnails, in the bottom-left corner ---
    Item {
        id: stackLayer
        x: 0
        y: win.height - win.stackH
        width: Style.thumbEdge + Style.thumbW + 40
        height: win.stackH
        visible: win.stackShown
        Repeater {
            id: stack
            model: cards
            delegate: Card { board: win }
        }
    }

    // --- a stand-in for the Overlay ---
    Rectangle {
        anchors.fill: parent
        visible: win.overlayShown
        color: Qt.rgba(0, 0, 0, 0.45)
        Text { anchors.centerIn: parent; color: "white"; font.pixelSize: 20; text: "Overlay (frozen shot): the area is taken in 1.5 s" }
    }

    // --- controls ---
    component Btn: Rectangle {
        id: btn
        property string label
        property bool on: false
        signal clicked()
        width: lbl.implicitWidth + 22; height: 30; radius: 15
        color: on ? "#e8e8ea" : (ma.containsMouse ? "#3a3d44" : "#2a2d33")
        Text { id: lbl; anchors.centerIn: parent; text: btn.label; color: btn.on ? "#111" : "#eee"; font.pixelSize: 13 }
        MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; onClicked: btn.clicked() }
    }
    Rectangle {
        visible: win.mode === "live"
        anchors.horizontalCenter: parent.horizontalCenter
        y: 16
        width: col.implicitWidth + 32; height: col.implicitHeight + 24; radius: 18
        color: Qt.rgba(0.08, 0.09, 0.11, 0.92)
        Column {
            id: col
            anchors.centerIn: parent
            spacing: 8
            Row {
                spacing: 6
                Text { text: "Return"; color: "#aaa"; width: 70; anchors.verticalCenter: parent.verticalCenter }
                Repeater { model: ["A", "B", "C", "D"]; Btn { required property string modelData; label: modelData; on: win.variant === modelData; onClicked: win.variant = modelData } }
                Text { text: "Gap"; color: "#aaa"; leftPadding: 18; anchors.verticalCenter: parent.verticalCenter }
                Repeater { model: [35, 116, 402]; Btn { required property int modelData; label: modelData + " ms"; on: win.gap === modelData; onClicked: win.gap = modelData } }
            }
            Row {
                spacing: 6
                Text { text: "Capture"; color: "#aaa"; width: 70; anchors.verticalCenter: parent.verticalCenter }
                Btn { label: "Full screen"; on: win.kind === "full"; onClicked: win.kind = "full" }
                Btn { label: "Overlay"; on: win.kind === "overlay"; onClicked: win.kind = "overlay" }
                Text { text: "Speed"; color: "#aaa"; leftPadding: 18; anchors.verticalCenter: parent.verticalCenter }
                Btn { label: "Real"; on: win.slow === 1; onClicked: win.slow = 1 }
                Btn { label: "4× slower"; on: win.slow === 4; onClicked: win.slow = 4 }
                Item { width: 12; height: 1 }
                Btn { label: "Capture (Space)"; onClicked: win.capture() }
                Btn { label: "Every 2.5 s"; on: win.auto; onClicked: win.auto = !win.auto }
                Btn { label: "Reset"; onClicked: win.reset() }
            }
            Text { text: win.variant + ": " + win.variants[win.variant]; color: "#ddd"; font.pixelSize: 13 }
            Text { text: "Now: " + win.phase + "   ·   F full screen, 1–4 or A–D return, G gap, Esc quit"; color: "#999"; font.pixelSize: 12 }
        }
    }

    Item {
        focus: true
        Keys.onPressed: (e) => {
            const k = e.key;
            if (k === Qt.Key_Space) win.capture();
            else if (k >= Qt.Key_1 && k <= Qt.Key_4) win.variant = "ABCD"[k - Qt.Key_1];
            else if (k >= Qt.Key_A && k <= Qt.Key_D) win.variant = "ABCD"[k - Qt.Key_A];
            else if (k === Qt.Key_G) win.gap = win.gap === 35 ? 116 : win.gap === 116 ? 402 : 35;
            else if (k === Qt.Key_F) win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen;
            else if (k === Qt.Key_Escape) Qt.quit();
        }
    }

    // --- off-screen frames for the contact sheets ---
    readonly property var shotTimes: [-20, 60, 0, 50, 100, 200, 350, 600] // after the press; 0.. are after the return
    function shootAll() {
        const out = cli[1], jobs = [];
        for (const v of ["A", "B", "C", "D"])
            for (let i = 0; i < shotTimes.length; ++i)
                jobs.push({ v: v, i: i });
        let cur = "";
        function next() {
            if (!jobs.length) { Qt.quit(); return; }
            const j = jobs.shift();
            if (j.v !== cur) {
                cur = j.v;
                variant = j.v;
                reset();
                now = 0;
            }
            const t = shotTimes[j.i];
            const target = j.i < 2 ? t : gap + t; // the first two are before the stack is back
            if (j.i === 1 && !busy) capture();
            while (now < target - 0.5) advance(1 / 60);
            win.contentItem.grabToImage((r) => {
                r.saveToFile(out + "/" + j.v + "-" + j.i + ".png");
                next();
            });
        }
        next();
    }
    // Off screen the window has no size until it is shown; wait a moment before the first grab.
    Timer { id: shotStart; interval: 400; onTriggered: win.shootAll() }
    Component.onCompleted: {
        reset();
        if (mode === "shot")
            shotStart.start();
    }
}
