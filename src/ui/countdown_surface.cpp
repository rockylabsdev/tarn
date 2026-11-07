#include "countdown_surface.h"

#include "palette/palette.h"
#include "theme.h"
#include "timer/countdown.h"

#include <LayerShellQt/Window>
#include <QGuiApplication>
#include <QQmlContext>
#include <QScreen>
#include <QTimer>

namespace tarn {

namespace {
// Hyprland needs this long after a surface unmaps before grim no longer sees it.
constexpr int kOffScreenDelayMs = 80;
} // namespace

QRect CountdownSurface::pillRect(const QSize &monitor, int top) {
    return QRect(QPoint((monitor.width() - kWidth) / 2, top), QSize(kWidth, kHeight));
}

CountdownSurface::CountdownSurface(Countdown &countdown, Palette &palette, QObject *parent)
    : QObject(parent), m_countdown(countdown) {
    m_view.setColor(Qt::transparent);
    m_view.setFlags(Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    m_view.setResizeMode(QQuickView::SizeRootObjectToView);

    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        auto *shell = LayerShellQt::Window::get(&m_view);
        shell->setScope(QStringLiteral("tarn-countdown")); // "tarn..." gets the no-animation layer rule
        shell->setLayer(LayerShellQt::Window::LayerOverlay);
        shell->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        shell->setExclusiveZone(-1);
        shell->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    }

    installTheme(m_view.rootContext(), palette);
    m_view.rootContext()->setContextProperty(QStringLiteral("countdown"), &countdown);
    m_view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/Tarn/Ui/qml/Countdown.qml")));

    countdown.attachSurface();
    connect(&countdown, &Countdown::changed, this, [this] {
        if (m_countdown.active() && !m_view.isVisible())
            show();
    });
    connect(&countdown, &Countdown::hidden, this, &CountdownSurface::hide);
}

void CountdownSurface::show() {
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen->name() == m_countdown.monitorName()) {
            m_view.setScreen(screen);
            break;
        }
    }
    // The whole monitor. Under layer-shell the four anchors size it; elsewhere it is placed.
    m_view.resize(m_countdown.monitorRect().size());
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        m_view.setPosition(m_countdown.monitorRect().topLeft());
    m_view.show();
}

void CountdownSurface::hide() {
    m_view.hide();
    // Tell the Countdown when grim can safely grab again.
    QTimer::singleShot(kOffScreenDelayMs, this, [this] { m_countdown.surfaceGone(); });
}

} // namespace tarn
