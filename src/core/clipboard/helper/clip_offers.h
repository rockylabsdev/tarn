#pragma once
// What tarn-clip puts on the clipboard for one file. No Wayland and no Qt here,
// so the offer list can be tested without a compositor.
#include <string>
#include <vector>

namespace tarn::clip {

struct Offer {
    std::string mime;
    std::string text;            // the bytes to send; unused when isFileContent
    bool isFileContent = false;  // image/png: the helper streams the file itself
};

// file:///home/me/a%20b.png
std::string fileUri(const std::string &path);

// image/png (the file's bytes; left out when fileOnly), text/uri-list and
// x-special/gnome-copied-files, in that order.
std::vector<Offer> offersFor(const std::string &path, bool fileOnly);

// Text capture: UTF-8 text, offered as plain text. The helper reads the file
// once at start, so the file can be deleted as soon as the helper has exited.
std::vector<Offer> textOffers(const std::string &content);

} // namespace tarn::clip
