#include "recording_surface.h"

#include "palette/palette.h"
#include "recording/recording_session.h"
#include "theme.h"

#include <LayerShellQt/Window>
#include <QGuiApplication>
#include <QQmlContext>
#include <QRegion>
#include <QScreen>

namespace tarn {

RecordingSurface::RecordingSurface(RecordingSession &session, Palette &palette, QObject *parent)
    : QObject(parent), m_session(session) {
    m_view.setColor(Qt::transparent);
    m_view.setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    m_view.setResizeMode(QQuickView::SizeRootObjectToView);

    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        auto *shell = LayerShellQt::Window::get(&m_view);
        shell->setScope(QStringLiteral("tarn-recording"));
        shell->setLayer(LayerShellQt::Window::LayerOverlay);
        shell->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
                                                       LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        shell->setExclusiveZone(-1); // same coordinates as the Overlay: the whole monitor, any bar included
        shell->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    }

    installTheme(m_view.rootContext(), palette);
    m_view.rootContext()->setContextProperty(QStringLiteral("recording"), &session);
    m_view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/Tarn/Ui/qml/Recording.qml")));

    connect(&session, &RecordingSession::stateChanged, this, &RecordingSurface::sync);
    connect(&session, &RecordingSession::barRectChanged, this, &RecordingSurface::updateMask);
    connect(&session, &RecordingSession::barVisibleChanged, this, &RecordingSurface::updateMask);
    connect(&session, &RecordingSession::stripChanged, this, &RecordingSurface::updateMask);
    sync();
}

void RecordingSurface::sync() {
    if (m_session.active() && !m_view.isVisible()) {
        for (QScreen *screen : QGuiApplication::screens()) {
            if (screen->name() == m_session.monitorName()) {
                m_view.setScreen(screen);
                break;
            }
        }
        m_view.resize(m_session.monitorSize());
        updateMask();
        m_view.show();
    } else if (!m_session.active() && m_view.isVisible()) {
        m_view.hide();
    }
    updateMask(); // the bar appears (or goes) with the state
}

// Only the bar takes the pointer; the rest of the monitor is click-through.
void RecordingSurface::updateMask() {
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return;
    // The bar when it shows, and the strip of an auto-hiding bar. An empty
    // region would mean "no mask"; one pixel at the corner is as good as none.
    QRegion region;
    if (m_session.hasBar() && m_session.barVisible())
        region += m_session.barRect();
    region += m_session.stripRect();
    m_view.setMask(region.isEmpty() ? QRegion(QRect(0, 0, 1, 1)) : region);
}

} // namespace tarn
