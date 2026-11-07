#include "history_windows.h"

#include "history_window/history_window.h"
#include "palette/palette.h"
#include "theme.h"

#include "daemon/daemon.h"
#include "history/history.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>

namespace tarn {

namespace {
// image://historypicture/<id>/<revision>: the finished picture of an edited entry (its Project
// file rebuilt on the original and flattened), drawn for the tile. The revision in the URL makes
// a tile ask again after the edits changed. Used synchronously (the Image is not asynchronous).
class FinishedPictureProvider : public QQuickImageProvider {
public:
    explicit FinishedPictureProvider(History &history) : QQuickImageProvider(QQuickImageProvider::Image), m_history(history) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
        QImage image = QImage::fromData(m_history.editedPng(id.section(QLatin1Char('/'), 0, 0)), "PNG");
        if (image.isNull()) // reverted meanwhile: the tile will reload from the file
            image = QImage::fromData(m_history.originalPng(id.section(QLatin1Char('/'), 0, 0)), "PNG");
        if (requestedSize.width() > 0 && image.width() > requestedSize.width())
            image = image.scaledToWidth(requestedSize.width(), Qt::SmoothTransformation);
        *size = image.size();
        return image;
    }

private:
    History &m_history;
};
} // namespace

void HistoryWindowHandle::close() { m_service.close(); }

void HistoryWindowHandle::escape() { m_service.escape(); }

bool HistoryWindowHandle::startMove() { return m_view && m_view->startSystemMove(); }

HistoryWindows::HistoryWindows(HistoryWindow &service, Palette &palette, QObject *parent)
    : QObject(parent), m_service(service), m_palette(palette), m_handle(service) {
    connect(&service, &HistoryWindow::opened, this, &HistoryWindows::onOpened);
    connect(&service, &HistoryWindow::focusRequested, this, [this] {
        if (m_view) {
            m_view->raise();
            m_view->requestActivate();
        }
    });
    connect(&service, &HistoryWindow::closed, this, [this] {
        if (m_view)
            m_view->hide();
    });
}

void HistoryWindows::onOpened() {
    if (!m_view) {
        m_view = std::make_unique<QQuickView>();
        m_handle.setView(m_view.get());
        m_view->setTitle(QString::fromLatin1(HistoryWindow::kTitle));
        m_view->setFlags(Qt::Window | Qt::FramelessWindowHint);
        m_view->setColor(m_palette.canvas());
        m_view->setResizeMode(QQuickView::SizeRootObjectToView);
        m_view->engine()->addImageProvider(QStringLiteral("historypicture"), new FinishedPictureProvider(m_service.daemon().service<History>()));
        installTheme(m_view->rootContext(), m_palette);
        m_view->rootContext()->setContextProperty(QStringLiteral("historyModel"), &m_service.model());
        m_view->rootContext()->setContextProperty(QStringLiteral("win"), &m_handle);
        m_view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/Tarn/Ui/qml/History.qml")));
        // Hyprland closing the window by other means (the compositor): tell the service.
        connect(m_view.get(), &QWindow::visibleChanged, this, [this](bool visible) {
            if (!visible)
                m_service.windowHidden();
        });
    }
    // Every time, not only the first: the pointer may be on another monitor now.
    m_view->resize(m_service.openingSize());
    m_view->show();
    m_view->requestActivate();
}

} // namespace tarn
