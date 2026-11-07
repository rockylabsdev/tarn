#include "overlay_surface.h"

#include "overlay/overlay.h"
#include "palette/palette.h"
#include "theme.h"

#include <LayerShellQt/Window>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QScreen>
#include <QTimer>

namespace tarn {

namespace {
// image://frozen/<generation>: grim's full-resolution image of the monitor.
class FrozenImageProvider : public QQuickImageProvider {
public:
    explicit FrozenImageProvider(Overlay &overlay) : QQuickImageProvider(QQuickImageProvider::Image), m_overlay(overlay) {}
    QImage requestImage(const QString &, QSize *size, const QSize &) override {
        QImage image = m_overlay.frozen();
        if (image.isNull()) {
            image = QImage(1, 1, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
        }
        *size = image.size();
        return image;
    }

private:
    Overlay &m_overlay;
};

// Hyprland needs this long after a surface unmaps before grim no longer sees it.
constexpr int kOffScreenDelayMs = 80;
} // namespace

OverlaySurface::OverlaySurface(Overlay &overlay, Palette &palette, QObject *parent) : QObject(parent), m_overlay(overlay) {
    m_view.setColor(Qt::transparent);
    m_view.setFlags(Qt::FramelessWindowHint);
    m_view.setResizeMode(QQuickView::SizeRootObjectToView);

    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        auto *shell = LayerShellQt::Window::get(&m_view);
        shell->setScope(QStringLiteral("tarn-overlay")); // must start with "tarn" for the no-animation layer rule
        shell->setLayer(LayerShellQt::Window::LayerOverlay);
        shell->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
                                                       LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        shell->setExclusiveZone(-1); // cover everything, any bar included
        shell->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
    }

    m_view.engine()->addImageProvider(QStringLiteral("frozen"), new FrozenImageProvider(overlay));
    installTheme(m_view.rootContext(), palette);
    m_view.rootContext()->setContextProperty(QStringLiteral("overlay"), &overlay);
    m_view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/Tarn/Ui/qml/Overlay.qml")));

    overlay.attachSurface();
    connect(&overlay, &Overlay::opened, this, &OverlaySurface::show);
    connect(&overlay, &Overlay::closed, this, &OverlaySurface::hide);
}

void OverlaySurface::setInteractive(bool on) {
    m_view.setFlag(Qt::WindowTransparentForInput, !on);
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        LayerShellQt::Window::get(&m_view)->setKeyboardInteractivity(on ? LayerShellQt::Window::KeyboardInteractivityExclusive
                                                                        : LayerShellQt::Window::KeyboardInteractivityNone);
}

void OverlaySurface::warmUp() {
    if (m_warmedUp || m_warming || m_view.isVisible())
        return;
    m_warming = true;
    setInteractive(false);
    // Nothing is drawn: the model is not active, so the QML draws nothing.
    connect(&m_view, &QQuickWindow::frameSwapped, this,
            [this] {
                if (!m_warming)
                    return;
                m_warming = false;
                m_warmedUp = true;
                QTimer::singleShot(0, this, [this] {
                    if (m_overlay.isOpen())
                        return; // a real open came in during the warm-up: keep it
                    m_view.hide();
                    setInteractive(true);
                });
            },
            Qt::QueuedConnection);
    m_view.resize(64, 64);
    m_view.show();
}

void OverlaySurface::show() {
    const QRect rect = m_overlay.monitorRect();
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen->name() == m_overlay.monitorName()) {
            m_view.setScreen(screen);
            break;
        }
    }
    if (m_warming) { // opened mid warm-up: the surface is already mapped, just make it live
        m_warming = false;
        m_warmedUp = true;
        setInteractive(true);
    }
    m_view.resize(rect.size());
    if (!m_view.isVisible())
        m_view.show();
    m_view.requestActivate();
    if (m_view.rootObject())
        m_view.rootObject()->forceActiveFocus();
}

void OverlaySurface::hide() {
    m_view.hide();
    // Tell the Overlay when grim can safely grab again.
    QTimer::singleShot(kOffScreenDelayMs, this, [this] { m_overlay.surfaceGone(); });
}

} // namespace tarn
