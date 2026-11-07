#include "ripple_surface.h"

#include "palette/palette.h"
#include "recording/click_ripples.h"
#include "recording/recording_session.h"
#include "theme.h"

#include <LayerShellQt/Window>
#include <QGuiApplication>
#include <QQmlContext>
#include <QScreen>

namespace tarn {

RippleSurface::RippleSurface(ClickRipples &ripples, RecordingSession &session, Palette &palette, QObject *parent)
    : QObject(parent), m_ripples(ripples), m_session(session) {
    m_view.setColor(Qt::transparent);
    m_view.setFlags(Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    m_view.setResizeMode(QQuickView::SizeRootObjectToView);

    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        auto *shell = LayerShellQt::Window::get(&m_view);
        shell->setScope(QStringLiteral("tarn-ripples"));
        shell->setLayer(LayerShellQt::Window::LayerOverlay);
        shell->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
                                                       LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        shell->setExclusiveZone(-1); // the Recording's coordinates: the whole monitor
        shell->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    }

    installTheme(m_view.rootContext(), palette);
    m_view.rootContext()->setContextProperty(QStringLiteral("ripples"), &ripples);
    m_view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/Tarn/Ui/qml/Ripples.qml")));

    connect(&ripples, &ClickRipples::activeChanged, this, &RippleSurface::sync);
    sync();
}

void RippleSurface::sync() {
    if (m_ripples.active() && !m_view.isVisible()) {
        for (QScreen *screen : QGuiApplication::screens()) {
            if (screen->name() == m_session.monitorName()) {
                m_view.setScreen(screen);
                break;
            }
        }
        m_view.resize(m_session.monitorSize());
        m_view.show();
    } else if (!m_ripples.active() && m_view.isVisible()) {
        m_view.hide();
    }
}

} // namespace tarn
