//
// Which of the web IDE's files (LittleFS /www/) a browser may keep.
//
#ifndef FREEBUTTON_STATICCACHE_H
#define FREEBUTTON_STATICCACHE_H

#include <Arduino.h>

namespace freebutton {

// "/assets/ide-BTquvnIq.js": a file whose name carries a hash of its content
// (the bundler's "-<8 characters>" before the extension). It never changes,
// so a browser may keep it for good. A hash of lower-case letters only is
// not recognised and is then checked again on every load, which is safe.
inline bool isHashedAssetUrl(const String& url) {
    if (!url.startsWith("/assets/")) {
        return false;
    }
    const int dot = url.lastIndexOf('.');
    constexpr int kHashLen = 8;
    if (dot < kHashLen + 1 || url.charAt(dot - kHashLen - 1) != '-') {
        return false;
    }
    // A hash has capitals or digits; a word such as "-compiler" has not
    // ("mquickjs-compiler.js" is not hashed and does change).
    bool mixed = false;
    for (int i = dot - kHashLen; i < dot; ++i) {
        const char c = url.charAt(i);
        const bool lower = c >= 'a' && c <= 'z';
        const bool other = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!lower && !other) {
            return false;
        }
        mixed = mixed || other;
    }
    return mixed;
}

// The IDE's own web fonts (/fonts/*.woff2 in /www/): replaced only with an
// IDE update.
inline bool isWebFontUrl(const String& url) {
    return url.startsWith("/fonts/") && url.endsWith(".woff2");
}

} // namespace freebutton

#endif // FREEBUTTON_STATICCACHE_H
