#include "tool_options.h"

#include "daemon/daemon.h"
#include "state/remembered_state.h"

#include <algorithm>

namespace tarn {

namespace {
const QList<ToolOptions::Group> &table() {
    static const QList<ToolOptions::Group> g = {
        {QStringLiteral("line"), QStringLiteral("px"), {2, 4, 6, 10, 16, 24}},
        {QStringLiteral("text"), QStringLiteral("pt"), {16, 22, 30, 40, 56, 80}},
        {QStringLiteral("counter"), QStringLiteral("pt"), {11, 14, 20, 24, 31, 45}},
        {QStringLiteral("highlighter"), QStringLiteral("px"), {12, 18, 26, 36, 50, 70}},
    };
    return g;
}
int indexOf(const QString &name) {
    for (int i = 0; i < table().size(); ++i)
        if (table()[i].name == name)
            return i;
    return -1;
}
} // namespace

QList<ToolOptions::Group> ToolOptions::groups() { return table(); }

const ToolOptions::Group *ToolOptions::group(const QString &name) {
    const int i = indexOf(name);
    return i < 0 ? nullptr : &table()[i];
}

QList<QColor> ToolOptions::fixedSwatches() {
    return {QColor(0xFF, 0x45, 0x3A), QColor(0xFF, 0x9F, 0x0A), QColor(0xFF, 0xD6, 0x0A), QColor(0x30, 0xD1, 0x58),
            QColor(0x0A, 0x84, 0xFF), QColor(0xBF, 0x5A, 0xF2), QColor(0xFF, 0xFF, 0xFF), QColor(0x00, 0x00, 0x00)};
}

ToolOptions::ToolOptions(Daemon &daemon) : QObject(), m_state(daemon.service<RememberedState>()), m_color(defaultColor()) {
    const QColor saved(m_state.value(QStringLiteral("tool.color")).toString());
    if (saved.isValid())
        m_color = saved;
    for (int i = 0; i < table().size(); ++i) {
        bool ok = false;
        const int v = m_state.value(QStringLiteral("size.") + table()[i].name, kDefaultStep).toInt(&ok);
        m_index[i] = ok ? std::clamp(v, 0, kSteps - 1) : kDefaultStep;
    }
}

void ToolOptions::setColor(const QColor &picked) {
    if (!picked.isValid())
        return;
    const QColor color = QColor::fromRgba(picked.rgba()); // 8 bits a channel, as the file keeps it
    if (color == m_color)
        return;
    m_color = color;
    m_state.setValueSoon(QStringLiteral("tool.color"), color.name(QColor::HexArgb)); // sliders send many a second
    emit colorChanged();
}

int ToolOptions::stepIndex(const QString &g) const {
    const int i = indexOf(g);
    return i < 0 ? kDefaultStep : m_index[i];
}

qreal ToolOptions::size(const QString &g) const {
    const int i = indexOf(g);
    return i < 0 ? 0 : table()[i].steps[m_index[i]];
}

void ToolOptions::setStepIndex(const QString &g, int index) {
    const int i = indexOf(g);
    if (i < 0)
        return;
    index = std::clamp(index, 0, kSteps - 1);
    if (index == m_index[i])
        return;
    m_index[i] = index;
    m_state.setValue(QStringLiteral("size.") + g, index);
    emit sizeChanged(g);
}

void ToolOptions::step(const QString &g, int delta) { setStepIndex(g, stepIndex(g) + delta); }

ToolStyle ToolOptions::style() const {
    ToolStyle s;
    s.color = m_color;
    s.lineWidth = size(QStringLiteral("line"));
    s.textSize = size(QStringLiteral("text"));
    s.counterSize = size(QStringLiteral("counter"));
    s.highlighterSize = size(QStringLiteral("highlighter"));
    s.textLook = std::clamp(styleValue(QStringLiteral("text_look"), 0).toInt(), 0, 2);
    const QString arrow = styleValue(QStringLiteral("arrow"), s.arrowStyle).toString();
    if (arrow == QLatin1String("tapered") || arrow == QLatin1String("open") || arrow == QLatin1String("double"))
        s.arrowStyle = arrow;
    s.counterStyle = std::clamp(styleValue(QStringLiteral("counter_style"), 0).toInt(), 0, 2);
    s.counterStart = std::clamp(styleValue(QStringLiteral("counter_start"), 1).toInt(), 0, 999);
    for (const AnnotationKind &kind : AnnotationKinds::all())
        for (const ToolChoice &c : kind.choices)
            s.choices.insert(c.name, choice(c.name, c.fallback));
    return s;
}

QString ToolOptions::choice(const QString &name, const QString &fallback) const {
    return styleValue(name, fallback).toString();
}

void ToolOptions::setChoice(const QString &name, const QString &value) {
    if (choice(name, QString()) == value)
        return;
    setStyleValue(name, value);
    emit choiceChanged(name);
}

QVariant ToolOptions::styleValue(const QString &name, const QVariant &fallback) const {
    return m_state.value(QStringLiteral("style.") + name, fallback);
}

void ToolOptions::setStyleValue(const QString &name, const QVariant &value) {
    m_state.setValue(QStringLiteral("style.") + name, value);
}

} // namespace tarn
