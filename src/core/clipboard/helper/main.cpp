// tarn-clip [--file-only | --text] <absolute-file-path>
//
// Puts one file on the Wayland clipboard in several forms at once (image/png,
// text/uri-list, x-special/gnome-copied-files), then detaches and keeps serving
// it until something else is copied, so the clipboard outlives Tarn. It speaks
// ext-data-control-v1, the protocol wl-copy uses; wl-copy itself can only offer
// one type per call.
//
// Exit 0 once the clipboard is set (a detached process carries on serving);
// exit 1 with a message on stderr otherwise.
#include "clip_offers.h"
#include "ext-data-control-v1-client.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>

namespace {

struct State {
    wl_seat *seat = nullptr;
    ext_data_control_manager_v1 *manager = nullptr;
    std::string path;
    std::vector<tarn::clip::Offer> offers;
    bool canceled = false;
};

void onGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t) {
    auto *s = static_cast<State *>(data);
    if (!std::strcmp(interface, wl_seat_interface.name) && !s->seat)
        s->seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
    else if (!std::strcmp(interface, ext_data_control_manager_v1_interface.name))
        s->manager = static_cast<ext_data_control_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1));
}
void onGlobalRemove(void *, wl_registry *, uint32_t) {}
const wl_registry_listener registryListener = {onGlobal, onGlobalRemove};

void writeAll(int fd, const char *p, size_t n) {
    while (n > 0) {
        const ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return;
        p += w;
        n -= size_t(w);
    }
}

void onSend(void *data, ext_data_control_source_v1 *, const char *mime, int32_t fd) {
    auto *s = static_cast<State *>(data);
    for (const auto &offer : s->offers) {
        if (offer.mime != mime)
            continue;
        if (offer.isFileContent) {
            const int in = open(s->path.c_str(), O_RDONLY | O_CLOEXEC);
            if (in >= 0) {
                char buf[65536];
                ssize_t n;
                while ((n = read(in, buf, sizeof buf)) > 0)
                    writeAll(fd, buf, size_t(n));
                close(in);
            }
        } else {
            writeAll(fd, offer.text.data(), offer.text.size());
        }
        break;
    }
    close(fd);
}
void onCanceled(void *data, ext_data_control_source_v1 *) { static_cast<State *>(data)->canceled = true; }
const ext_data_control_source_v1_listener sourceListener = {onSend, onCanceled};

void onDataOffer(void *, ext_data_control_device_v1 *, ext_data_control_offer_v1 *offer) {
    ext_data_control_offer_v1_destroy(offer);
}
void onSelection(void *, ext_data_control_device_v1 *, ext_data_control_offer_v1 *) {}
void onFinished(void *, ext_data_control_device_v1 *) {}
void onPrimary(void *, ext_data_control_device_v1 *, ext_data_control_offer_v1 *) {}
const ext_data_control_device_v1_listener deviceListener = {onDataOffer, onSelection, onFinished, onPrimary};

int fail(const char *message) {
    std::fprintf(stderr, "tarn-clip: %s\n", message);
    return 1;
}

} // namespace

int main(int argc, char **argv) {
    State state;
    bool fileOnly = false, text = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--file-only"))
            fileOnly = true;
        else if (!std::strcmp(argv[i], "--text"))
            text = true; // the file holds UTF-8 text, offered as plain text
        else
            state.path = argv[i];
    }
    if (state.path.empty() || state.path[0] != '/')
        return fail("usage: tarn-clip [--file-only | --text] <absolute-file-path>");
    struct stat st;
    if (stat(state.path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return fail("the file does not exist");
    if (text) {
        std::string content;
        const int in = open(state.path.c_str(), O_RDONLY | O_CLOEXEC);
        if (in < 0)
            return fail("the file cannot be read");
        char buf[65536];
        ssize_t n;
        while ((n = read(in, buf, sizeof buf)) > 0)
            content.append(buf, size_t(n));
        close(in);
        state.offers = tarn::clip::textOffers(content);
    } else {
        state.offers = tarn::clip::offersFor(state.path, fileOnly);
    }

    signal(SIGPIPE, SIG_IGN);
    wl_display *display = wl_display_connect(nullptr);
    if (!display)
        return fail("cannot connect to the Wayland display");
    wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registryListener, &state);
    wl_display_roundtrip(display);
    if (!state.seat || !state.manager)
        return fail("the compositor has no ext-data-control-v1 (or no seat)");

    auto *device = ext_data_control_manager_v1_get_data_device(state.manager, state.seat);
    ext_data_control_device_v1_add_listener(device, &deviceListener, &state);
    auto *source = ext_data_control_manager_v1_create_data_source(state.manager);
    ext_data_control_source_v1_add_listener(source, &sourceListener, &state);
    for (const auto &offer : state.offers)
        ext_data_control_source_v1_offer(source, offer.mime.c_str());
    ext_data_control_device_v1_set_selection(device, source);
    if (wl_display_roundtrip(display) < 0)
        return fail("the compositor refused the clipboard");

    // Detach: the parent reports success, the child keeps serving pastes.
    const pid_t pid = fork();
    if (pid < 0)
        return fail("cannot fork");
    if (pid > 0)
        _exit(0);
    setsid();
    if (chdir("/") != 0) {
    }
    const int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, 0);
        dup2(devnull, 1);
        dup2(devnull, 2);
    }
    while (!state.canceled && wl_display_dispatch(display) >= 0) {
    }
    return 0;
}
