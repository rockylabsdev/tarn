pragma Singleton
import QtQuick

// PROTOTYPE (#3): the one clock every Spring follows. Main.qml emits tick() each
// frame (live) or in fixed 1/60 s steps (shot), so the off-screen shots are exact.
QtObject {
    signal tick(real dt)
}
