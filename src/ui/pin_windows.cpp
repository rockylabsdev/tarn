#include "pin_windows.h"

#include "palette/palette.h"
#include "pin/pins.h"
#include "theme.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>

// Registers the module's C++ QML types. The module is a static library, so
// nothing else would pull the generated registration in.
void qml_register_types_Tarn_Ui();

namespace tarn {

namespace {
// image://pin/<id>: the Pin's image (the edited one, if it was edited). Asked for a
// smaller size (a shrunk Pin), it resamples smoothly here: GPU mipmap filtering
// leaves a shrunk Pin soft. Never enlarged: a bigger Pin scales the full picture.
class PinImageProvider : public QQuickImageProvider {
public:
    explicit PinImageProvider(Pins &pins) : QQuickImageProvider(QQuickImageProvider::Image), m_pins(pins) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override {
        QImage image = m_pins.image(id.section('/', 0, 0).toInt());
        if (requested.isValid() && requested.width() < image.width() && requested.height() < image.height())
            image = image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        *size = image.size();
        return image;
    }

private:
    Pins &m_pins;
};
} // namespace

PinWindow::PinWindow(Pins &pins, Palette &palette, int pin, QObject *parent) : QObject(parent) {
    static const bool typesRegistered = (qml_register_types_Tarn_Ui(), true);
    Q_UNUSED(typesRegistered);
    m_view.setTitle(Pins::title(pin));
    m_view.setFlags(Qt::Window | Qt::FramelessWindowHint);
    m_view.setColor(Qt::transparent);
    m_view.setResizeMode(QQuickView::SizeRootObjectToView);
    // The opening size, before the first show: Hyprland ignores later self-resizes.
    m_view.resize(pins.openingRect(pin).size());

    m_view.engine()->addImageProvider(QStringLiteral("pin"), new PinImageProvider(pins));
    installTheme(m_view.rootContext(), palette);
    m_view.rootContext()->setContextProperty(QStringLiteral("pins"), &pins);
    m_view.rootContext()->setContextProperty(QStringLiteral("pinId"), pin);
    m_view.rootContext()->setContextProperty(QStringLiteral("win"), this);
    const auto sizeChanged = [&pins, pin] { pins.windowResized(pin); };
    QObject::connect(&m_view, &QWindow::widthChanged, this, sizeChanged);
    QObject::connect(&m_view, &QWindow::heightChanged, this, sizeChanged);
    m_view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/Tarn/Ui/qml/Pin.qml")));
}

PinWindows::PinWindows(Pins &pins, Palette &palette, QObject *parent) : QObject(parent), m_pins(pins), m_palette(palette) {
    connect(&pins, &Pins::opened, this, &PinWindows::onOpened);
    connect(&pins, &Pins::closed, this, [this](int pin) {
        if (PinWindow *window = m_windows.take(pin)) {
            window->view().hide();
            window->deleteLater();
        }
    });
}

QQuickView *PinWindows::viewFor(int pin) const {
    const auto it = m_windows.find(pin);
    return it == m_windows.end() || !it.value() ? nullptr : &it.value()->view();
}

void PinWindows::onOpened(int pin) {
    auto *window = new PinWindow(m_pins, m_palette, pin, this);
    m_windows[pin] = window;
    // Hidden by anything but Tarn (Hyprland's close): closed, as from its menu.
    // Tarn's own close drops the window from m_windows before hiding it.
    connect(&window->view(), &QWindow::visibleChanged, window, [this, pin](bool visible) {
        if (!visible)
            m_pins.close(pin);
    });
    window->view().show();
}

} // namespace tarn
