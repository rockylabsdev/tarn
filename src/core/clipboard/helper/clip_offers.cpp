#include "clip_offers.h"

namespace tarn::clip {

std::string fileUri(const std::string &path) {
    static const char *hex = "0123456789ABCDEF";
    std::string out = "file://";
    for (unsigned char c : path) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                           c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
        if (plain) {
            out += char(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::vector<Offer> offersFor(const std::string &path, bool fileOnly) {
    const std::string uri = fileUri(path);
    std::vector<Offer> offers;
    if (!fileOnly)
        offers.push_back({"image/png", {}, true});
    offers.push_back({"text/uri-list", uri + "\r\n", false});
    offers.push_back({"x-special/gnome-copied-files", "copy\n" + uri, false});
    return offers;
}

std::vector<Offer> textOffers(const std::string &content) {
    return {{"text/plain;charset=utf-8", content, false}, {"text/plain", content, false}, {"UTF8_STRING", content, false}};
}

} // namespace tarn::clip
