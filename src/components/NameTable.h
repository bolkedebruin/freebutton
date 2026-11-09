//
// NameTable - names from config.json for display items and keys
//
// config.json may give a display item ("mqttdisplays") or a key ("buttons")
// a "name"; scripts then address it by that name:
//   fb.main.display.setValue("volume", "15%")
//   fb.key("sceneUp").button.onClick(...)
//
// The names live here, in the general heap, never in the JS heap: a script's
// string literal stays in the bytecode (mapped from flash) and the binding
// reads it and looks it up in this table. One packed buffer, one entry per
// named element:
//   [kind:1][len:1][id:4, little endian][name:len]
// so 6 + len bytes per name, nothing for unnamed elements.
//
// Two namespaces (Kind): display item names are unique among display items,
// key names among keys; a key and a display item may share a name.
//
// Written only while config.json loads at boot, before the script runs;
// read-only afterwards, so lookups need no lock. A display item a script
// removes keeps its entry: the id is never reused (auto ids only grow), so
// the name then resolves to an id that no longer exists (setValue returns
// false), and the config writer only writes names of existing items.
//

#ifndef FREEBUTTON_NAMETABLE_H
#define FREEBUTTON_NAMETABLE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace freebutton {

class NameTable {
public:
    enum class Kind : uint8_t { DisplayItem = 0, Key = 1 };
    enum class AddResult : uint8_t { Added, Invalid, Duplicate };

    // Longest name; also what the JS binding's lookup accepts.
    static constexpr size_t kMaxLen = 24;

    // [A-Za-z_][A-Za-z0-9_]*, 1..kMaxLen characters.
    static bool valid(std::string_view name) {
        if (name.empty() || name.size() > kMaxLen) {
            return false;
        }
        for (size_t i = 0; i < name.size(); ++i) {
            const char c = name[i];
            const bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
            const bool digit = c >= '0' && c <= '9';
            if (!(alpha || (digit && i > 0))) {
                return false;
            }
        }
        return true;
    }

    // Adds name -> id. Invalid: the name is not valid(); Duplicate: the name
    // is already used for this kind (the first one stays).
    AddResult add(Kind kind, uint32_t id, std::string_view name) {
        if (!valid(name)) {
            return AddResult::Invalid;
        }
        uint32_t existing = 0;
        if (find(kind, name, existing)) {
            return AddResult::Duplicate;
        }
        buf_.push_back(static_cast<char>(kind));
        buf_.push_back(static_cast<char>(name.size()));
        for (int shift = 0; shift < 32; shift += 8) {
            buf_.push_back(static_cast<char>((id >> shift) & 0xFF));
        }
        buf_.insert(buf_.end(), name.begin(), name.end());
        return AddResult::Added;
    }

    // Id for name, false when the kind has no such name.
    bool find(Kind kind, std::string_view name, uint32_t& id) const {
        for (size_t at = 0; at + kHeader <= buf_.size(); at += kHeader + lenAt(at)) {
            if (kindAt(at) == kind && lenAt(at) == name.size() &&
                std::memcmp(buf_.data() + at + kHeader, name.data(), name.size()) == 0) {
                id = idAt(at);
                return true;
            }
        }
        return false;
    }

    // Name of id (the first one, should an id carry two), empty when none.
    std::string_view nameOf(Kind kind, uint32_t id) const {
        for (size_t at = 0; at + kHeader <= buf_.size(); at += kHeader + lenAt(at)) {
            if (kindAt(at) == kind && idAt(at) == id) {
                return {buf_.data() + at + kHeader, lenAt(at)};
            }
        }
        return {};
    }

    void clear() {
        buf_.clear();
        buf_.shrink_to_fit();
    }

    // After loading: give back the vector's growth slack.
    void compact() { buf_.shrink_to_fit(); }

    size_t bytes() const { return buf_.capacity(); }

private:
    static constexpr size_t kHeader = 6;

    Kind kindAt(size_t at) const { return static_cast<Kind>(buf_[at]); }
    size_t lenAt(size_t at) const { return static_cast<uint8_t>(buf_[at + 1]); }
    uint32_t idAt(size_t at) const {
        uint32_t id = 0;
        for (int i = 0; i < 4; ++i) {
            id |= static_cast<uint32_t>(static_cast<uint8_t>(buf_[at + 2 + i])) << (8 * i);
        }
        return id;
    }

    std::vector<char> buf_;
};

} // namespace freebutton

#endif // FREEBUTTON_NAMETABLE_H
