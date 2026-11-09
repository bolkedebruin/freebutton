//
// ListLogic - the pure part of a main-display list (List.h): its items as one
// roller options string ("Bed Time\nEvening") and the selection rules.
//
// No LVGL, no ESP: unit-tested on the host (test/test_list_logic).
//

#ifndef FREEBUTTON_LISTLOGIC_H
#define FREEBUTTON_LISTLOGIC_H

#include <cstddef>
#include <string>
#include <string_view>

namespace freebutton::list {

// Most items a list keeps; more are dropped (logged by the caller).
constexpr int kMaxItems = 32;
// Longest item text in bytes; longer ones are cut (on a UTF-8 boundary).
constexpr size_t kMaxItemBytes = 48;
// Visible rows of the roller.
constexpr int kMinRows = 1;
constexpr int kMaxRows = 7;
constexpr int kDefaultRows = 3;

// Length of item cut to kMaxItemBytes without splitting a UTF-8 sequence.
inline size_t itemLength(const std::string_view item) {
    if (item.size() <= kMaxItemBytes) {
        return item.size();
    }
    size_t n = kMaxItemBytes;
    while (n > 0 && (static_cast<unsigned char>(item[n]) & 0xC0) == 0x80) {
        --n;  // item[n] continues a sequence: cut before its lead byte
    }
    return n;
}

// Appends one item to an options string. A newline in the item (the roller's
// separator) becomes a space. false when the list is full (nothing added).
inline bool appendItem(std::string& options, int& count, const std::string_view item) {
    if (count >= kMaxItems) {
        return false;
    }
    if (count > 0) {
        options.push_back('\n');
    }
    const size_t len = itemLength(item);
    for (size_t i = 0; i < len; ++i) {
        const char c = item[i];
        options.push_back(c == '\n' || c == '\r' ? ' ' : c);
    }
    ++count;
    return true;
}

// Selection for count items: -1 when empty, else clamped to 0..count-1.
inline int clampSelected(const int selected, const int count) {
    if (count <= 0) {
        return -1;
    }
    if (selected < 0) {
        return 0;
    }
    return selected >= count ? count - 1 : selected;
}

inline int clampRows(const int rows) {
    if (rows < kMinRows) {
        return kMinRows;
    }
    return rows > kMaxRows ? kMaxRows : rows;
}

// "rrggbb" (optionally with '#') to 0xRRGGBB; fallback when malformed.
inline unsigned long parseColor(std::string_view hex, const unsigned long fallback) {
    if (!hex.empty() && hex.front() == '#') {
        hex.remove_prefix(1);
    }
    if (hex.size() != 6) {
        return fallback;
    }
    unsigned long value = 0;
    for (const char c : hex) {
        int digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            return fallback;
        }
        value = (value << 4) | static_cast<unsigned long>(digit);
    }
    return value;
}

} // namespace freebutton::list

#endif // FREEBUTTON_LISTLOGIC_H
