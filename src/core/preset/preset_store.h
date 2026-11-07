#pragma once
// Presets: Frames saved under a name in ~/.config/tarn/presets.toml, and the
// one Preset marked to frame every capture. The "Frame and Preset store" of the spec.
//
// The file is the user's as much as Tarn's, so Tarn never rewrites it as a whole:
//   - It is read afresh on every call, so a hand edit applies at once (the next
//     capture, the next look at the panel). A mistake in the file is reported in
//     ONE notification (a line of text) and Tarn keeps working from the last good
//     reading; a preset with a mistake is skipped, the others still apply.
//   - Writing is text surgery: saving or deleting a preset replaces or removes only
//     that preset's own tables (`[preset."Name"]` and `[preset."Name".background]`),
//     and setting the auto-applied Preset only touches the one `auto = "Name"` line.
//     Every other byte (comments, blank lines, other presets, key order) stays.
//     The only thing lost is a comment INSIDE the preset being re-saved.
//   - After a write the result is parsed again and must equal what was meant. If
//     it does not (the user wrote the file in a form Tarn can't edit safely, such
//     as dotted keys) nothing is written and a notification says to edit by hand.
//   - A file that does not parse at all is never written to.
//
// File shape (what Tarn writes; every key is optional and has the Frame's default):
//
//   auto = "Sunset card"          # applies to every capture (a top-level key)
//
//   [preset."Sunset card"]
//   padding = 0.08                # fractions of the shot's shorter side
//   inset = 0.0
//   auto_balance = false
//   shadow = 0.03
//   corners = 0.02
//   align_h = 0                   # -1 left, 0 center, 1 right
//   align_v = 0                   # -1 top, 0 center, 1 bottom
//   ratio = "Auto"                # or "16:9"
//
//   [preset."Sunset card".watermark]   # optional; the image is kept by path
//   mode = "image"                # image (path = "...") | text (text = "...")
//   corner = "bottom-right"       # top-left | top-right | bottom-left | bottom-right
//   size = 0.08                   # 0.03 - 0.25 of the shot's shorter side
//   opacity = 0.6                 # 0.1 - 1
//
//   [preset."Sunset card".background]
//   kind = "image-gradient"       # image-gradient | gradient (Palette) | wallpaper | blur | plain
//   name = "glacial-bloom"        # an image gradient is stored by its name
//
// Following the wallpaper: a Palette gradient (`slots = [...]`) is re-resolved
// through the live Palette every time it is applied, and `current = true` on a
// wallpaper background means "whatever the wallpaper is now". With no Palette
// (no Palette file) a Palette gradient falls back to the closest image gradient by average color (so does a Preset naming one of the
// old color-stop gradients);
// a deleted wallpaper file falls back quietly (Background `missing`).
#include "frame/frame.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace tarn {

class Daemon;

class PresetStore : public QObject {
    Q_OBJECT
public:
    explicit PresetStore(Daemon &daemon);

    // $XDG_CONFIG_HOME/tarn/presets.toml
    static QString defaultPath();
    QString path() const { return m_path; }

    // Preset names, sorted without regard to case.
    QStringList names();
    bool contains(const QString &name);
    // The Frame as stored (Palette gradients keep their slots).
    std::optional<Frame> preset(const QString &name);
    // The Frame to paint now: Palette gradients and the current wallpaper follow
    // the desktop. Missing wallpapers and Palettes are already dealt with.
    std::optional<Frame> resolved(const QString &name, bool notify = false);
    // Same step for any Frame (the Editor applies it when a Preset is picked).
    Frame resolve(const Frame &frame, bool notify = false) const;
    // True when the Preset's wallpaper file is gone (the quiet mark).
    bool isMissing(const QString &name);

    // Saves `frame` under `name` (replacing a preset of that name). A Frame with no
    // Background ("none") cannot be a Preset. False after a notification on failure.
    bool save(const QString &name, const Frame &frame, QString *error = nullptr);
    bool remove(const QString &name, QString *error = nullptr);

    // The auto-applied Preset: "" = none. It is also "none" when the name no longer exists.
    QString autoName();
    bool hasAuto() { return !autoName().isEmpty(); }
    bool setAuto(const QString &name, QString *error = nullptr); // "" clears it
    // The auto-applied Preset's resolved Frame, if there is one.
    std::optional<Frame> autoFrame();

signals:
    void changed(); // after Tarn wrote the file

private:
    struct Loaded {
        QList<QPair<QString, Frame>> presets;
        QString autoName;
        bool parsed = false; // false: the file is unreadable or has a syntax error
    };
    const Loaded &refresh();
    QByteArray readFile(bool *exists) const;
    bool write(const QByteArray &newBytes, const QString &what, QString *error);
    void reportOnce(const QString &problem);

    Daemon &m_daemon;
    QString m_path;
    QByteArray m_bytes; // what m_loaded was read from
    bool m_haveBytes = false;
    Loaded m_loaded;
    QString m_lastReported;
};

} // namespace tarn
