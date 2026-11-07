#pragma once
// Counter numbering: the label for the nth Counter of a run.
#include <QString>

namespace tarn {

// style 0 = 1 2 3, 1 = A B C (then AA AB ...), 2 = i ii iii. `value` is the
// number to show (start + place in the run). Values below 1 have no letter or
// roman form and fall back to plain digits.
QString counterLabel(int style, int value);

} // namespace tarn
