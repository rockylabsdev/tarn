#pragma once
// Settings: the user's hand-edited file, $XDG_CONFIG_HOME/tarn/config.toml
// (default ~/.config/tarn/config.toml). Written once with comments if it is
// missing, never rewritten by Tarn afterwards, watched and applied live. A bad
// line raises ONE notification naming the line(s); only the Setting on that
// line keeps its last good value and every other Setting still applies.
// Never mixed with Remembered state (state.toml).
//
// Reading a Setting (the Editor, Recording and everyone else):
//     auto &settings = daemon.service<Settings>();
//     settings.copyAutomatically(); settings.recordingsFolder(); ...
// and connect to `changed(key)` if you need to react to an edit. Always ask the
// service at the moment you need the value: it is live.
//
// Adding a Setting: one entry in the table `kDefs` in
// settings.cpp (key, kind, default, comment) plus a typed accessor below. The
// file template, validation and the per-line errors come from the table.
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariant>
#include <optional>

namespace tarn {

class Daemon;

class Settings : public QObject {
    Q_OBJECT
public:
    explicit Settings(Daemon &daemon);

    static QString defaultPath();
    QString path() const { return m_path; }

    // ---- the Settings ----
    // Where Save puts screenshots (absolute; `~` in the file is expanded).
    QString screenshotFolder() const { return value(QStringLiteral("screenshot_folder")).toString(); }
    // Where Recordings are saved (Recording).
    QString recordingsFolder() const { return value(QStringLiteral("recordings_folder")).toString(); }
    // "1 month", "2 weeks", ... or "forever". Always in that canonical spelling.
    QString keepHistoryFor() const { return value(QStringLiteral("keep_history_for")).toString(); }
    // Entries captured before this are expired; nullopt = keep forever.
    std::optional<QDateTime> retentionCutoff(const QDateTime &now) const { return cutoffFor(keepHistoryFor(), now); }
    static std::optional<QDateTime> cutoffFor(const QString &retention, const QDateTime &now);
    // Copy / Save automatically after a capture (the Thumbnail still shows).
    bool copyAutomatically() const { return value(QStringLiteral("copy_automatically")).toBool(); }
    bool saveAutomatically() const { return value(QStringLiteral("save_automatically")).toBool(); }
    // Close the Thumbnail after this many seconds without saving; 0 = never.
    // Never applies to a Recording (the Thumbnail stack refuses).
    double closeThumbnailAfterSeconds() const { return value(QStringLiteral("close_thumbnail_after")).toDouble(); }
    // Text capture's Tesseract languages, "eng" or "eng+fra" (the -l argument).
    QString textLanguages() const { return value(QStringLiteral("text_languages")).toString(); }
    // The pointer in Recordings (Recording).
    bool showPointerInRecordings() const { return value(QStringLiteral("show_pointer_in_recordings")).toBool(); }

    // The folder whose images the Background panel offers as wallpapers.
    QString wallpaperFolder() const { return value(QStringLiteral("wallpaper_folder")).toString(); }

    // The Palette file Tarn's colors follow (absolute), or empty for none.
    QString wallpaperFile() const { return value(QStringLiteral("wallpaper_file")).toString(); }
    QString paletteFile() const { return value(QStringLiteral("palette_file")).toString(); }
    // A fixed accent as lowercase "#rrggbb", or empty for none.
    QString accentColor() const { return value(QStringLiteral("accent_color")).toString(); }

    // The Timer's Countdown length in seconds (default 3; 0 = shoot at once).
    double timerDelaySeconds() const { return value(QStringLiteral("timer_delay")).toDouble(); }

    // The File name pattern (tokens {date} {time} {app} {title} {kind}) and the
    // Image format ("png", "jpeg" or "webp") that Save, auto-save and Save as write.
    QString fileNamePattern() const { return value(QStringLiteral("file_name")).toString(); }
    QString imageFormat() const { return value(QStringLiteral("image_format")).toString(); }

    // The widest a GIF gets in pixels; 0 = no limit.
    int gifMaxWidth() const { return value(QStringLiteral("gif_max_width")).toInt(); }

    // Any Setting by its file key (a QVariant of the kind the table says).
    QVariant value(const QString &key) const { return m_values.value(key); }

    // `tarn settings`: opens the file in the user's editor with `xdg-open`
    // (the file is created first if it is missing). False with *error set if
    // the opener could not be started.
    bool openInEditor(QString *error);

    // The commented file as Tarn writes it on first start.
    QString defaultFileText() const;

signals:
    // A Setting got a different value (after load, edit or fix).
    void changed(const QString &key);

private:
    void ensureFile();
    void reload();
    void rewatch();

    Daemon &m_daemon;
    QString m_path;
    QHash<QString, QVariant> m_values;
    QByteArray m_lastBytes;
    bool m_haveBytes = false;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};

} // namespace tarn
