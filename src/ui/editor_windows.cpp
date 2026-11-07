#include "editor_windows.h"

#include "editor/editor.h"
#include "editor/editor_session.h"
#include "palette/palette.h"
#include "theme.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QUrl>
#include <QQmlEngine>

// Registers the module's C++ QML types (DocumentCanvas). The module is a static
// library, so nothing else would pull the generated registration in.
void qml_register_types_Tarn_Ui();

namespace tarn {

namespace {
// image://background/<kind>/<JSON params, percent-encoded>: a tile of a Background
// for the panel (gradients, wallpapers, blurred copies of this session's shot).
class BackgroundTileProvider : public QQuickImageProvider {
public:
    explicit BackgroundTileProvider(EditorSession &session) : QQuickImageProvider(QQuickImageProvider::Image), m_session(session) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
        const QString kind = id.section(QLatin1Char('/'), 0, 0);
        QString json = id.section(QLatin1Char('/'), 1);
        if (!json.startsWith(QLatin1Char('{'))) // Qt may or may not have decoded it already
            json = QUrl::fromPercentEncoding(json.toUtf8());
        const QVariantMap params = QJsonDocument::fromJson(json.toUtf8()).object().toVariantMap();
        const QSize want = requestedSize.isValid() ? requestedSize : QSize(112, 80);
        const QImage image = m_session.backgroundTile(kind, params, want);
        *size = image.size();
        return image;
    }

private:
    EditorSession &m_session;
};
} // namespace

EditorWindow::EditorWindow(EditorSession &session, Palette &palette, QObject *parent) : QObject(parent) {
    static const bool typesRegistered = (qml_register_types_Tarn_Ui(), true);
    Q_UNUSED(typesRegistered);
    m_view.setTitle(session.windowTitle());
    m_view.setFlags(Qt::Window | Qt::FramelessWindowHint);
    m_view.setColor(palette.canvas());
    m_view.setResizeMode(QQuickView::SizeRootObjectToView);
    // The opening size, before the first show: Hyprland ignores later self-resizes.
    m_view.resize(session.openingSize());

    m_view.engine()->addImageProvider(QStringLiteral("background"), new BackgroundTileProvider(session));
    installTheme(m_view.rootContext(), palette);
    m_view.rootContext()->setContextProperty(QStringLiteral("session"), &session);
    m_view.rootContext()->setContextProperty(QStringLiteral("win"), this);
    m_view.setSource(QUrl(QStringLiteral("qrc:/qt/qml/Tarn/Ui/qml/Editor.qml")));
}

EditorWindows::EditorWindows(Editor &editor, Palette &palette, QObject *parent) : QObject(parent), m_palette(palette) {
    connect(&editor, &Editor::opened, this, &EditorWindows::onOpened);
    // Edit on an entry that is already open in an Editor: that window comes forward.
    connect(&editor, &Editor::broughtForward, this, [this](EditorSession *session) {
        if (QQuickView *view = viewFor(session->captureId())) {
            view->raise();
            view->requestActivate();
        }
    });
}

QQuickView *EditorWindows::viewFor(int captureId) const {
    const auto it = m_windows.find(captureId);
    return it == m_windows.end() || !it.value() ? nullptr : &it.value()->view();
}

void EditorWindows::onOpened(EditorSession *session) {
    const int id = session->captureId();
    auto *window = new EditorWindow(*session, m_palette, this);
    m_windows[id] = window;
    // Any way out of the Editor takes the window with it.
    connect(session, &EditorSession::finished, window, [window] {
        window->view().hide();
        window->deleteLater();
    });
    // Hidden by anything but Tarn (Hyprland's close): the normal way out, Done.
    // Tarn's own closes finish the session first, so done() is then a no-op.
    connect(&window->view(), &QWindow::visibleChanged, window, [session](bool visible) {
        if (!visible && session->isOpen())
            session->done();
    });
    window->view().show();
}

} // namespace tarn
