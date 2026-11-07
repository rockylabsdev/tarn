#pragma once
// The File name pattern: turns `{date}_{time}_{app}` and a capture's facts into the
// base name of a file (no folder, no extension). Pure: no Settings, no clock.
#include <QDateTime>
#include <QString>

namespace tarn {

// What a capture knows about itself, as Hyprland reported it (nothing cleaned yet).
struct CaptureName {
    QString kind;  // "area", "window", "screen" or "recording"
    QString app;   // the window class
    QString title; // the window title
};

// Tokens: {date} 2026-11-02, {time} 14-03-22, {app} (class, lowercased), {title}
// (case kept), {kind}. An empty token drops with the separator next to it; the
// result is cleaned to letters, digits, `.`, `-`, `_`. Never empty: if nothing is
// left it is the date and time.
QString fileBaseName(const QString &pattern, const QDateTime &time, const CaptureName &name);

// The default pattern.
QString defaultFileNamePattern();

// True if the pattern is usable. Otherwise *problem is a plain phrase (an unknown
// token, a `/`, an unclosed brace, empty).
bool checkFileNamePattern(const QString &pattern, QString *problem);

} // namespace tarn
