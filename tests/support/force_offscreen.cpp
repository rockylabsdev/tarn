// Compiled into every test executable (see tarn_add_test). Runs before main(),
// so a test binary started by hand from a shell on the live desktop still can't
// open windows on it or reach Hyprland: the same safe environment CTest sets.
#include <cstdlib>

namespace {
struct ForceOffscreen {
    ForceOffscreen() {
        setenv("QT_QPA_PLATFORM", "offscreen", 1);
        setenv("QT_QUICK_BACKEND", "software", 1);
        setenv("QT_QPA_PLATFORMTHEME", "generic", 1);
        unsetenv("WAYLAND_DISPLAY");
        unsetenv("DISPLAY");
        unsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    }
} forceOffscreen;
} // namespace
