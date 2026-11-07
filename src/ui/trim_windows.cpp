#include "trim_windows.h"

#include "palette/palette.h"
#include "theme.h"
#include "trim/trim.h"
#include "trim_frames.h"

#include <QQmlContext>
#include <QQmlEngine>

namespace tarn {

TrimWindow::TrimWindow(Trim &trim, TrimSession &session, Palette &palette, QObject *parent) : QObject(parent) {
    m_view.setTitle(QString::fromLatin1(TrimWindows::kTitle));
    m_view.setFlags(Qt::Window | Qt::FramelessWindowHint);
    m_view.setColor(palette.canvas());
    m_view.setResizeMode(QQuickView::SizeRootObjectToView);
    // The opening size, before the first show: Hyprland ignores later self-resizes.
    m_view.resize(session.openingSize());

    installTheme(m_view.rootContext(), palette);
    m_view.engine()->addImageProvider(QStringLiteral("trim"), new TrimFrames(trim));
    m_view.engine()->addImageProvider(QStringLiteral("trimcache"), new TrimCacheFrames(trim));
    m_view.rootContext()->setContextProperty(QStringLiteral("session"), &session);
    m_view.rootContext()->setContextProperty(QStringLiteral("win"), this);
    m_view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/Tarn/Ui/qml/Trim.qml")));
}

TrimWindows::TrimWindows(Trim &trim, Palette &palette, QObject *parent) : QObject(parent), m_trim(trim), m_palette(palette) {
    connect(&trim, &Trim::opened, this, &TrimWindows::onOpened);
}

QQuickView *TrimWindows::viewFor(int captureId) const {
    const auto it = m_windows.find(captureId);
    return it == m_windows.end() || !it.value() ? nullptr : &it.value()->view();
}

void TrimWindows::onOpened(TrimSession *session) {
    const int id = session->captureId();
    auto *window = new TrimWindow(m_trim, *session, m_palette, this);
    m_windows[id] = window;
    // Any way out of Trim takes the window with it.
    connect(session, &TrimSession::finished, window, [window] {
        window->view().hide();
        window->deleteLater();
    });
    // Hidden by anything but Tarn (Hyprland's close): canceled, as the × does.
    connect(&window->view(), &QWindow::visibleChanged, window, [session](bool visible) {
        if (!visible && session->isOpen())
            session->cancel();
    });
    window->view().show();
}

} // namespace tarn
