#include "thumbnail_surface.h"

#include "palette/palette.h"
#include "theme.h"
#include "thumbnails/thumbnail_stack.h"

#include <LayerShellQt/Window>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QScreen>

namespace tarn {

namespace {
// image://capture/<id>: the stack's decoded image for that Thumbnail.
class CaptureImageProvider : public QQuickImageProvider {
public:
    explicit CaptureImageProvider(ThumbnailStack &stack) : QQuickImageProvider(QQuickImageProvider::Image), m_stack(stack) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override {
        const QImage image = m_stack.image(id.section('/', 0, 0).toInt());
        *size = image.size();
        return image;
    }

private:
    ThumbnailStack &m_stack;
};
} // namespace

ThumbnailSurface::ThumbnailSurface(ThumbnailStack &stack, QObject *parent)
    : ThumbnailSurface(stack, *new Palette(QString()), parent) {
    // The Palette made just above belongs to this surface.
    qobject_cast<Palette *>(m_view.rootContext()->contextProperty(QStringLiteral("theme")).value<QObject *>())->setParent(this);
}

ThumbnailSurface::ThumbnailSurface(ThumbnailStack &stack, Palette &palette, QObject *parent) : QObject(parent), m_stack(stack) {
    m_view.setColor(Qt::transparent);
    m_view.setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    m_view.setResizeMode(QQuickView::SizeViewToRootObject);

    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        // Set before the window is first shown.
        auto *shell = LayerShellQt::Window::get(&m_view);
        shell->setScope(QStringLiteral("tarn-thumbnail")); // must start with "tarn" for the layer rule
        shell->setLayer(LayerShellQt::Window::LayerOverlay);
        shell->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft));
        shell->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        shell->setExclusiveZone(0);
        shell->setMargins(QMargins()); // the 16 px edge is inside the QML, so cards can slide off the screen edge
    }

    auto *engine = m_view.engine();
    engine->addImageProvider(QStringLiteral("capture"), new CaptureImageProvider(stack));
    installTheme(m_view.rootContext(), palette);
    m_view.rootContext()->setContextProperty(QStringLiteral("thumbnails"), &stack);
    m_view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/Tarn/Ui/qml/Thumbnails.qml")));

    // On screen while the QML still shows a card, which outlasts the stack by the exit animation.
    QQuickItem *root = m_view.rootObject();
    if (root) {
        connect(root, SIGNAL(cardCountChanged()), this, SLOT(sync()));
        connect(root, SIGNAL(inputRectChanged()), this, SLOT(updateMask()));
    }
    connect(&stack, &ThumbnailStack::monitorChanged, this, &ThumbnailSurface::follow);
    connect(&stack, &ThumbnailStack::captureHiddenChanged, this, &ThumbnailSurface::sync);
    sync();
}

// The layer surface is bound to one output when it is created, and a window hidden and shown
// again falls back to the first screen unless it is told again. So the screen is set before
// every show, and a surface already up is taken down and shown again to move it.
void ThumbnailSurface::applyScreen() {
    for (QScreen *screen : QGuiApplication::screens())
        if (screen->name() == m_stack.monitorName()) {
            m_view.setScreen(screen);
            break;
        }
}

void ThumbnailSurface::follow() {
    const bool wasVisible = m_view.isVisible();
    if (wasVisible)
        m_view.hide();
    applyScreen();
    if (wasVisible)
        m_view.show();
}

void ThumbnailSurface::sync() {
    QQuickItem *root = m_view.rootObject();
    const bool wanted = root && root->property("cardCount").toInt() > 0 && !m_stack.captureHidden();
    if (wanted && !m_view.isVisible()) {
        updateMask();
        applyScreen();
        m_view.show();
    } else if (!wanted && m_view.isVisible())
        m_view.hide();
}

// Only the live cards take the pointer; the rest of the surface (the edge strip,
// cards still sliding out) lets it through to the windows below.
void ThumbnailSurface::updateMask() {
    QQuickItem *root = m_view.rootObject();
    if (root && QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        m_view.setMask(QRegion(root->property("inputRect").toRect()));
}

} // namespace tarn
