#pragma once
// Last area per app: the area most recently taken over an app's window,
// kept in Remembered state under `[last_areas]`, keyed by the app's window class,
// as "x,y,width,height" in screen (layout) coordinates. v1's single `[last_area]`
// table is never read.
#include <QRect>
#include <QString>

namespace tarn {

class RememberedState;

namespace lastarea {

void remember(RememberedState &state, const QString &app, const QRect &screenRect);
// Empty if the app has none.
QRect recall(const RememberedState &state, const QString &app);

} // namespace lastarea
} // namespace tarn
