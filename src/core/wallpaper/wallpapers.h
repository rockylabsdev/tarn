#pragma once
// The wallpapers the Background panel offers, in order:
//   1. the current wallpaper;
//   2. the images in the wallpaper folder (Setting `wallpaper_folder`);
//   ("+" for any image is the panel's own file dialog.)
// Tarn ships no wallpapers and never changes the user's wallpaper: everything
// here only READS.
//
// How "current wallpaper" is found: the `wallpaper_file` Setting names a file,
// written by any tool, whose first line is the current wallpaper's path. It is
// watched live, including a tool that replaces it by renaming over it. Tarn runs
// no command to ask. This is the one place that knows that, behind
// `setCurrentSource()`, so tests swap it. With no file, an unreadable one, or one
// naming a missing image, there is simply no "current" entry: no message, no
// error, and only the folder's images are offered.
#include "settings/watched_file.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <functional>

namespace tarn {

class Daemon;
class Settings;

class Wallpapers : public QObject {
    Q_OBJECT
public:
    explicit Wallpapers(Daemon &daemon);

    // The seam: returns the current wallpaper's path ("" = unknown). Pass an
    // empty function to go back to reading the wallpaper_file Setting's file.
    using CurrentSource = std::function<QString()>;
    void setCurrentSource(CurrentSource source);
    // What the default source reads: the first line of the wallpaper_file Setting's file. "" if none.
    QString fileCurrentWallpaper() const;

    // The current wallpaper if it names a readable image file, else "".
    QString current() const;
    // Images directly in the wallpaper folder, sorted by name (case-insensitive).
    // A missing or empty folder is just an empty list.
    QStringList folderImages() const;
    // current first, then the folder's images (the current one is not repeated).
    // Each: {path, label (file name without extension), current (bool)}.
    QVariantList list() const;

    static bool isImageFile(const QString &path);

signals:
    // The wallpaper folder or file Setting, or the file itself, changed: the list is different now.
    void changed();

private:
    Settings &m_settings;
    WatchedFile m_file;
    CurrentSource m_source;
};

} // namespace tarn
