// The Frame's side of EditorSession: what the Background panel calls.
// Every change goes through the document, which records it in the undo history.
#include "editor_session.h"

#include <QColor>
#include <QFileInfo>
#include <QUrl>

#include "daemon/daemon.h"
#include "editor.h"
#include "notify/notifier.h"
#include "state/remembered_state.h"
#include <algorithm>

namespace tarn {

void EditorSession::frameChanged() {
    clampPan();
    emit viewChanged();
    changed();
}

namespace {
Frame withBackground(Frame f, const QString &kind, const QVariantMap &params) {
    f.background.kind = kind;
    f.background.params = params;
    if (!params.value(QStringLiteral("color")).isNull()) { // colors are stored as "#aarrggbb"
        QVariantMap p = params;
        p[QStringLiteral("color")] = QColor(params.value(QStringLiteral("color")).toString()).name(QColor::HexArgb);
        f.background.params = p;
    }
    return f;
}
} // namespace

void EditorSession::setFrameBackground(const QString &kind, const QVariantMap &params) {
    if (m_mode != Mode::None)
        return;
    m_document->setFrame(withBackground(m_document->frame(), kind, params),
                         kind == QLatin1String("none") ? QStringLiteral("Remove Frame") : QStringLiteral("Background"));
    frameChanged();
}

void EditorSession::dragFrameBackground(const QString &kind, const QVariantMap &params) {
    if (m_mode != Mode::None)
        return;
    if (!m_frameBeforeDrag)
        m_frameBeforeDrag = m_document->frame();
    m_document->setFrameLive(withBackground(m_document->frame(), kind, params));
    frameChanged();
}

// Sets one control on `frame`; false for an unknown key.
bool EditorSession::applyFrameValue(Frame &f, const QString &key, const QVariant &v) const {
    auto fraction = [&](qreal max) { return std::clamp(v.toDouble(), 0.0, max); };
    if (key == QLatin1String("padding"))
        f.padding = fraction(Frame::kMaxPadding);
    else if (key == QLatin1String("inset"))
        f.inset = fraction(Frame::kMaxInset);
    else if (key == QLatin1String("shadow"))
        f.shadow = fraction(Frame::kMaxShadow);
    else if (key == QLatin1String("corners"))
        f.corners = fraction(Frame::kMaxCorners);
    else if (key == QLatin1String("autoBalance"))
        f.autoBalance = v.toBool();
    else if (key == QLatin1String("ah"))
        f.alignH = std::clamp(v.toInt(), -1, 1);
    else if (key == QLatin1String("av"))
        f.alignV = std::clamp(v.toInt(), -1, 1);
    else if (key == QLatin1String("ratio"))
        f.ratio = v.toString();
    else if (key == QLatin1String("wmMode")) { // off | image | text (an image needs setWatermarkImage first)
        const QString m = v.toString();
        if (m == QLatin1String("off"))
            f.watermark.mode = Watermark::Off;
        else if (m == QLatin1String("text"))
            f.watermark.mode = Watermark::Text;
        else if (m == QLatin1String("image") && !f.watermark.image.isNull())
            f.watermark.mode = Watermark::Image;
        else
            return false;
    } else if (key == QLatin1String("wmText"))
        f.watermark.text = v.toString().simplified();
    else if (key == QLatin1String("wmCorner"))
        f.watermark.corner = std::clamp(v.toInt(), 0, 3);
    else if (key == QLatin1String("wmSize"))
        f.watermark.size = std::clamp(v.toDouble(), Watermark::kMinSize, Watermark::kMaxSize);
    else if (key == QLatin1String("wmOpacity"))
        f.watermark.opacity = std::clamp(v.toDouble(), Watermark::kMinOpacity, 1.0);
    else
        return false;
    return true;
}

void EditorSession::setFrameValue(const QString &key, const QVariant &value) {
    if (m_mode != Mode::None)
        return;
    Frame f = m_document->frame();
    if (!applyFrameValue(f, key, value))
        return;
    m_document->setFrame(f, key == QLatin1String("ratio") ? QStringLiteral("Ratio") : QStringLiteral("Frame"));
    rememberWatermark(f.watermark, false);
    frameChanged();
}

// "Last Watermark": kept in Remembered state whenever a Watermark that is on is set or edited
// in the panel. Called from the panel's setters, not from setFrame: applying a Preset
// sets the Frame too, and must not become your last Watermark.
void EditorSession::rememberWatermark(const Watermark &w, bool soon) {
    if (w.on())
        m_editor.daemon().service<RememberedState>().setMap(QStringLiteral("watermark"), w.rememberedMap(), soon);
}

// The Watermark button and W: your last Watermark on, or the Watermark off (one undo step each).
// With none remembered it changes nothing and asks the panel to open at the Watermark section.
void EditorSession::toggleWatermark() {
    if (m_mode != Mode::None)
        return;
    Frame f = m_document->frame();
    if (f.watermark.on()) {
        f.watermark.mode = Watermark::Off;
    } else {
        Watermark last = Watermark::fromMap(m_editor.daemon().service<RememberedState>().map(QStringLiteral("watermark")));
        if (!last.on()) {
            emit watermarkSetupRequested();
            return;
        }
        if (last.mode == Watermark::Image) {
            if (last.path.isEmpty() || !last.loadImage(last.path)) {
                m_editor.daemon().service<Notifier>().notify(QStringLiteral("Tarn could not use the Watermark image"),
                                                             (last.path.isEmpty() ? QStringLiteral("It") : QFileInfo(last.path).fileName()) + QStringLiteral(" is missing or is not a picture Tarn can read"));
                return;
            }
        } else { // a text one keeps this shot's picture, so the panel can still switch to Image
            last.path = f.watermark.path;
            last.image = f.watermark.image;
        }
        f.watermark = last;
    }
    m_document->setFrame(f, QStringLiteral("Watermark"));
    frameChanged();
}

// The Watermark's image chooser: reads the picture now (the document keeps its pixels).
bool EditorSession::setWatermarkImage(const QString &pathOrUrl) {
    if (m_mode != Mode::None)
        return false;
    const QUrl url(pathOrUrl);
    const QString path = QFileInfo(url.isLocalFile() ? url.toLocalFile() : pathOrUrl).absoluteFilePath();
    Frame f = m_document->frame();
    if (!f.watermark.loadImage(path)) {
        m_editor.daemon().service<Notifier>().notify(QStringLiteral("Tarn could not use that image"),
                                                     QFileInfo(path).fileName() + QStringLiteral(" is not a picture Tarn can read"));
        return false;
    }
    f.watermark.mode = Watermark::Image;
    m_document->setFrame(f, QStringLiteral("Watermark"));
    rememberWatermark(f.watermark, false);
    frameChanged();
    return true;
}

void EditorSession::dragFrameValue(const QString &key, const QVariant &value) {
    if (m_mode != Mode::None)
        return;
    Frame f = m_document->frame();
    if (!applyFrameValue(f, key, value))
        return;
    if (!m_frameBeforeDrag)
        m_frameBeforeDrag = m_document->frame();
    m_document->setFrameLive(f);
    rememberWatermark(f.watermark, true);
    frameChanged();
}

void EditorSession::endFrameDrag() {
    if (!m_frameBeforeDrag)
        return;
    const Frame before = *m_frameBeforeDrag;
    m_frameBeforeDrag.reset();
    m_document->recordFrameChange(QStringLiteral("Frame"), before);
    emit documentChanged();
}

} // namespace tarn
