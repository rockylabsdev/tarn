#include "project_file.h"

#include "annotation_json.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace tarn {

namespace {

// Keys of Frame::toMap() that are worked out from the rest (for the QML panel), not state.
const QStringList kDerivedFrameKeys = {QStringLiteral("missing"), QStringLiteral("on"), QStringLiteral("kind"), QStringLiteral("color"),
                                       QStringLiteral("slot")};

// The Frame as it looks now, with nothing that follows the desktop left in it.
Frame lockedLook(Frame frame) {
    frame.background.params.remove(QStringLiteral("slots"));   // a Palette gradient keeps its colors, not the slot names
    frame.background.params.remove(QStringLiteral("current")); // the current wallpaper keeps its actual file
    // The Watermark's picture is kept as data (written below), never re-read from its path.
    if (frame.watermark.mode == Watermark::Image && frame.watermark.image.isNull())
        frame.watermark.loadImage(frame.watermark.path);
    return frame;
}

QJsonObject cropToJson(const Crop &crop) {
    return {{QStringLiteral("rect"), crop.rect.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(json::rect(crop.rect))},
            {QStringLiteral("quarterTurns"), crop.quarterTurns},
            {QStringLiteral("mirrored"), crop.mirrored},
            {QStringLiteral("scale"), crop.scale}};
}

Crop cropFromJson(const QJsonObject &o) {
    Crop crop;
    if (const auto r = json::readRect(o.value(QStringLiteral("rect"))))
        crop.rect = r->toRect();
    crop.quarterTurns = ((o.value(QStringLiteral("quarterTurns")).toInt() % 4) + 4) % 4;
    crop.mirrored = o.value(QStringLiteral("mirrored")).toBool();
    crop.scale = qBound(Crop::kMinScale, o.value(QStringLiteral("scale")).toDouble(1.0), Crop::kMaxScale);
    return crop;
}

} // namespace

QString projectText(const QByteArray &json) {
    QStringList texts;
    for (const QJsonValue &a : QJsonDocument::fromJson(json).object().value(QStringLiteral("annotations")).toArray()) {
        const QString t = a.toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("text")).toString();
        if (!t.isEmpty())
            texts << t;
    }
    return texts.join(QLatin1Char('\n'));
}

QByteArray projectToJson(const CaptureDocument &document) {
    QJsonArray annotations;
    for (const auto &item : document.items()) {
        const QString kind = item.annotation->kind();
        if (!AnnotationLoaders::find(kind))
            continue;
        annotations.append(QJsonObject{{QStringLiteral("id"), item.id}, {QStringLiteral("kind"), kind}, {QStringLiteral("data"), item.annotation->toJson()}});
    }
    const Frame locked = lockedLook(document.frame());
    QVariantMap frame = locked.toMap();
    QVariantMap watermark = frame.value(QStringLiteral("watermark")).toMap();
    watermark.remove(QStringLiteral("hasImage"));
    if (!locked.watermark.image.isNull())
        watermark.insert(QStringLiteral("png"), QString::fromLatin1(locked.watermark.imagePng().toBase64()));
    frame.insert(QStringLiteral("watermark"), watermark);
    for (const QString &key : kDerivedFrameKeys)
        frame.remove(key);
    const QJsonObject root{{QStringLiteral("annotations"), annotations},
                           {QStringLiteral("crop"), cropToJson(document.crop())},
                           {QStringLiteral("frame"), QJsonObject::fromVariantMap(frame)}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

ProjectLoad loadProject(CaptureDocument &document, const QByteArray &bytes) {
    ProjectLoad result;
    QJsonParseError parse;
    const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !parsed.isObject()) {
        result.error = parse.error != QJsonParseError::NoError ? parse.errorString() : QStringLiteral("not a JSON object");
        return result;
    }
    const QJsonObject root = parsed.object();
    if (!root.value(QStringLiteral("annotations")).isArray()) {
        result.error = QStringLiteral("no annotations list");
        return result;
    }

    std::vector<CaptureDocument::Item> items;
    QSet<int> seen;
    int nextId = 1;
    for (const QJsonValue &entry : root.value(QStringLiteral("annotations")).toArray()) {
        const QJsonObject o = entry.toObject();
        const QString kind = o.value(QStringLiteral("kind")).toString();
        const AnnotationLoader *loader = AnnotationLoaders::find(kind);
        if (!o.value(QStringLiteral("id")).isDouble() || !loader) {
            result.error = loader ? QStringLiteral("an annotation has no id") : QStringLiteral("unknown annotation kind \"%1\"").arg(kind);
            return result;
        }
        const int id = o.value(QStringLiteral("id")).toInt();
        std::unique_ptr<Annotation> annotation = (*loader)(o.value(QStringLiteral("data")).toObject());
        if (!annotation || id < 1 || seen.contains(id)) {
            result.error = QStringLiteral("a \"%1\" annotation is damaged").arg(kind);
            return result;
        }
        seen.insert(id);
        nextId = qMax(nextId, id + 1);
        items.push_back({id, std::move(annotation)});
    }

    Frame frame = Frame::fromMap(root.value(QStringLiteral("frame")).toObject().toVariantMap());
    // A wallpaper that is gone: a blurred copy of the shot instead, so the capture still frames.
    if (frame.active()) {
        const BackgroundKind *kind = BackgroundKinds::find(frame.background.kind);
        if (kind && kind->missing && kind->missing(frame.background.params)) {
            result.backgroundMissing = true;
            result.missingFile = frame.background.params.value(QStringLiteral("path")).toString();
            if (result.missingFile.isEmpty())
                result.missingFile = frame.background.params.value(QStringLiteral("name")).toString();
            frame.background = Background{QStringLiteral("blur"), {{QStringLiteral("style"), QStringLiteral("heavy")}}};
        }
    }

    document.restoreFrom(std::move(items), nextId, cropFromJson(root.value(QStringLiteral("crop")).toObject()), frame);
    result.ok = true;
    return result;
}

} // namespace tarn
