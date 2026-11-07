#pragma once
// ToolOptions: what every tool draws with, shared by all Editors and remembered
// across captures and restarts in state.toml.
//
// - ONE color (with opacity) for every tool.
// - A size step per size group. Each group has six steps and opens on its
//   third; the group a tool uses is AnnotationKind::sizeGroup.
// - Styles and strengths (Counter style, Pixelate strength, ...): a tool ticket
//   remembers its own with styleValue()/setStyleValue(); they land under [style].
//
// Keys: [tool] color = "#aarrggbb"; [size] line / text / counter / highlighter
// = step index 0..5.
#include "document/annotation.h"

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>

namespace tarn {

class Daemon;
class RememberedState;

class ToolOptions : public QObject {
    Q_OBJECT
public:
    static constexpr int kSteps = 6;
    static constexpr int kDefaultStep = 2; // the third
    struct Group {
        QString name;
        QString unit; // "px" or "pt"
        qreal steps[kSteps];
    };

    explicit ToolOptions(Daemon &);

    static QList<Group> groups();
    static const Group *group(const QString &name);
    // The 8 fixed swatches of the color popover.
    static QList<QColor> fixedSwatches();
    static QColor defaultColor() { return QColor(0xFF, 0x45, 0x3A); }

    QColor color() const { return m_color; }
    void setColor(const QColor &color);

    int stepIndex(const QString &group) const;
    qreal size(const QString &group) const; // px or pt
    void setStepIndex(const QString &group, int index); // clamped to 0..5
    void step(const QString &group, int delta);         // [ is -1, ] is +1; stops at the ends

    // The style a tool draws with right now.
    ToolStyle style() const;

    // A tool's choice (AnnotationKind::choices): Pixelate style, Spotlight shape...
    QString choice(const QString &name, const QString &fallback) const;
    void setChoice(const QString &name, const QString &value);

    QVariant styleValue(const QString &name, const QVariant &fallback = {}) const;
    void setStyleValue(const QString &name, const QVariant &value);

signals:
    void colorChanged();
    void sizeChanged(const QString &group);
    void choiceChanged(const QString &name);

private:
    RememberedState &m_state;
    QColor m_color;
    int m_index[4];
};

} // namespace tarn
