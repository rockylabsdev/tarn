#include "annotation.h"

#include <algorithm>

namespace tarn {

namespace {
QList<AnnotationKind> &registry() {
    static QList<AnnotationKind> kinds;
    return kinds;
}
} // namespace

void AnnotationKinds::add(AnnotationKind kind) {
    auto &kinds = registry();
    kinds.append(std::move(kind));
    std::stable_sort(kinds.begin(), kinds.end(), [](const auto &a, const auto &b) { return a.order < b.order; });
}

const AnnotationKind *AnnotationKinds::find(const QString &name) {
    for (const auto &k : registry())
        if (k.name == name)
            return &k;
    return nullptr;
}

namespace {
QMap<QString, AnnotationLoader> &loaders() {
    static QMap<QString, AnnotationLoader> map;
    return map;
}
} // namespace

void AnnotationLoaders::add(const QString &kind, AnnotationLoader loader) { loaders().insert(kind, std::move(loader)); }

const AnnotationLoader *AnnotationLoaders::find(const QString &kind) {
    const auto it = loaders().constFind(kind);
    return it == loaders().constEnd() ? nullptr : &*it;
}

QList<AnnotationKind> AnnotationKinds::all() { return registry(); }

} // namespace tarn
