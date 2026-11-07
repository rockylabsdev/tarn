#pragma once
// Saved colors: the row of up to 8 colors the user keeps, shown in the Editor's
// color popover and the Frame panel's Plain color section (the same list).
//
// Rules: newest first, at most 8 (the oldest is dropped), a color equal to a
// saved one (same RGB and opacity) moves to the front instead of repeating.
// Kept in Remembered state as one string, `[colors] saved = "#aarrggbb,..."`,
// newest first, so it survives a restart. Color pick adds through save(); the
// Editor's Eyedropper never does.
#include <QColor>
#include <QList>
#include <QObject>

namespace tarn {

class Daemon;
class RememberedState;

class SavedColors : public QObject {
    Q_OBJECT
public:
    static constexpr int kMax = 8;

    explicit SavedColors(Daemon &);

    QList<QColor> colors() const { return m_colors; } // newest first
    void save(const QColor &color);
    void remove(int index);

signals:
    void changed();

private:
    void store();

    RememberedState &m_state;
    QList<QColor> m_colors;
};

} // namespace tarn
