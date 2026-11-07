#include "preset_store.h"

#include "daemon/daemon.h"
#include "frame/gradient_background.h"
#include "frame/image_gradient_background.h"
#include "notify/notifier.h"
#include "palette/palette.h"
#include "toml/toml_variant.h"
#include "wallpaper/wallpapers.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>
#include <sstream>
#include <toml++/toml.hpp>

namespace tarn {

namespace {

const QString kHeader = QStringLiteral(
    "# Tarn presets: Frames saved from the Editor's Background panel.\n"
    "# This file is yours to edit by hand. Tarn reads it again each time it needs it.\n"
    "# When Tarn saves or deletes a preset it changes only that preset's own tables and\n"
    "# the `auto = ...` line. Your comments and everything else stay as they are\n"
    "# (the one exception: comments inside a preset that Tarn re-saves).\n"
    "#\n"
    "# auto = \"Name\"   frames every capture with that preset (B in the Overlay skips it once)\n"
    "#\n"
    "# [preset.\"Name\"] lengths (padding, inset, shadow, corners) are fractions of the\n"
    "# shot's shorter side; align_h / align_v are -1, 0 or 1; ratio is \"Auto\" or \"16:9\".\n"
    "# [preset.\"Name\".watermark] (optional) mode is \"image\" (path = \"/logo.png\") or \"text\" (text = \"@me\");\n"
    "# corner is top-left, top-right, bottom-left or bottom-right; size 0.03-0.25, opacity 0.1-1.\n"
    "# [preset.\"Name\".background] kind is gradient, wallpaper, blur or plain.\n"
    "#   gradient   stops = [\"#rrggbb\", ...], angle = 135. With slots = [\"accent\", \"canvas\"]\n"
    "#              the colors follow your wallpaper (the Palette).\n"
    "#   wallpaper  path = \"/path/to/image\", or current = true to follow your wallpaper.\n"
    "#   blur       style = \"light\" | \"heavy\" | \"dark\".\n"
    "#   plain      color = \"#aarrggbb\" (\"#00000000\" is transparent).\n");

// ── toml <-> QVariant ───────────────────────────────────────────────────────────────

using tomlvariant::toQString;
QString q(std::string_view s) { return toQString(s); }

// Strings are always written in "double quotes" (toml++ would pick 'literal' ones).
constexpr auto kFlags = toml::toml_formatter::default_flags & ~toml::format_flags::allow_literal_strings;

template <class T>
QString literal(const T &value) {
    std::ostringstream out;
    out << toml::toml_formatter{toml::value<T>(value), kFlags};
    return QString::fromStdString(out.str());
}
QString quoted(const QString &s) { return literal(s.toStdString()); }

// ── Frame <-> text ──────────────────────────────────────────────────────────────────

// One preset as the text Tarn writes: the main table and its background table.
QString blockFor(const QString &name, const Frame &f) {
    const QString key = quoted(name);
    QString out;
    out += QStringLiteral("[preset.%1]\n").arg(key);
    out += QStringLiteral("padding = %1\n").arg(literal(double(f.padding)));
    out += QStringLiteral("inset = %1\n").arg(literal(double(f.inset)));
    out += QStringLiteral("auto_balance = %1\n").arg(literal(f.autoBalance));
    out += QStringLiteral("shadow = %1\n").arg(literal(double(f.shadow)));
    out += QStringLiteral("corners = %1\n").arg(literal(double(f.corners)));
    out += QStringLiteral("align_h = %1\n").arg(f.alignH);
    out += QStringLiteral("align_v = %1\n").arg(f.alignV);
    out += QStringLiteral("ratio = %1\n").arg(quoted(f.ratio));
    if (f.watermark.on()) {
        const Watermark &w = f.watermark;
        out += QStringLiteral("\n[preset.%1.watermark]\n").arg(key);
        out += QStringLiteral("mode = %1\n").arg(quoted(Watermark::modeName(w.mode)));
        if (w.mode == Watermark::Image)
            out += QStringLiteral("path = %1\n").arg(quoted(w.path));
        else
            out += QStringLiteral("text = %1\n").arg(quoted(w.text));
        out += QStringLiteral("corner = %1\n").arg(quoted(Watermark::cornerName(w.corner)));
        out += QStringLiteral("size = %1\n").arg(literal(double(w.size)));
        out += QStringLiteral("opacity = %1\n").arg(literal(double(w.opacity)));
    }
    out += QStringLiteral("\n[preset.%1.background]\n").arg(key);
    out += QStringLiteral("kind = %1\n").arg(quoted(f.background.kind));
    QVariantMap rest = f.background.params;
    rest.remove(QStringLiteral("kind"));
    toml::table table;
    for (auto it = rest.begin(); it != rest.end(); ++it)
        tomlvariant::assign(table, it.key(), it.value());
    std::ostringstream body;
    body << toml::toml_formatter{table, kFlags};
    out += QString::fromStdString(body.str());
    if (!out.endsWith(QLatin1Char('\n')))
        out += QLatin1Char('\n');
    return out;
}

// A preset table of the file -> Frame, or why not.
std::optional<Frame> frameFrom(const toml::table &t, QString *why) {
    QVariantMap watermark;
    if (const toml::table *wt = t["watermark"].as_table())
        watermark = tomlvariant::fromNode(*wt).toMap();
    const bool hasWatermark = Watermark::fromMap(watermark).on();
    const toml::table *bg = t["background"].as_table();
    if (!bg && !hasWatermark) {
        *why = QStringLiteral("has no [background] table");
        return std::nullopt;
    }
    QVariantMap background = bg ? tomlvariant::fromNode(*bg).toMap() : QVariantMap();
    QString kind = background.value(QStringLiteral("kind")).toString();
    if (kind.isEmpty() && hasWatermark)
        kind = QStringLiteral("none"); // a Watermark on its own
    if (kind.isEmpty() || (kind == QLatin1String("none") && !hasWatermark)) {
        *why = QStringLiteral("its background needs a kind (gradient, wallpaper, blur or plain)");
        return std::nullopt;
    }
    if (kind != QLatin1String("none") && !BackgroundKinds::find(kind)) {
        *why = QStringLiteral("its background kind \"%1\" is unknown").arg(kind);
        return std::nullopt;
    }
    background.insert(QStringLiteral("kind"), kind);
    QVariantMap map;
    map.insert(QStringLiteral("background"), background);
    map.insert(QStringLiteral("watermark"), watermark);
    const std::pair<const char *, const char *> keys[] = {
        {"padding", "padding"}, {"inset", "inset"},     {"shadow", "shadow"},     {"corners", "corners"},
        {"auto_balance", "autoBalance"}, {"align_h", "ah"}, {"align_v", "av"}, {"ratio", "ratio"},
    };
    for (const auto &[file, internal] : keys) {
        const toml::node *node = t.get(file);
        const QVariant v = node ? tomlvariant::fromNode(*node) : QVariant();
        if (!v.isValid())
            continue;
        const bool wantsNumber = QLatin1String(internal) != QLatin1String("ratio") && QLatin1String(internal) != QLatin1String("autoBalance");
        if (wantsNumber && !v.canConvert<double>())
            continue;
        if (wantsNumber && v.typeId() == QMetaType::QString)
            continue;
        map.insert(QLatin1String(internal), v);
    }
    return Frame::fromMap(map);
}

// ── Reading ─────────────────────────────────────────────────────────────────────────

QString parseMessage(const toml::parse_error &e) {
    return QStringLiteral("line %1: %2").arg(e.source().begin.line).arg(q(e.description()));
}

// ── Text surgery ────────────────────────────────────────────────────────────────────

std::optional<QStringList> headerPath(const QString &line) {
    const QString t = line.trimmed();
    if (!t.startsWith(QLatin1Char('[')) || t.startsWith(QStringLiteral("[[")))
        return std::nullopt;
    try {
        toml::table table = toml::parse(t.toStdString() + "\n");
        QStringList path;
        const toml::table *cur = &table;
        while (cur && cur->size() == 1) {
            const auto it = cur->begin();
            path << q(it->first.str());
            cur = it->second.as_table();
        }
        if (path.isEmpty())
            return std::nullopt;
        return path;
    } catch (const toml::parse_error &) {
        return std::nullopt;
    }
}

bool isBlankOrComment(const QString &line) {
    const QString t = line.trimmed();
    return t.isEmpty() || t.startsWith(QLatin1Char('#'));
}

struct Lines {
    QStringList list;
    bool crlf = false;
    static Lines of(const QString &text) {
        Lines l;
        l.crlf = text.contains(QStringLiteral("\r\n"));
        l.list = text.split(QLatin1Char('\n'));
        if (l.list.size() > 1 && l.list.last().isEmpty())
            l.list.removeLast(); // the final newline is not a line
        else if (l.list.size() == 1 && l.list.first().isEmpty())
            l.list.clear();
        return l;
    }
    QString text() const {
        if (list.isEmpty())
            return {};
        QString out = list.join(QLatin1Char('\n'));
        out += QLatin1Char('\n');
        return out;
    }
    QString eol(const QString &s) const { return crlf ? s + QLatin1Char('\r') : s; }
};

QStringList splitBlock(const QString &block, const Lines &ctx) {
    QStringList lines = block.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    for (QString &l : lines)
        l = ctx.eol(l);
    return lines;
}

// Replaces (or, with an empty block, removes) every table of preset `name`.
QString editPreset(const QString &text, const QString &name, const QString &block) {
    Lines lines = Lines::of(text);
    struct Range {
        int begin, end; // end exclusive
    };
    QList<Range> ranges;
    int sectionStart = -1;
    auto closeSection = [&](int next) {
        if (sectionStart < 0)
            return;
        int end = next;
        while (end > sectionStart + 1 && isBlankOrComment(lines.list[end - 1]))
            --end; // trailing comments and blanks belong to whatever follows
        ranges.append({sectionStart, end});
        sectionStart = -1;
    };
    for (int i = 0; i < lines.list.size(); ++i) {
        const auto path = headerPath(lines.list[i]);
        if (!path)
            continue;
        closeSection(i);
        if (path->size() >= 2 && path->at(0) == QLatin1String("preset") && path->at(1) == name)
            sectionStart = i;
    }
    closeSection(lines.list.size());

    const QStringList replacement = block.isEmpty() ? QStringList() : splitBlock(block, lines);
    if (ranges.isEmpty()) {
        if (replacement.isEmpty())
            return text;
        while (!lines.list.isEmpty() && lines.list.last().trimmed().isEmpty())
            lines.list.removeLast();
        if (!lines.list.isEmpty())
            lines.list << lines.eol(QString());
        lines.list << replacement;
        return lines.text();
    }
    // Remove from the back so earlier indexes stay valid; the first range gets the new text.
    for (int r = int(ranges.size()) - 1; r >= 0; --r) {
        const Range range = ranges[r];
        for (int i = range.end - 1; i >= range.begin; --i)
            lines.list.removeAt(i);
        if (r == 0) {
            for (int i = 0; i < replacement.size(); ++i)
                lines.list.insert(range.begin + i, replacement[i]);
            if (replacement.isEmpty() && range.begin > 0 && lines.list[range.begin - 1].trimmed().isEmpty() &&
                (range.begin >= lines.list.size() || lines.list[range.begin].trimmed().isEmpty()))
                lines.list.removeAt(range.begin - 1); // do not leave two blank lines where a preset was
        }
    }
    return lines.text();
}

// Sets (or, with an empty name, removes) the top-level `auto = "..."` line.
QString editAuto(const QString &text, const QString &name) {
    Lines lines = Lines::of(text);
    int firstHeader = lines.list.size();
    for (int i = 0; i < lines.list.size(); ++i)
        if (headerPath(lines.list[i])) {
            firstHeader = i;
            break;
        }
    static const QRegularExpression autoLine(QStringLiteral("^\\s*auto\\s*="));
    int found = -1;
    for (int i = 0; i < firstHeader; ++i)
        if (lines.list[i].contains(autoLine)) {
            found = i;
            break;
        }
    const QString line = lines.eol(QStringLiteral("auto = %1").arg(quoted(name)));
    if (found >= 0) {
        if (name.isEmpty())
            lines.list.removeAt(found);
        else
            lines.list[found] = line;
        return lines.text();
    }
    if (name.isEmpty())
        return text;
    int at = 0;
    for (int i = 0; i < firstHeader; ++i)
        if (!lines.list[i].trimmed().isEmpty())
            at = i + 1;
    lines.list.insert(at, line);
    if (at + 1 < lines.list.size() && !lines.list[at + 1].trimmed().isEmpty())
        lines.list.insert(at + 1, lines.eol(QString()));
    return lines.text();
}

void dropEmptyPresets(toml::table &root) {
    if (const toml::table *p = root["preset"].as_table(); p && p->empty())
        root.erase("preset");
}

} // namespace

// ── PresetStore ─────────────────────────────────────────────────────────────────────

QString PresetStore::defaultPath() {
    QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (config.isEmpty())
        config = QDir::homePath() + QStringLiteral("/.config");
    return config + QStringLiteral("/tarn/presets.toml");
}

PresetStore::PresetStore(Daemon &daemon) : QObject(nullptr), m_daemon(daemon), m_path(defaultPath()) {}

QByteArray PresetStore::readFile(bool *exists) const {
    QFile file(m_path);
    *exists = file.open(QIODevice::ReadOnly);
    return *exists ? file.readAll() : QByteArray();
}

void PresetStore::reportOnce(const QString &problem) {
    if (problem == m_lastReported)
        return;
    m_lastReported = problem;
    if (!problem.isEmpty())
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn has a problem with presets.toml"), problem);
}

const PresetStore::Loaded &PresetStore::refresh() {
    bool exists = false;
    const QByteArray bytes = readFile(&exists);
    if (m_haveBytes && bytes == m_bytes)
        return m_loaded;
    m_bytes = bytes;
    m_haveBytes = true;

    toml::table root;
    try {
        root = toml::parse(std::string_view(bytes.constData(), size_t(bytes.size())));
    } catch (const toml::parse_error &e) {
        // Keep the last good reading; do not write over a file Tarn cannot read.
        m_loaded.parsed = false;
        reportOnce(QStringLiteral("%1. Presets stay as they were until this is fixed.").arg(parseMessage(e)));
        return m_loaded;
    }
    Loaded loaded;
    loaded.parsed = true;
    QStringList problems;
    if (const toml::table *all = root["preset"].as_table()) {
        for (const auto &[key, node] : *all) {
            const QString name = q(key.str());
            const toml::table *t = node.as_table();
            QString why;
            if (!t)
                why = QStringLiteral("is not a table");
            if (t) {
                if (const auto frame = frameFrom(*t, &why)) {
                    loaded.presets.append({name, *frame});
                    continue;
                }
            }
            problems << QStringLiteral("preset \"%1\" %2").arg(name, why);
        }
    }
    std::sort(loaded.presets.begin(), loaded.presets.end(),
              [](const auto &a, const auto &b) { return QString::compare(a.first, b.first, Qt::CaseInsensitive) < 0; });
    if (const auto autoNode = root["auto"]) {
        if (autoNode.is_string()) {
            const QString name = q(*autoNode.value<std::string_view>());
            const bool known = std::any_of(loaded.presets.begin(), loaded.presets.end(), [&](const auto &p) { return p.first == name; });
            if (known)
                loaded.autoName = name;
            else if (!name.isEmpty())
                problems << QStringLiteral("auto names \"%1\", which is not a preset").arg(name);
        } else {
            problems << QStringLiteral("auto must be a preset's name in quotes");
        }
    }
    m_loaded = std::move(loaded);
    QString message = problems.value(0);
    if (problems.size() > 1)
        message += QStringLiteral(" (and %1 more)").arg(problems.size() - 1);
    reportOnce(message.isEmpty() ? message : message + QStringLiteral(". The rest of the file still applies."));
    return m_loaded;
}

QStringList PresetStore::names() {
    QStringList out;
    for (const auto &p : refresh().presets)
        out << p.first;
    return out;
}

bool PresetStore::contains(const QString &name) { return preset(name).has_value(); }

std::optional<Frame> PresetStore::preset(const QString &name) {
    for (const auto &p : refresh().presets)
        if (p.first == name)
            return p.second;
    return std::nullopt;
}

std::optional<Frame> PresetStore::resolved(const QString &name, bool notify) {
    if (const auto f = preset(name))
        return resolve(*f, notify);
    return std::nullopt;
}

bool PresetStore::isMissing(const QString &name) {
    const auto f = resolved(name);
    if (!f)
        return false;
    const BackgroundKind *k = BackgroundKinds::find(f->background.kind);
    return k && k->missing && k->missing(f->background.params);
}

// Palette gradients and the current wallpaper follow the desktop: this is the one
// place that re-reads them, each time a Preset is applied. It is also where the
// color-stop gradients Tarn had before the image gradients end: a Preset naming one of them, or a
// Palette gradient with no Palette, becomes the image gradient whose average
// color is closest.
Frame PresetStore::resolve(const Frame &frame, bool notify) const {
    Frame out = frame;
    // The Watermark's picture is read from its file now, so a replaced logo reaches the next shot.
    // An unreadable one is skipped (the rest of the Frame still applies), with one notification.
    if (out.watermark.mode == Watermark::Image && out.watermark.image.isNull()) {
        if (out.watermark.path.isEmpty() || !out.watermark.loadImage(out.watermark.path)) {
            if (notify)
                m_daemon.service<Notifier>().notify(QStringLiteral("Watermark skipped"),
                                                    QFileInfo(out.watermark.path).fileName() + QStringLiteral(" is missing or not a picture Tarn can read"));
            out.watermark.mode = Watermark::Off;
        }
    }
    QVariantMap &params = out.background.params;
    auto toImageGradient = [&](const QString &name) {
        out.background.kind = QStringLiteral("image-gradient");
        params = imageGradientParams(name);
    };
    if (out.background.kind == QLatin1String("gradient")) {
        const QVariantList slotNames = params.value(QStringLiteral("slots")).toList();
        const QString id = params.value(QStringLiteral("id")).toString();
        const QVariantList stored = params.value(QStringLiteral("stops")).toList();
        if (slotNames.isEmpty()) {
            // One of the old built-in gradients (a hand-written gradient that names none of them stays as written).
            const QStringList old = legacyGradientStops(id);
            if (!old.isEmpty())
                toImageGradient(closestImageGradient(stored.isEmpty() ? QVariantList(old.begin(), old.end()) : stored));
        } else {
            const Palette &palette = m_daemon.service<Palette>();
            if (palette.fromFile()) {
                QVariantList stops;
                for (int i = 0; i < slotNames.size(); ++i) {
                    QColor c = paletteSlotColor(palette, slotNames[i].toString());
                    if (!c.isValid() && i < stored.size())
                        c = QColor(stored[i].toString());
                    stops << (c.isValid() ? c : QColor(Qt::gray)).name(QColor::HexArgb);
                }
                params.insert(QStringLiteral("stops"), stops);
            } else {
                // No Palette: a known Palette gradient has its own closest image gradient;
                // a hand-written one goes by the colors it was saved with.
                QString name = closestImageGradient(stored);
                for (const auto &g : paletteGradients())
                    if (g.id == id)
                        name = closestImageGradientForLegacy(g.fallbackId);
                toImageGradient(name);
            }
        }
    } else if (out.background.kind == QLatin1String("wallpaper") && params.value(QStringLiteral("current")).toBool()) {
        const QString now = m_daemon.service<Wallpapers>().current();
        if (!now.isEmpty())
            params.insert(QStringLiteral("path"), now);
    }
    return out;
}

QString PresetStore::autoName() { return refresh().autoName; }

std::optional<Frame> PresetStore::autoFrame() {
    const QString name = autoName();
    if (name.isEmpty())
        return std::nullopt;
    return resolved(name, true);
}

// Writes `newBytes` (already checked) atomically. Failure leaves the file as it was.
bool PresetStore::write(const QByteArray &newBytes, const QString &what, QString *error) {
    auto fail = [&](const QString &why) {
        const QString message = QStringLiteral("%1: %2").arg(what, why);
        if (error)
            *error = message;
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not change presets.toml"), message);
        return false;
    };
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
        return fail(QStringLiteral("the folder could not be created"));
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(newBytes) != newBytes.size() || !file.commit())
        return fail(QStringLiteral("the file could not be written"));
    emit changed();
    return true;
}

bool PresetStore::save(const QString &rawName, const Frame &frame, QString *error) {
    const QString name = rawName.trimmed();
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not save the preset"), why);
        return false;
    };
    if (name.isEmpty())
        return fail(QStringLiteral("a preset needs a name"));
    if (!frame.active() && !frame.watermark.on())
        return fail(QStringLiteral("there is no Frame to save"));
    if (frame.active() && !BackgroundKinds::find(frame.background.kind))
        return fail(QStringLiteral("the background kind is unknown"));

    const Loaded &loaded = refresh();
    if (!loaded.parsed)
        return fail(QStringLiteral("presets.toml has a mistake; fix it by hand first"));
    // Saving the same Frame again changes nothing (so a hand-commented preset keeps its comments).
    for (const auto &p : loaded.presets)
        if (p.first == name && p.second == Frame::fromMap(frame.toMap()))
            return true;

    bool existed = false;
    const QByteArray oldBytes = readFile(&existed);
    const QString oldText = existed ? QString::fromUtf8(oldBytes) : kHeader;
    const QString block = blockFor(name, frame);
    const QString newText = editPreset(oldText, name, block);

    // Check: the new text must parse, hold exactly the preset we meant, and
    // leave every other key as it was.
    try {
        toml::table before = toml::parse(oldText.toStdString());
        toml::table after = toml::parse(newText.toStdString());
        const toml::table mine = toml::parse(block.toStdString());
        const toml::node *mineNode = mine["preset"][name.toStdString()].node();
        if (!mineNode)
            return fail(QStringLiteral("internal: the preset text did not read back"));
        if (toml::table *presets = before["preset"].as_table())
            presets->insert_or_assign(name.toStdString(), *mineNode->as_table());
        else
            before.insert_or_assign("preset", toml::table{{name.toStdString(), *mineNode->as_table()}});
        dropEmptyPresets(before);
        dropEmptyPresets(after);
        if (!(before == after))
            return fail(QStringLiteral("presets.toml is written in a form Tarn can't safely edit (dotted keys?); edit it by hand"));
    } catch (const toml::parse_error &) {
        return fail(QStringLiteral("presets.toml is written in a form Tarn can't safely edit; edit it by hand"));
    }
    return write(newText.toUtf8(), QStringLiteral("saving \"%1\"").arg(name), error);
}

bool PresetStore::remove(const QString &name, QString *error) {
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not delete the preset"), why);
        return false;
    };
    const Loaded &loaded = refresh();
    if (!loaded.parsed)
        return fail(QStringLiteral("presets.toml has a mistake; fix it by hand first"));
    bool existed = false;
    const QByteArray oldBytes = readFile(&existed);
    if (!existed)
        return true;
    QString oldText = QString::fromUtf8(oldBytes);
    QString newText = editPreset(oldText, name, QString());
    // The auto-applied Preset can't outlive its preset.
    if (loaded.autoName == name)
        newText = editAuto(newText, QString());
    try {
        toml::table expected = toml::parse(oldText.toStdString());
        if (toml::table *presets = expected["preset"].as_table())
            presets->erase(name.toStdString());
        if (loaded.autoName == name)
            expected.erase("auto");
        toml::table after = toml::parse(newText.toStdString());
        dropEmptyPresets(expected);
        dropEmptyPresets(after);
        if (!(expected == after))
            return fail(QStringLiteral("presets.toml is written in a form Tarn can't safely edit (dotted keys?); edit it by hand"));
    } catch (const toml::parse_error &) {
        return fail(QStringLiteral("presets.toml is written in a form Tarn can't safely edit; edit it by hand"));
    }
    if (newText == oldText)
        return true;
    return write(newText.toUtf8(), QStringLiteral("deleting \"%1\"").arg(name), error);
}

bool PresetStore::setAuto(const QString &name, QString *error) {
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not change presets.toml"), why);
        return false;
    };
    const Loaded &loaded = refresh();
    if (!loaded.parsed)
        return fail(QStringLiteral("presets.toml has a mistake; fix it by hand first"));
    if (!name.isEmpty() && !std::any_of(loaded.presets.begin(), loaded.presets.end(), [&](const auto &p) { return p.first == name; }))
        return fail(QStringLiteral("there is no preset called \"%1\"").arg(name));
    if (loaded.autoName == name)
        return true;
    bool existed = false;
    const QByteArray oldBytes = readFile(&existed);
    if (!existed && name.isEmpty())
        return true;
    const QString oldText = existed ? QString::fromUtf8(oldBytes) : kHeader;
    const QString newText = editAuto(oldText, name);
    try {
        toml::table expected = toml::parse(oldText.toStdString());
        if (name.isEmpty())
            expected.erase("auto");
        else
            expected.insert_or_assign("auto", name.toStdString());
        if (!(expected == toml::parse(newText.toStdString())))
            return fail(QStringLiteral("presets.toml is written in a form Tarn can't safely edit; edit it by hand"));
    } catch (const toml::parse_error &) {
        return fail(QStringLiteral("presets.toml is written in a form Tarn can't safely edit; edit it by hand"));
    }
    return write(newText.toUtf8(), QStringLiteral("marking \"%1\"").arg(name), error);
}

} // namespace tarn
