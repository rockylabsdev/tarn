#pragma once
// The Editor canvas's picture: paints an EditorSession's Capture document (and
// the Annotation being drawn) at the session's zoom and offset. It calls the
// same CaptureDocument::paint the export uses, so the canvas and the exported
// image come from one drawing.
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace tarn {

class EditorSession;

class DocumentCanvas : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *editing READ editing WRITE setEditing NOTIFY editingChanged)
public:
    explicit DocumentCanvas(QQuickItem *parent = nullptr);
    QObject *editing() const;
    void setEditing(QObject *session);
    void paint(QPainter *painter) override;

signals:
    void editingChanged();

private:
    QPointer<EditorSession> m_session;
    bool m_fadeTick = false;
};

} // namespace tarn
