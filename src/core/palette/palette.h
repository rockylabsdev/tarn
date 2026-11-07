#pragma once
// The Palette: Tarn's colors, following a Palette file live.
//
// The `palette_file` Setting names a flat JSON file, written by any tool, that
// maps Material 3 color names to "#rrggbb". This service reads it, keeps
// watching it (including a tool that replaces it by renaming over it), and
// exposes the colors the screens use as properties. With no file, or one that
// cannot be read, every color is Tarn's built-in fallback. A single missing or
// malformed key falls back on its own; the rest still follow the file.
//
// Accent: the `accent_color` Setting wins, then the file's `accent`, then its
// `primary`, then the built-in blue. The text on the accent is the file's
// `on_accent`, then `on_primary`, then the built-in.
#include <QColor>
#include "settings/watched_file.h"

#include <QObject>

namespace tarn {

class Daemon;

class Palette : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool fromFile READ fromFile NOTIFY changed)
    Q_PROPERTY(QColor canvas READ canvas NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor raised READ raised NOTIFY changed)
    Q_PROPERTY(QColor control READ control NOTIFY changed)
    Q_PROPERTY(QColor hover READ hover NOTIFY changed)
    Q_PROPERTY(QColor pressed READ pressed NOTIFY changed)
    Q_PROPERTY(QColor track READ track NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor text2 READ text2 NOTIFY changed)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed)
    Q_PROPERTY(QColor disabled READ disabled NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor divider READ divider NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY changed)
public:
    // The daemon's service: follows the palette_file and accent_color Settings.
    explicit Palette(Daemon &);
    // Watches an explicit Palette file path (empty: none); the Settings do not apply.
    explicit Palette(const QString &palettePath, QObject *parent = nullptr);

    // True while the colors come from a readable Palette file.
    bool fromFile() const { return m_fromFile; }

    QColor canvas() const { return m_c[Canvas]; }
    QColor surface() const { return m_c[Surface]; }
    QColor raised() const { return m_c[Raised]; }
    QColor control() const { return m_c[Control]; }
    QColor hover() const { return m_c[Hover]; }
    QColor pressed() const { return m_c[Pressed]; }
    QColor track() const { return m_c[Track]; }
    QColor text() const { return m_c[Text]; }
    QColor text2() const { return m_c[Text2]; }
    QColor muted() const { return m_c[Muted]; }
    QColor disabled() const { return m_c[Disabled]; }
    QColor border() const { return m_c[Border]; }
    QColor divider() const { return m_c[Divider]; }
    QColor accent() const { return m_c[Accent]; }
    QColor accentText() const { return m_c[AccentText]; }

    // Re-reads the file now (the watcher calls this, slightly debounced).
    void reload();

signals:
    void changed();

private:
    enum Token { Canvas, Surface, Raised, Control, Hover, Pressed, Track, Text, Text2, Muted, Disabled, Border, Divider, Accent, AccentText, TokenCount };
    void init();

    QString m_path;
    WatchedFile m_file;
    QColor m_accentSetting; // invalid when the accent_color Setting is empty
    bool m_fromFile = false;
    QColor m_c[TokenCount];
};

} // namespace tarn
