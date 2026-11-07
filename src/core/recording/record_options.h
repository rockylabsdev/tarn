#pragma once
// What the Record panel chooses for the next Recording, and remembers:
// the mic, system audio and Show clicks toggles (state.toml `[record]`), plus
// whether the pointer can be left out at all (the Setting "Show the pointer in
// Recordings" is moot under Hyprland's software pointer).
//
// Audio is for MP4 only: a GIF has no sound, so `soundFor(Gif)` is always false
// and the panel grays the toggles. The choices are kept as the user left them
// when GIF is picked (they are not cleared).
#include <QObject>

namespace tarn {

class Daemon;

class RecordOptions : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool micOn READ micOn WRITE setMicOn NOTIFY changed)
    Q_PROPERTY(bool systemAudio READ systemAudio WRITE setSystemAudio NOTIFY changed)
    // Show clicks (the Ripples give it an effect); only remembered here.
    Q_PROPERTY(bool showClicks READ showClicks WRITE setShowClicks NOTIFY changed)
    Q_PROPERTY(bool softwarePointer READ softwarePointer NOTIFY pointerChanged)
    Q_PROPERTY(bool pointerSetting READ pointerSetting NOTIFY pointerChanged)
public:
    explicit RecordOptions(Daemon &daemon);

    bool micOn() const { return m_mic; }
    bool systemAudio() const { return m_system; }
    bool showClicks() const { return m_clicks; }
    void setMicOn(bool on);
    void setSystemAudio(bool on);
    void setShowClicks(bool on);

    // The Setting "Show the pointer in Recordings", as written in config.toml.
    bool pointerSetting() const;
    // Asks Hyprland (through the bridge) whether its software pointer is in use;
    // the Record panel calls it when it opens. Cached until the next call.
    Q_INVOKABLE void refreshPointer();
    bool softwarePointer() const { return m_software; }
    // Whether the recorder is told to draw the pointer: the Setting, but under a
    // software pointer it cannot be removed, so it is "yes" then. Asks Hyprland afresh.
    bool pointerShown();

signals:
    void changed();
    void pointerChanged();

private:
    void remember(const char *key, bool on);

    Daemon &m_daemon;
    bool m_mic = false;
    bool m_system = false;
    bool m_clicks = false;
    bool m_software = false;
};

} // namespace tarn
