import QtQuick

// The Recording's frames side by side (one ffmpeg picture, `tiles` frames wide),
// cropped to the strip's width. Used twice by the Trim window: the whole strip
// (dimmed) and the kept part (not dimmed).
Item {
    id: root
    property int tiles: 1
    property real tileW: 80
    clip: true
    Image {
        objectName: "stripImage"
        width: root.tiles * root.tileW
        height: root.height
        asynchronous: true
        cache: true // the same picture is asked for twice; the generation in the URL makes a new file a new picture
        source: "image://trim/" + session.captureId + "/" + session.generation + "/strip" + root.tiles
    }
}
