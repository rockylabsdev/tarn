#include "document_canvas.h"

#include "editor/editor_session.h"

#include <QPainter>
#include <QTimer>

namespace tarn {

DocumentCanvas::DocumentCanvas(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setRenderTarget(QQuickPaintedItem::Image); // the raster engine: the one the export uses
    setAntialiasing(true);
}

QObject *DocumentCanvas::editing() const { return m_session; }

void DocumentCanvas::setEditing(QObject *session) {
    auto *s = qobject_cast<EditorSession *>(session);
    if (s == m_session)
        return;
    if (m_session)
        m_session->disconnect(this);
    m_session = s;
    if (s) {
        connect(s, &EditorSession::documentChanged, this, [this] { update(); });
        connect(s, &EditorSession::viewChanged, this, [this] { update(); });
        connect(s, &EditorSession::cropChanged, this, [this] { update(); });
    }
    update();
    emit editingChanged();
}

void DocumentCanvas::paint(QPainter *painter) {
    if (!m_session)
        return;
    const qreal z = m_session->zoom();
    const QPointF o = m_session->origin();
    painter->save();
    painter->translate(o);
    painter->scale(z, z);
    // Zoomed out, smooth; zoomed in, show the real pixels.
    painter->setRenderHint(QPainter::SmoothPixmapTransform, z < 1.0);
    // The document cuts at the Crop (or, in Crop mode, at the canvas) itself, as the export does.
    m_session->document().paint(*painter, m_session->draft(), !m_session->cropping());
    painter->restore();
    // A style crossfade (Pixelate) is running: keep repainting until it ends. This is
    // content changing, not a control, so it animates even though shortcuts open UI instantly.
    if (!m_fadeTick && m_session->document().fading()) {
        m_fadeTick = true;
        QTimer::singleShot(16, this, [this] {
            m_fadeTick = false;
            update();
        });
    }
}

} // namespace tarn
