#include "theme.h"

#include "palette/palette.h"

#include <QDirIterator>
#include <QFile>
#include <QFontDatabase>
#include <QHash>

namespace tarn {

namespace {
// `icons.svg("x")` in QML: the text of the compiled-in Tabler icon x.svg, which
// Icon.qml tints. (QML's XMLHttpRequest may not read Qt resources.)
class IconSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QString svg(const QString &name) {
        auto it = m_cache.find(name);
        if (it == m_cache.end()) {
            QFile f(QStringLiteral(":/qt/qml/Tarn/Ui/assets/icons/") + name + QStringLiteral(".svg"));
            it = m_cache.insert(name, f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString());
        }
        return it.value();
    }

private:
    QHash<QString, QString> m_cache;
};
} // namespace

void loadFonts() {
    static const bool loaded = [] {
        QDirIterator it(QStringLiteral(":/qt/qml/Tarn/Ui/assets/fonts"), {QStringLiteral("*.otf")}, QDir::Files);
        while (it.hasNext())
            QFontDatabase::addApplicationFont(it.next());
        return true;
    }();
    Q_UNUSED(loaded);
}

void installTheme(QQmlContext *context, Palette &palette) {
    loadFonts();
    context->setContextProperty(QStringLiteral("theme"), &palette);
    context->setContextProperty(QStringLiteral("icons"), new IconSource(context));
}

} // namespace tarn

#include "theme.moc"
