//
// HaSlug - approximation of Home Assistant's slugify() (python-slugify with
// separator "_") for building entity IDs from a device's friendly name.
//
// Pure, no ESP dependencies: unit-tested on the host (test/test_ha_slug).
//
// Rules:
//  - ASCII letters are lowercased; ASCII [a-z0-9] are kept.
//  - Latin-1 Supplement letters (U+00C0..U+00FF) are transliterated the way
//    unidecode does (é->e, ü->u, ø->o, æ->ae, ß->ss, þ->th, ...), plus ĳ->ij.
//  - Every other byte / code point run becomes a single "_".
//  - Repeated "_" collapse; leading/trailing "_" are trimmed.
//

#ifndef FREEBUTTON_HASLUG_H
#define FREEBUTTON_HASLUG_H

#include <cstddef>
#include <string>
#include <string_view>

std::string haSlugify(std::string_view utf8);

// Heap-free variant: writes the slug NUL-terminated into `out` and returns
// its length. A slug longer than capacity - 1 is cut (never ending in "_").
size_t haSlugify(std::string_view utf8, char* out, size_t capacity);

#endif //FREEBUTTON_HASLUG_H
