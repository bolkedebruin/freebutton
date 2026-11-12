//
// Image binding - see image_binding.h.
//

#include "image_binding.h"

#include <string_view>

#include "components/ConfigManager.h"

extern freebutton::ConfigManager configManager;

namespace {

freebutton::ImageItem* imageAt(const int index) {
    return index < 0 ? nullptr : configManager.findImage(static_cast<size_t>(index));
}

} // namespace

extern "C" {

int image_hw_find(const char* name, const size_t len) {
    uint32_t index = 0;
    if (name == nullptr ||
        !configManager.names().find(freebutton::NameTable::Kind::Image, std::string_view(name, len), index)) {
        return -1;
    }
    return static_cast<int>(index);
}

bool image_hw_exists(const int index) {
    return imageAt(index) != nullptr;
}

int image_hw_set_icon(const int index, const char* name, const size_t len) {
    auto* image = imageAt(index);
    if (image == nullptr) {
        return -1;
    }
    return image->setIcon(std::string_view(name ? name : "", name ? len : 0)) ? 0 : -2;
}

int image_hw_set_src(const int index, const char* key, const size_t len) {
    auto* image = imageAt(index);
    if (image == nullptr) {
        return -1;
    }
    return image->setSrc(std::string_view(key ? key : "", key ? len : 0)) ? 0 : -2;
}

} // extern "C"
