#pragma once
// Storage and naming: where files go and what they are called.
#include <QDateTime>
#include "storage/file_name.h"

#include <QObject>

namespace tarn {

class Daemon;

class Storage : public QObject {
    Q_OBJECT
public:
    explicit Storage(Daemon &daemon) : m_daemon(daemon) {}

    // The Screenshot folder Setting (default <Pictures>/Screenshots; <Pictures> is
    // the XDG Pictures folder, or ~/Pictures).
    QString screenshotDir() const;

    // The File name pattern Setting applied to a capture: the base name, no extension.
    QString baseName(const QDateTime &captureTime, const CaptureName &name) const;
    // The Image format Setting as a file extension ("png", "jpg" or "webp"), and as
    // the position of its type in the Save as dialog (PNG, JPEG, WebP).
    QString imageExtension() const;
    int imageFormatIndex() const;

    // Writes the picture (PNG bytes in, the Image format Setting out) as
    // <screenshotDir>/<baseName>.<ext>, creating the folder if needed. On a name
    // clash it uses _2, _3, ... and never overwrites. Returns the path written, or
    // empty with *error set.
    QString saveScreenshot(const QByteArray &png, const QDateTime &captureTime, QString *error,
                           const CaptureName &name = {});

    // Writes bytes to a new file <dir>/<base>[_n].<extension>, never overwriting.
    static QString writeNew(const QString &dir, const QString &base, const QString &extension, const QByteArray &bytes,
                            QString *error);

    static QString timestampName(const QDateTime &t);

private:
    Daemon &m_daemon;
};

} // namespace tarn
