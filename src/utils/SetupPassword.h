//
// SetupPassword - the password of the WiFi setup access point (the AP the
// device opens when it has no WiFi). Pure logic: no ESP headers, so the
// native test (test/test_setup_password) includes it directly. The random
// source, the NVS storage and the lifetime are in src/core/WifiManager.cpp.
//
// Form: "XXXXXX-XXXXXX": 12 characters from a 32-character alphabet (60 bits)
// and a '-' in the middle, which is part of the password. No 0/O and no
// 1/I/l, so it can be typed from the display. No character needs escaping in
// a WiFi QR code ("WIFI:T:WPA;S:..;P:..;;"). 13 characters: inside the WPA2
// range of 8..63.
//

#ifndef SETUPPASSWORD_H
#define SETUPPASSWORD_H

#include <stddef.h>
#include <stdint.h>

namespace freebutton {

constexpr size_t kSetupPasswordGroup = 6;                          // random characters per group
constexpr size_t kSetupPasswordLen = 2 * kSetupPasswordGroup + 1;  // two groups and the '-'
constexpr char kSetupPasswordAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

// 32 characters: 5 of the 8 bits of a random byte select one, without bias.
static_assert(sizeof(kSetupPasswordAlphabet) - 1 == 32, "the alphabet must have 32 characters");

// rng() -> uint8_t, uniformly random. out: kSetupPasswordLen + 1 bytes.
template <class Rng>
inline void generateSetupPassword(Rng&& rng, char out[kSetupPasswordLen + 1]) {
    for (size_t i = 0; i < kSetupPasswordLen; ++i) {
        if (i == kSetupPasswordGroup) {
            out[i] = '-';
        } else {
            out[i] = kSetupPasswordAlphabet[static_cast<uint8_t>(rng()) & 31];
        }
    }
    out[kSetupPasswordLen] = '\0';
}

inline bool isSetupPasswordChar(char c) {
    for (size_t i = 0; i < sizeof(kSetupPasswordAlphabet) - 1; ++i) {
        if (kSetupPasswordAlphabet[i] == c) return true;
    }
    return false;
}

// True for a string of exactly the generated form. Reads at most
// kSetupPasswordLen + 1 bytes of s.
inline bool isValidSetupPassword(const char* s) {
    if (!s) return false;
    for (size_t i = 0; i < kSetupPasswordLen; ++i) {
        if (i == kSetupPasswordGroup) {
            if (s[i] != '-') return false;
        } else if (!isSetupPasswordChar(s[i])) {  // also false at an early '\0'
            return false;
        }
    }
    return s[kSetupPasswordLen] == '\0';
}

}  // namespace freebutton

#endif  // SETUPPASSWORD_H
