#include "history_window.h"

#include "daemon/daemon.h"
#include "editor/opening_size.h"
#include "hyprland/hyprland_bridge.h"

namespace tarn {

HistoryWindow::HistoryWindow(Daemon &daemon) : m_daemon(daemon), m_model(daemon, this) {}

QSize HistoryWindow::openingSize() {
    constexpr QSize usual{1200, 780};
    constexpr int margin = 80;
    const QSize monitor = monitorSizeUnderPointer(m_daemon).size;
    return {qMin(usual.width(), monitor.width() - margin), qMin(usual.height(), monitor.height() - margin)};
}

void HistoryWindow::open() {
    if (m_open)
        return;
    m_model.setSearch({}); // the search is empty each time the window opens
    m_model.reload();
    m_model.clearSelection();
    m_open = true;
    emit opened();
}

void HistoryWindow::close() {
    if (!m_open)
        return;
    m_open = false;
    emit closed();
}

void HistoryWindow::escape() {
    if (!m_model.search().isEmpty())
        m_model.setSearch({});
    else
        close();
}

void HistoryWindow::focus() {
    if (!m_open)
        return;
    QString error;
    m_daemon.service<HyprlandBridge>().focusWindow(QString::fromLatin1(kTitle), &error);
    emit focusRequested();
}

void HistoryWindow::toggle() {
    if (!m_open) {
        open();
        return;
    }
    auto &hyprland = m_daemon.service<HyprlandBridge>();
    if (hyprland.isWindowFocused(QString::fromLatin1(kTitle))) {
        close();
    } else {
        QString error;
        hyprland.focusWindow(QString::fromLatin1(kTitle), &error);
    }
}

TARN_MODULE(history_window, [](Daemon &daemon) {
    daemon.addCommand(QStringLiteral("history"), [&daemon](const QStringList &) {
        daemon.service<HistoryWindow>().toggle();
        return Reply::success();
    });
});

} // namespace tarn
