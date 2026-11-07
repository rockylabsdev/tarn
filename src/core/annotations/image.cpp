// The added image Annotation. See image.h.
#include "image.h"

#include "document/annotation_json.h"

#include <QBuffer>

namespace tarn {

namespace {

class ImageAnnotation : public Annotation {
public:
    ImageAnnotation(const QImage &image, const QRectF &rect) : m_image(image), m_rect(rect) {}
    QString kind() const override { return QStringLiteral("image"); }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<ImageAnnotation>(*this); }
    QJsonObject toJson() const override {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        m_image.save(&buffer, "PNG");
        return {{QStringLiteral("rect"), json::rect(m_rect)}, {QStringLiteral("png"), QString::fromLatin1(png.toBase64())}};
    }
    QRectF bounds() const override { return m_rect; }
    bool growsCanvas() const override { return true; }
    void translate(const QPointF &delta) override { m_rect.translate(delta); }
    QRectF frame() const override { return m_rect; }
    void setFrame(const QRectF &frame) override {
        const QRectF f = frame.normalized();
        if (f.width() >= 1 && f.height() >= 1) // never collapses to nothing
            m_rect = f;
    }
    void paint(QPainter &painter, const QImage &) const override {
        painter.save();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, m_rect.size() != QSizeF(m_image.size()));
        painter.drawImage(m_rect, m_image);
        painter.restore();
    }

private:
    QImage m_image; // implicitly shared: clones cost nothing
    QRectF m_rect;
};

// The picture is PNG data inside the Project file, so a moved or deleted source
// file never breaks an old capture.
const RegisterAnnotationLoader loadImage(QStringLiteral("image"), [](const QJsonObject &o) -> std::unique_ptr<Annotation> {
    const auto rect = json::readRect(o.value(QStringLiteral("rect")));
    const QImage image = QImage::fromData(QByteArray::fromBase64(o.value(QStringLiteral("png")).toString().toLatin1()), "PNG");
    if (!rect || image.isNull())
        return nullptr;
    return std::make_unique<ImageAnnotation>(image, *rect);
});

} // namespace

std::unique_ptr<Annotation> makeImageAnnotation(const QImage &image, const QRectF &rect) {
    return std::make_unique<ImageAnnotation>(image, rect);
}

} // namespace tarn
