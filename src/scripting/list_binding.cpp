//
// List binding - see list_binding.h.
//

#include "list_binding.h"

#include <new>
#include <string>
#include <string_view>

#include "components/ConfigManager.h"
#include "components/ListLogic.h"

extern freebutton::ConfigManager configManager;

namespace {

struct ItemsBuilder {
    std::string options;
    int count = 0;
};

freebutton::List* listAt(const int index) {
    return index < 0 ? nullptr : configManager.findList(static_cast<size_t>(index));
}

} // namespace

extern "C" {

int list_hw_find(const char* name, const size_t len) {
    uint32_t index = 0;
    if (name == nullptr ||
        !configManager.names().find(freebutton::NameTable::Kind::List, std::string_view(name, len), index)) {
        return -1;
    }
    return static_cast<int>(index);
}

int list_hw_count(const int index) {
    const auto* l = listAt(index);
    return l ? l->count() : -1;
}

int list_hw_get_selected(const int index) {
    const auto* l = listAt(index);
    return l ? l->selected() : -1;
}

int list_hw_move(const int index, const int step) {
    auto* l = listAt(index);
    return l ? l->move(step) : -1;
}

bool list_hw_select(const int index, const int item) {
    auto* l = listAt(index);
    return l != nullptr && l->select(item);
}

bool list_hw_set_font(const int index, const int face, const int size) {
    auto* l = listAt(index);
    if (l == nullptr) {
        return false;
    }
    l->setFont(face, size);
    return true;
}

bool list_hw_restore_font(const int index) {
    auto* l = listAt(index);
    if (l == nullptr) {
        return false;
    }
    l->restoreFont();
    return true;
}

void* list_hw_items_new(void) {
    return new (std::nothrow) ItemsBuilder();
}

bool list_hw_items_add(void* builder, const char* text, const size_t len) {
    auto* b = static_cast<ItemsBuilder*>(builder);
    return b != nullptr && freebutton::list::appendItem(b->options, b->count, std::string_view(text, len));
}

bool list_hw_items_commit(const int index, void* builder) {
    auto* b = static_cast<ItemsBuilder*>(builder);
    if (b == nullptr) {
        return false;
    }
    auto* l = listAt(index);
    if (l != nullptr) {
        l->setItems(std::move(b->options), b->count);
    }
    delete b;
    return l != nullptr;
}

void list_hw_items_free(void* builder) {
    delete static_cast<ItemsBuilder*>(builder);
}

} // extern "C"
