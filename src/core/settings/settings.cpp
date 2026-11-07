#include "settings.h"

#include "daemon/daemon.h"
#include "notify/notifier.h"
#include "process/process_runner.h"
#include "storage/file_name.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <toml++/toml.hpp>

namespace tarn {

namespace {

enum class Kind { Folder, Bool, Retention, Seconds, Languages, FileName, ImageFormat, OptionalFile, OptionalColor, Pixels };

struct Def {
    const char *key;
    Kind kind;
    const char *comment;
    const char *fallback; // Bool / Retention / Seconds default as file text; Folder: see defaultFolder()
    const char *zeroMeans = nullptr; // Seconds: what 0 does, for the per-line error ("never", "at once")
};

// The one table. File order is table order.
const Def kDefs[] = {
    {"screenshot_folder", Kind::Folder,
     "Where Save puts screenshots. A folder path; ~ means your home folder.", "pictures"},
    {"recordings_folder", Kind::Folder, "Where Recordings are saved.", "videos"},
    {"keep_history_for", Kind::Retention,
     "How long History keeps the original of every capture. Use a number and a unit:\n"
     "# \"2 weeks\", \"1 month\", \"1 year\" (units: days, weeks, months, years), or \"forever\".\n"
     "# Shortening it sends the newly expired entries to the Trash, not away for good.",
     "1 month"},
    {"copy_automatically", Kind::Bool, "Copy every capture to the clipboard as soon as it is taken (true or false).",
     "false"},
    {"save_automatically", Kind::Bool,
     "Save every capture to the screenshot folder as soon as it is taken (true or false).", "false"},
    {"close_thumbnail_after", Kind::Seconds,
     "Close the Thumbnail by itself after this many seconds, without saving (like the x button).\n"
     "# 0 means never. A Recording's Thumbnail never closes by itself.",
     "0", "never"},
    {"text_languages", Kind::Languages,
     "The languages Text capture reads (Tesseract language codes). Join several with +, for example\n"
     "# \"eng+fra\". Setup offers French (fra) and Spanish (spa); each needs its Tesseract language\n"
     "# data installed (on Arch: tesseract-data-fra, tesseract-data-spa).",
     "eng"},
    {"show_pointer_in_recordings", Kind::Bool, "Show the mouse pointer in Recordings (true or false).\n"
     "# Ignored while Hyprland draws a software pointer (cursor:no_hardware_cursors is not 0): then the\n"
     "# pointer is in every Recording whatever this says.",
     "true"},
    {"wallpaper_folder", Kind::Folder,
     "Images in this folder are offered as Wallpaper Backgrounds in the Editor (after your current wallpaper).\n"
     "# A folder path; ~ means your home folder. Tarn ships no wallpapers.",
     "wallpapers"},
    {"wallpaper_file", Kind::OptionalFile,
     "Offer your current wallpaper as the first Wallpaper Background. A full file path; ~ means your home\n"
     "# folder. The file's first line is the path of the current wallpaper image; any tool can write it, and\n"
     "# Tarn watches it. Empty (or a missing file) offers only the images in wallpaper_folder.",
     ""},
    {"file_name", Kind::FileName,
     "How saved files, kept Recordings and the Copy file are named (no extension, no /). Tokens:\n"
     "# {date} 2026-11-02, {time} 14-03-22, {app} the window's class in lowercase, {title} the window\n"
     "# title cleaned and cut to 60 characters, {kind} area, window, screen or recording. A token with\n"
     "# nothing to say (an area over bare desktop has no {app}) drops with its separator.",
     "{date}_{time}_{app}"},
    {"image_format", Kind::ImageFormat,
     "The file type Save, Save automatically and Save as use: \"png\", \"jpeg\" or \"webp\" (quality 90).\n"
     "# Copy and History always keep the lossless PNG.",
     "png"},
    {"timer_delay", Kind::Seconds, "How many seconds the Timer counts down before it takes the shot. 0 takes it at once.", "3", "at once"},
    {"gif_max_width", Kind::Pixels,
     "The widest a GIF gets, in pixels. A wider GIF is scaled down to this, keeping its shape; a narrower one\n"
     "# is left alone. 0 turns the limit off. The video is always kept at full size, and Trim remakes a GIF to\n"
     "# the same limit.",
     "1280", "no limit"},
    {"palette_file", Kind::OptionalFile,
     "Make Tarn's colors follow a file that another tool writes (a wallpaper color tool, a shell). A full file\n"
     "# path; ~ means your home folder. The file is flat JSON mapping Material 3 color names to \"#rrggbb\":\n"
     "# surface_container_lowest, surface_container_low, surface_container, surface_container_high,\n"
     "# surface_container_highest, surface_bright, on_surface, on_surface_variant, outline, outline_variant,\n"
     "# primary and on_primary. Optional accent and on_accent win over primary and on_primary. Tarn watches\n"
     "# the file and recolors at once. Empty means Tarn's built-in colors.",
     ""},
    {"accent_color", Kind::OptionalColor,
     "A fixed accent color as \"#rrggbb\". It wins over the Palette file's accent and primary.\n"
     "# Empty means the Palette file's accent, then its primary, then Tarn's built-in blue.",
     ""},
};

QString defaultFolder(const QString &which) {
    if (which == QLatin1String("pictures")) {
        QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        if (pictures.isEmpty())
            pictures = QDir::homePath() + QStringLiteral("/Pictures");
        return pictures + QStringLiteral("/Screenshots");
    }
    if (which == QLatin1String("wallpapers")) {
        QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        if (pictures.isEmpty())
            pictures = QDir::homePath() + QStringLiteral("/Pictures");
        return pictures + QStringLiteral("/Wallpapers");
    }
    QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (videos.isEmpty())
        videos = QDir::homePath() + QStringLiteral("/Videos");
    return videos + QStringLiteral("/Tarn");
}

QString withTilde(const QString &path) {
    const QString home = QDir::homePath();
    return path == home || path.startsWith(home + '/') ? QStringLiteral("~") + path.mid(home.size()) : path;
}

QString expandTilde(const QString &path) {
    if (path == QLatin1String("~"))
        return QDir::homePath();
    return path.startsWith(QLatin1String("~/")) ? QDir::homePath() + path.mid(1) : path;
}

QString quoted(const QString &s) {
    QString out = QStringLiteral("\"");
    for (const QChar c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + '"';
}

QVariant defaultValue(const Def &def) {
    switch (def.kind) {
    case Kind::Folder: return defaultFolder(QString::fromLatin1(def.fallback));
    case Kind::Bool: return QLatin1String(def.fallback) == QLatin1String("true");
    case Kind::Retention: return QString::fromLatin1(def.fallback);
    case Kind::Seconds: return QString::fromLatin1(def.fallback).toDouble();
    case Kind::Pixels: return QString::fromLatin1(def.fallback).toInt();
    case Kind::Languages:
    case Kind::FileName:
    case Kind::ImageFormat:
    case Kind::OptionalFile:
    case Kind::OptionalColor: return QString::fromLatin1(def.fallback);
    }
    return {};
}

// Turns a node into the Setting's value, or sets *problem (a plain phrase).
QVariant checked(const Def &def, const toml::node &node, QString *problem) {
    switch (def.kind) {
    case Kind::Folder: {
        if (!node.is_string()) {
            *problem = QStringLiteral("needs a folder path in quotes");
            return {};
        }
        const QString path = expandTilde(QString::fromStdString(*node.value<std::string>()).trimmed());
        if (!path.startsWith('/')) {
            *problem = QStringLiteral("needs a full folder path starting with / or ~");
            return {};
        }
        return QDir::cleanPath(path);
    }
    case Kind::Bool:
        if (node.is_boolean())
            return *node.value<bool>();
        *problem = QStringLiteral("needs true or false");
        return {};
    case Kind::Retention: {
        if (!node.is_string()) {
            *problem = QStringLiteral("needs text like \"1 month\" or \"forever\"");
            return {};
        }
        const QString text = QString::fromStdString(*node.value<std::string>()).trimmed().toLower();
        if (text == QLatin1String("forever"))
            return text;
        static const QRegularExpression re(QStringLiteral("^(\\d+)\\s*(day|week|month|year)s?$"));
        const auto m = re.match(text);
        if (!m.hasMatch() || m.captured(1).toInt() < 1) {
            *problem = QStringLiteral("needs a number and a unit like \"2 weeks\" (days, weeks, months, years), or \"forever\"");
            return {};
        }
        const int n = m.captured(1).toInt();
        return QStringLiteral("%1 %2%3").arg(n).arg(m.captured(2), n == 1 ? QString() : QStringLiteral("s"));
    }
    case Kind::Languages: {
        const QString what = QStringLiteral("needs Tesseract language codes in quotes, like \"eng\" or \"eng+fra\"");
        if (!node.is_string()) {
            *problem = what;
            return {};
        }
        const QStringList parts = QString::fromStdString(*node.value<std::string>())
                                      .split(QRegularExpression(QStringLiteral("[+,\\s]+")), Qt::SkipEmptyParts);
        static const QRegularExpression code(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*$"));
        bool ok = !parts.isEmpty();
        for (const QString &part : parts)
            ok = ok && code.match(part).hasMatch();
        if (!ok) {
            *problem = what;
            return {};
        }
        return parts.join('+');
    }
    case Kind::FileName: {
        if (!node.is_string()) {
            *problem = QStringLiteral("needs a pattern in quotes, like \"{date}_{time}_{app}\"");
            return {};
        }
        const QString pattern = QString::fromStdString(*node.value<std::string>());
        QString why;
        if (!checkFileNamePattern(pattern, &why)) {
            *problem = why;
            return {};
        }
        return pattern;
    }
    case Kind::ImageFormat: {
        const QString text = node.is_string() ? QString::fromStdString(*node.value<std::string>()).trimmed().toLower() : QString();
        if (text != QLatin1String("png") && text != QLatin1String("jpeg") && text != QLatin1String("webp")) {
            *problem = QStringLiteral("needs \"png\", \"jpeg\" or \"webp\" in quotes");
            return {};
        }
        return text;
    }
    case Kind::OptionalFile: {
        if (!node.is_string()) {
            *problem = QStringLiteral("needs a file path in quotes, or \"\" for none");
            return {};
        }
        const QString text = QString::fromStdString(*node.value<std::string>()).trimmed();
        if (text.isEmpty())
            return QString(); // valid and empty: the QVariant must still count as set
        const QString path = expandTilde(text);
        if (!path.startsWith('/')) {
            *problem = QStringLiteral("needs a full file path starting with / or ~, or \"\" for none");
            return {};
        }
        return QDir::cleanPath(path);
    }
    case Kind::OptionalColor: {
        static const QRegularExpression hex(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
        const QString text = node.is_string() ? QString::fromStdString(*node.value<std::string>()).trimmed() : QString();
        if (!node.is_string() || (!text.isEmpty() && !hex.match(text).hasMatch())) {
            *problem = QStringLiteral("needs a color like \"#9abcf2\", or \"\" for none");
            return {};
        }
        return text.toLower();
    }
    case Kind::Seconds: {
        const auto d = node.value<double>();
        if (!(node.is_integer() || node.is_floating_point()) || !d || *d < 0) {
            *problem = QStringLiteral("needs a number of seconds, 0 or more");
            if (def.zeroMeans)
                *problem += QStringLiteral(" (0 means %1)").arg(QLatin1String(def.zeroMeans));
            return {};
        }
        return *d;
    }
    case Kind::Pixels: {
        const auto n = node.is_integer() ? node.value<int64_t>() : std::nullopt;
        if (!n || *n < 0 || *n > 100000) {
            *problem = QStringLiteral("needs a whole number of pixels, 0 or more");
            if (def.zeroMeans)
                *problem += QStringLiteral(" (0 means %1)").arg(QLatin1String(def.zeroMeans));
            return {};
        }
        return int(*n);
    }
    }
    return {};
}

} // namespace

std::optional<QDateTime> Settings::cutoffFor(const QString &retention, const QDateTime &now) {
    static const QRegularExpression re(QStringLiteral("^(\\d+) (day|week|month|year)s?$"));
    const auto m = re.match(retention);
    if (!m.hasMatch())
        return std::nullopt; // "forever"
    const int n = m.captured(1).toInt();
    const QString unit = m.captured(2);
    if (unit == QLatin1String("day"))
        return now.addDays(-n);
    if (unit == QLatin1String("week"))
        return now.addDays(-7 * n);
    if (unit == QLatin1String("month"))
        return now.addMonths(-n);
    return now.addYears(-n);
}

QString Settings::defaultPath() {
    QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (config.isEmpty())
        config = QDir::homePath() + QStringLiteral("/.config");
    return config + QStringLiteral("/tarn/config.toml");
}

Settings::Settings(Daemon &daemon) : QObject(nullptr), m_daemon(daemon), m_path(defaultPath()) {
    for (const Def &def : kDefs)
        m_values.insert(QString::fromLatin1(def.key), defaultValue(def));
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(60);
    connect(&m_debounce, &QTimer::timeout, this, &Settings::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    ensureFile();
    reload();
}

QString Settings::defaultFileText() const {
    QString text = QStringLiteral(
        "# Tarn's Settings. Edit and save: Tarn applies a change at once, with no restart.\n"
        "# Tarn never rewrites this file, so your edits and comments stay. Delete a line to get\n"
        "# its default back. If a line has a mistake, Tarn says which line and keeps that\n"
        "# Setting's last good value.\n");
    for (const Def &def : kDefs) {
        text += QStringLiteral("\n# %1\n").arg(QString::fromLatin1(def.comment));
        const QVariant v = defaultValue(def);
        QString literal;
        switch (def.kind) {
        case Kind::Folder: literal = quoted(withTilde(v.toString())); break;
        case Kind::Retention:
        case Kind::Languages:
        case Kind::FileName:
        case Kind::ImageFormat:
        case Kind::OptionalFile:
        case Kind::OptionalColor: literal = quoted(v.toString()); break;
        default: literal = QString::fromLatin1(def.fallback); break;
        }
        text += QStringLiteral("%1 = %2\n").arg(QString::fromLatin1(def.key), literal);
    }
    return text;
}

// Written once, only if missing; an existing file is never touched.
void Settings::ensureFile() {
    if (QFileInfo::exists(m_path))
        return;
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return; // the defaults apply; nothing to tell the user at start-up
    file.write(defaultFileText().toUtf8());
    file.commit();
}

void Settings::rewatch() {
    // Editors often replace the file instead of rewriting it, which ends a file
    // watch; so watch the folders too, and add everything back after each read.
    const QFileInfo file(m_path);
    const QStringList wanted = {file.absoluteFilePath(), file.absolutePath(), QFileInfo(file.absolutePath()).absolutePath()};
    QStringList todo;
    for (const QString &p : wanted)
        if (QFileInfo::exists(p) && !m_watcher.files().contains(p) && !m_watcher.directories().contains(p))
            todo << p;
    if (!todo.isEmpty())
        m_watcher.addPaths(todo);
}

void Settings::reload() {
    rewatch();
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return; // missing file: what applies now keeps applying; it is not rewritten until the next start
    const QByteArray bytes = file.readAll();
    file.close();
    if (m_haveBytes && bytes == m_lastBytes)
        return; // a second event for the same save
    m_haveBytes = true;
    m_lastBytes = bytes;

    struct Problem {
        int line;
        QString text;
    };
    QList<Problem> problems;
    QSet<QString> keepLast; // keys on lines with a syntax error: they keep their last good value

    // A syntax error makes toml++ reject the whole file. Blank out the line it
    // names (line numbers stay) and parse again, so the other lines still apply.
    QList<QByteArray> lines = bytes.split('\n');
    toml::table root;
    for (int attempt = 0; attempt < 40; ++attempt) {
        const QByteArray text = lines.join('\n');
        try {
            root = toml::parse(std::string_view(text.constData(), size_t(text.size())));
            break;
        } catch (const toml::parse_error &e) {
            const int line = int(e.source().begin.line);
            problems.append({line, QString::fromUtf8(e.description().data(), qsizetype(e.description().length()))});
            root = toml::table();
            if (line < 1 || line > lines.size())
                break;
            const QByteArray bad = lines.at(line - 1);
            const int eq = bad.indexOf('=');
            if (eq > 0)
                keepLast.insert(QString::fromUtf8(bad.left(eq).trimmed()));
            lines[line - 1] = QByteArray();
        }
    }

    QHash<QString, QVariant> next = m_values;
    QSet<QString> present;
    for (const auto &[k, node] : root) {
        const QString key = QString::fromUtf8(k.data(), qsizetype(k.length()));
        const int line = int(node.source().begin.line);
        const Def *def = nullptr;
        for (const Def &d : kDefs)
            if (key == QLatin1String(d.key))
                def = &d;
        if (!def) {
            problems.append({line, QStringLiteral("%1 is not a Tarn Setting").arg(key)});
            continue;
        }
        present.insert(key);
        QString why;
        const QVariant v = checked(*def, node, &why);
        if (v.isValid())
            next.insert(key, v);
        else
            problems.append({line, QStringLiteral("%1 %2").arg(key, why)});
    }
    // A Setting whose line is gone goes back to its default.
    for (const Def &d : kDefs) {
        const QString key = QString::fromLatin1(d.key);
        if (!present.contains(key) && !keepLast.contains(key))
            next.insert(key, defaultValue(d));
    }

    QStringList changedKeys;
    for (auto it = next.cbegin(); it != next.cend(); ++it)
        if (m_values.value(it.key()) != it.value())
            changedKeys << it.key();
    m_values = next;
    for (const QString &key : changedKeys)
        emit changed(key);

    if (!problems.isEmpty()) {
        std::sort(problems.begin(), problems.end(), [](const Problem &a, const Problem &b) { return a.line < b.line; });
        QStringList told;
        for (const Problem &p : problems)
            told << QStringLiteral("line %1: %2").arg(p.line).arg(p.text);
        m_daemon.service<Notifier>().notify(
            problems.size() == 1 ? QStringLiteral("Tarn: a mistake in config.toml, line %1").arg(problems.first().line)
                                 : QStringLiteral("Tarn: mistakes in config.toml"),
            told.join(QStringLiteral("; ")) + QStringLiteral(". The rest of the file still applies."));
    }
}

bool Settings::openInEditor(QString *error) {
    ensureFile();
    return m_daemon.service<ProcessRunner>().openWithDefaultApp(m_path, QStringLiteral("file"), error);
}

// `tarn settings`
TARN_MODULE(settings, [](Daemon &daemon) {
    daemon.service<Settings>(); // writes the file on first start, starts watching
    daemon.addCommand(QStringLiteral("settings"), [&daemon](const QStringList &) {
        QString error;
        return daemon.service<Settings>().openInEditor(&error) ? Reply::success() : Reply::failure(error);
    });
});

} // namespace tarn
