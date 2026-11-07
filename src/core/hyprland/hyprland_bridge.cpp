#include "hyprland_bridge.h"

#include "daemon/daemon.h"
#include "process/process_runner.h"

#include <QJsonArray>
#include <algorithm>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QJsonObject>

namespace tarn {

// The two queries several callers need, each in one place.
std::optional<QJsonArray> HyprlandBridge::queryMonitors(QString *error) {
    const auto result = m_daemon.service<ProcessRunner>().run(QStringLiteral("hyprctl"), {QStringLiteral("monitors"), QStringLiteral("-j")});
    if (!result.succeeded()) {
        *error = result.describe(QStringLiteral("hyprctl monitors"));
        return std::nullopt;
    }
    return QJsonDocument::fromJson(result.out).array();
}

std::optional<QJsonArray> HyprlandBridge::queryClients(QString *error) {
    const auto result = m_daemon.service<ProcessRunner>().run(QStringLiteral("hyprctl"), {QStringLiteral("clients"), QStringLiteral("-j")});
    if (!result.succeeded()) {
        *error = result.describe(QStringLiteral("hyprctl clients"));
        return std::nullopt;
    }
    return QJsonDocument::fromJson(result.out).array();
}

std::optional<QJsonObject> HyprlandBridge::clientTitled(const QString &title, QString *error) {
    const auto clients = queryClients(error);
    if (!clients)
        return std::nullopt;
    for (const auto &value : *clients) {
        const auto c = value.toObject();
        if (c.value("title").toString() == title)
            return c;
    }
    *error = QStringLiteral("Hyprland has no window called %1").arg(title);
    return std::nullopt;
}

std::optional<QPointF> HyprlandBridge::queryCursor(QString *error) {
    const auto result = m_daemon.service<ProcessRunner>().run(QStringLiteral("hyprctl"), {QStringLiteral("cursorpos"), QStringLiteral("-j")});
    if (!result.succeeded()) {
        *error = result.describe(QStringLiteral("hyprctl cursorpos"));
        return std::nullopt;
    }
    const auto pos = QJsonDocument::fromJson(result.out).object();
    if (!pos.contains("x") || !pos.contains("y")) {
        *error = QStringLiteral("hyprctl cursorpos gave an answer Tarn does not understand");
        return std::nullopt;
    }
    return QPointF(pos.value("x").toDouble(), pos.value("y").toDouble());
}

// One entry of `monitors -j`: its exact logical rectangle (size = pixels / scale).
static QRectF monitorRect(const QJsonObject &m) {
    double w = m.value("width").toDouble();
    double h = m.value("height").toDouble();
    const double scale = m.value("scale").toDouble(1.0);
    // transform 1, 3, 5 and 7 are the rotated-sideways ones.
    if (m.value("transform").toInt() % 2 == 1)
        std::swap(w, h);
    return QRectF(m.value("x").toDouble(), m.value("y").toDouble(), w / scale, h / scale);
}

static MonitorInfo monitorFrom(const QJsonObject &m) {
    // `reserved` is [left, top, right, bottom]; a monitor that reports nothing counts as 0.
    const int reservedTop = m.value("reserved").toArray().at(1).toInt(0);
    return MonitorInfo{m.value("name").toString(), monitorRect(m).toRect(), m.value("scale").toDouble(1.0), reservedTop};
}

QList<MonitorInfo> HyprlandBridge::monitors(QString *error) {
    const auto array = queryMonitors(error);
    QList<MonitorInfo> out;
    if (array)
        for (const auto &value : *array)
            out << monitorFrom(value.toObject());
    return out;
}

std::optional<MonitorInfo> HyprlandBridge::monitorContaining(const QPoint &point, QString *error) {
    std::optional<MonitorInfo> found;
    for (const auto &m : monitors(error))
        if (m.logicalRect.contains(point))
            found = m;
    return found;
}

// Lua string literal for text we put into `hyprctl eval`.
static QString lua(const QString &s) {
    QString out = QStringLiteral("\"");
    for (const QChar c : s) {
        if (c == QLatin1Char('"') || c == QLatin1Char('\\'))
            out += QLatin1Char('\\');
        out += c;
    }
    return out + QLatin1Char('"');
}

// A window title as an anchored regex (only the metacharacters are escaped: RE2
// rejects a backslash before a space).
static QString titleRegex(const QString &title) {
    QString out = QStringLiteral("^");
    for (const QChar c : title) {
        if (QStringLiteral(".^$|()[]{}*+?\\").contains(c))
            out += QLatin1Char('\\');
        out += c;
    }
    return out + QLatin1Char('$');
}

bool HyprlandBridge::openWindowAt(const QString &title, const QRect &r, QString *error) {
    // Rule positions are local to the monitor the window opens on.
    const auto monitor = monitorContaining(r.center(), error);
    const QPoint origin = monitor ? monitor->logicalRect.topLeft() : QPoint();
    // Hyprland 0.56's Lua has no way to delete a window rule (a rule handle only has
    // set_enabled), so rules must not pile up, one per Pin. A small fixed pool of
    // names is reused round-robin; the rule that last held a slot is disabled
    // first (through a handle kept in a Lua global, guarded with pcall in case the
    // global did not survive), and reusing a name is meant to replace it. A Pin's
    // rule only matters at its first map, so retiring it when its slot comes round
    // again costs nothing.
    const int slot = m_ruleSlot++ % kPinRuleSlots;
    // Values are strings, like the prototype's rules (Hyprland converts them itself).
    const QString code =
        QStringLiteral("_G.tarn_pin_rules = _G.tarn_pin_rules or {}; "
                       "local old = _G.tarn_pin_rules[%1]; if old then pcall(function() old:set_enabled(false) end) end; "
                       "_G.tarn_pin_rules[%1] = hl.window_rule({name=%2, match={title=%3}, move={%4, %5}, size={%6, %7}})")
            .arg(slot)
            .arg(lua(QStringLiteral("tarn-pin-slot-") + QString::number(slot)), lua(titleRegex(title)))
            .arg(lua(QString::number(r.x() - origin.x())), lua(QString::number(r.y() - origin.y())))
            .arg(lua(QString::number(r.width())), lua(QString::number(r.height())));
    const auto result = m_daemon.service<ProcessRunner>().run(QStringLiteral("hyprctl"), {QStringLiteral("eval"), code});
    if (!result.succeeded()) {
        *error = result.describe(QStringLiteral("hyprctl eval"));
        return false;
    }
    return true;
}

std::optional<QRect> HyprlandBridge::windowRect(const QString &title, QString *error) {
    const auto c = clientTitled(title, error);
    if (!c)
        return std::nullopt;
    const auto at = c->value("at").toArray(), size = c->value("size").toArray();
    return QRect(at.at(0).toInt(), at.at(1).toInt(), size.at(0).toInt(), size.at(1).toInt());
}

bool HyprlandBridge::setWindowRect(const QString &title, const QRect &r, QString *error) {
    const QString who = lua(QStringLiteral("title:") + titleRegex(title));
    const QString code =
        QStringLiteral("hl.dispatch(hl.dsp.window.resize({window=%1, x=%2, y=%3, relative=false})); "
                       "hl.dispatch(hl.dsp.window.move({window=%1, x=%4, y=%5, relative=false}))")
            .arg(who).arg(r.width()).arg(r.height()).arg(r.x()).arg(r.y());
    const auto result = m_daemon.service<ProcessRunner>().run(QStringLiteral("hyprctl"), {QStringLiteral("eval"), code});
    if (!result.succeeded()) {
        *error = result.describe(QStringLiteral("hyprctl eval"));
        return false;
    }
    return true;
}

bool HyprlandBridge::setWindowNoFocus(const QString &title, bool noFocus, QString *error) {
    const auto client = clientTitled(title, error);
    if (!client)
        return false;
    const QString address = client->value("address").toString();
    if (address.isEmpty()) {
        *error = QStringLiteral("Hyprland has no window called %1").arg(title);
        return false;
    }
    const QString code = QStringLiteral("hl.dispatch(hl.dsp.window.set_prop({window=%1, prop=\"no_focus\", value=%2}))")
                             .arg(lua(QStringLiteral("address:") + address), lua(noFocus ? QStringLiteral("1") : QStringLiteral("0")));
    const auto result = m_daemon.service<ProcessRunner>().run(QStringLiteral("hyprctl"), {QStringLiteral("eval"), code});
    if (!result.succeeded()) {
        *error = result.describe(QStringLiteral("hyprctl eval"));
        return false;
    }
    return true;
}

std::optional<MonitorInfo> HyprlandBridge::monitorUnderPointer(QString *error) {
    const auto pointer = queryCursor(error);
    if (!pointer)
        return std::nullopt;
    const auto array = queryMonitors(error);
    if (!array)
        return std::nullopt;
    for (const auto &value : *array) {
        const auto m = value.toObject();
        if (monitorRect(m).contains(*pointer))
            return monitorFrom(m);
    }
    *error = QStringLiteral("the pointer is not on any monitor Hyprland lists");
    return std::nullopt;
}

bool HyprlandBridge::isWindowFocused(const QString &title) {
    const auto result = m_daemon.service<ProcessRunner>().run(QStringLiteral("hyprctl"), {QStringLiteral("activewindow"), QStringLiteral("-j")});
    return result.succeeded() && QJsonDocument::fromJson(result.out).object().value("title").toString() == title;
}

bool HyprlandBridge::focusWindow(const QString &title, QString *error) {
    const QString code = QStringLiteral("hl.dispatch(hl.dsp.focus({window=%1}))").arg(lua(QStringLiteral("title:") + titleRegex(title)));
    const auto result = m_daemon.service<ProcessRunner>().run(QStringLiteral("hyprctl"), {QStringLiteral("eval"), code});
    if (!result.succeeded()) {
        *error = result.describe(QStringLiteral("hyprctl eval"));
        return false;
    }
    return true;
}

bool HyprlandBridge::warpPointer(const QPoint &p, QString *error) {
    // Hyprland 0.56 reads `hyprctl dispatch` arguments as Lua, so the old
    // `dispatch movecursor X Y` no longer parses; this is the Lua form.
    const QString code = QStringLiteral("hl.dispatch(hl.dsp.cursor.move({x=%1, y=%2}))").arg(p.x()).arg(p.y());
    const auto result = m_daemon.service<ProcessRunner>().run(QStringLiteral("hyprctl"), {QStringLiteral("eval"), code});
    if (!result.succeeded()) {
        *error = result.describe(QStringLiteral("hyprctl eval"));
        return false;
    }
    return true;
}

// Linux evdev button codes, as Hyprland names them: mouse:272 left, 273 right, 274 middle.
static const struct { const char *key; const char *name; } kClickButtons[] = {{"mouse:272", "left"}, {"mouse:274", "middle"}, {"mouse:273", "right"}};

bool HyprlandBridge::addClickBinds(const QString &command, QString *error) {
    QStringList lines;
    for (const auto &b : kClickButtons)
        lines << QStringLiteral("hl.bind(%1, hl.dsp.exec_cmd(%2), {non_consuming=true})")
                     .arg(lua(QLatin1String(b.key)), lua(command + QLatin1Char(' ') + QLatin1String(b.name)));
    const auto result = m_daemon.service<ProcessRunner>().run(QStringLiteral("hyprctl"), {QStringLiteral("eval"), lines.join(QStringLiteral("; "))});
    if (!result.succeeded()) {
        *error = result.describe(QStringLiteral("hyprctl eval"));
        return false;
    }
    return true;
}

bool HyprlandBridge::removeClickBinds(QString *error) {
    QStringList lines;
    for (const auto &b : kClickButtons)
        lines << QStringLiteral("hl.unbind(%1)").arg(lua(QLatin1String(b.key)));
    const auto result = m_daemon.service<ProcessRunner>().run(QStringLiteral("hyprctl"), {QStringLiteral("eval"), lines.join(QStringLiteral("; "))});
    if (!result.succeeded()) {
        *error = result.describe(QStringLiteral("hyprctl eval"));
        return false;
    }
    return true;
}

std::optional<QPointF> HyprlandBridge::pointerPosition(QString *error) {
    return queryCursor(error);
}

bool HyprlandBridge::softwarePointer() {
    const auto option = m_daemon.service<ProcessRunner>().run(
        QStringLiteral("hyprctl"), {QStringLiteral("getoption"), QStringLiteral("cursor:no_hardware_cursors"), QStringLiteral("-j")});
    if (!option.succeeded())
        return false;
    // 0 hardware, 1 software, 2 auto (software on this NVIDIA card).
    return QJsonDocument::fromJson(option.out).object().value("int").toInt() != 0;
}

QList<WindowInfo> HyprlandBridge::windows(const MonitorInfo &monitor, QString *error) {
    auto &runner = m_daemon.service<ProcessRunner>();

    const auto monitorList = queryMonitors(error);
    if (!monitorList)
        return {};
    int workspace = 0, special = 0;
    for (const auto &value : *monitorList) {
        const auto m = value.toObject();
        if (m.value("name").toString() != monitor.name)
            continue;
        workspace = m.value("activeWorkspace").toObject().value("id").toInt();
        special = m.value("specialWorkspace").toObject().value("id").toInt();
    }

    const auto clients = queryClients(error);
    if (!clients)
        return {};
    // Rounding is a global option; if it cannot be read the corners stay square.
    qreal rounding = 0;
    const auto option = runner.run(QStringLiteral("hyprctl"), {QStringLiteral("getoption"), QStringLiteral("decoration:rounding"), QStringLiteral("-j")});
    if (option.succeeded())
        rounding = QJsonDocument::fromJson(option.out).object().value("int").toDouble();

    struct Ranked {
        int rank;
        int focus;
        WindowInfo window;
    };
    QList<Ranked> found;
    for (const auto &value : *clients) {
        const auto c = value.toObject();
        if (!c.value("mapped").toBool(true) || c.value("hidden").toBool(false))
            continue;
        const int ws = c.value("workspace").toObject().value("id").toInt();
        const bool onSpecial = special != 0 && ws == special;
        if (!onSpecial && !(ws == workspace || c.value("pinned").toBool(false)))
            continue;
        const auto at = c.value("at").toArray(), size = c.value("size").toArray();
        if (at.size() < 2 || size.size() < 2)
            continue;
        const QRect global(at[0].toInt(), at[1].toInt(), size[0].toInt(), size[1].toInt());
        const QRect local = global.translated(-monitor.logicalRect.topLeft()).intersected(QRect(QPoint(), monitor.logicalRect.size()));
        if (local.isEmpty())
            continue;
        const bool fullscreen = c.value("fullscreen").toInt() != 0;
        const int rank = onSpecial ? 0 : fullscreen ? 1 : c.value("floating").toBool() ? 2 : 3;
        found.append({rank, c.value("focusHistoryID").toInt(), {local, c.value("class").toString(), c.value("title").toString(), fullscreen ? 0 : rounding}});
    }
    std::stable_sort(found.begin(), found.end(), [](const Ranked &a, const Ranked &b) {
        return a.rank != b.rank ? a.rank < b.rank : a.focus < b.focus;
    });
    QList<WindowInfo> out;
    for (const auto &r : found)
        out.append(r.window);
    return out;
}

void HyprlandBridge::takeSnapshot() {
    m_snapshot.clear();
    m_focused.reset();
    auto &runner = m_daemon.service<ProcessRunner>();
    QString error;
    const auto monitorList = queryMonitors(&error);
    const auto clients = queryClients(&error);
    if (!monitorList || !clients)
        return;

    struct Screen {
        QRect rect;
        int workspace, special;
    };
    QList<Screen> screens;
    for (const auto &value : *monitorList) {
        const auto m = value.toObject();
        screens.append({monitorRect(m).toRect(), m.value("activeWorkspace").toObject().value("id").toInt(),
                        m.value("specialWorkspace").toObject().value("id").toInt()});
    }

    struct Ranked {
        int rank;
        int index;
        QString address;
        WindowInfo window;
    };
    QList<Ranked> found;
    int index = 0;
    for (const auto &value : *clients) {
        const auto c = value.toObject();
        ++index;
        if (!c.value("mapped").toBool(true) || c.value("hidden").toBool(false))
            continue;
        const auto at = c.value("at").toArray(), size = c.value("size").toArray();
        if (at.size() < 2 || size.size() < 2)
            continue;
        const QRect rect(at[0].toInt(), at[1].toInt(), size[0].toInt(), size[1].toInt());
        // The monitor a window is on is the one holding its center.
        const Screen *screen = nullptr;
        for (const auto &s : screens)
            if (s.rect.contains(rect.center()))
                screen = &s;
        if (!screen)
            continue;
        const int ws = c.value("workspace").toObject().value("id").toInt();
        const bool onSpecial = screen->special != 0 && ws == screen->special;
        if (!onSpecial && ws != screen->workspace && !c.value("pinned").toBool(false))
            continue;
        const bool fullscreen = c.value("fullscreen").toInt() != 0;
        const int rank = onSpecial ? 0 : fullscreen ? 1 : c.value("floating").toBool() ? 2 : 3;
        found.append({rank, index, c.value("address").toString(), {rect, c.value("class").toString(), c.value("title").toString(), 0}});
    }
    // Front first: by group, then the later in Hyprland's list the higher.
    std::stable_sort(found.begin(), found.end(), [](const Ranked &a, const Ranked &b) {
        return a.rank != b.rank ? a.rank < b.rank : a.index > b.index;
    });

    const auto active = runner.run(QStringLiteral("hyprctl"), {QStringLiteral("activewindow"), QStringLiteral("-j")});
    const QString focusedAddress = active.succeeded() ? QJsonDocument::fromJson(active.out).object().value("address").toString() : QString();
    for (const auto &r : found) {
        m_snapshot.append(r.window);
        if (!focusedAddress.isEmpty() && r.address == focusedAddress)
            m_focused = r.window;
    }
}

std::optional<WindowInfo> HyprlandBridge::windowOverlapping(const QRect &rect) const {
    QRegion covered; // what the windows in front already hide
    const WindowInfo *best = nullptr;
    qint64 bestArea = 0;
    for (const auto &w : m_snapshot) {
        const QRegion visible = QRegion(w.rect.intersected(rect)).subtracted(covered);
        qint64 area = 0;
        for (const QRect &part : visible)
            area += qint64(part.width()) * part.height();
        if (area > bestArea) {
            bestArea = area;
            best = &w;
        }
        covered += w.rect;
    }
    return best ? std::optional<WindowInfo>(*best) : std::nullopt;
}

} // namespace tarn
