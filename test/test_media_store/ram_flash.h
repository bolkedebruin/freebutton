// A NOR flash in RAM for the media store tests: programming can only clear
// bits and writing a byte that is not erased counts as a violation, erase
// sets 0xFF, and a power cut can be injected after N write/erase operations
// or N programmed bytes (the cut operation is torn: part of it happens).
#pragma once

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "../../src/media/media_core.h"

struct RamFlash {
    std::vector<uint8_t> mem;
    media_flash_t flash{};

    // fault injection: <0 = off
    long ops_left = -1;
    long bytes_left = -1;
    bool power_off = false; // after a cut every operation fails until revive()
    bool hit = false;       // a cut happened
    std::mt19937 rng{12345};

    // checks
    unsigned violations = 0;     // bytes programmed over non-erased flash
    unsigned map_violations = 0; // flash of a multi-page mapping changed while mapped
    unsigned writes = 0, erases = 0, header_writes = 0, maps_live = 0;

    struct Map {
        std::vector<uint8_t> pages;
        std::vector<uint8_t> copy; // non-contiguous: a copy (checked on unmap)
        bool exclusive = false;    // more than one page: a large file, nobody may touch it
    };

    explicit RamFlash(uint32_t pages) : mem(pages * MEDIA_PAGE_SIZE, 0xFF) {
        flash.ctx = this;
        flash.size = pages * MEDIA_PAGE_SIZE;
        flash.read = [](void *c, uint32_t off, void *dst, uint32_t len) {
            return static_cast<RamFlash *>(c)->read(off, dst, len);
        };
        flash.write = [](void *c, uint32_t off, const void *src, uint32_t len) {
            return static_cast<RamFlash *>(c)->write(off, static_cast<const uint8_t *>(src), len);
        };
        flash.erase = [](void *c, uint32_t off, uint32_t len) { return static_cast<RamFlash *>(c)->erase(off, len); };
        flash.map = [](void *c, const uint8_t *pages, uint32_t n, const void **out, void **handle) {
            return static_cast<RamFlash *>(c)->map(pages, n, out, handle);
        };
        flash.unmap = [](void *c, void *handle) { static_cast<RamFlash *>(c)->unmap(handle); };
    }
    RamFlash(const RamFlash &) = delete;
    RamFlash &operator=(const RamFlash &) = delete;
    ~RamFlash() {
        for (Map *m : maps) delete m;
    }

    void cut_after_ops(long n) {
        ops_left = n;
        bytes_left = -1;
        hit = false;
    }
    void cut_after_bytes(long n) {
        bytes_left = n;
        ops_left = -1;
        hit = false;
    }
    void revive() {
        power_off = false;
        ops_left = -1;
        bytes_left = -1;
    }

    int read(uint32_t off, void *dst, uint32_t len) {
        if (power_off || off + len > mem.size()) return -1;
        memcpy(dst, mem.data() + off, len);
        return 0;
    }

    bool touches_exclusive(uint32_t off, uint32_t len) const {
        for (const Map *m : maps) {
            if (!m->exclusive) continue;
            for (uint8_t p : m->pages) {
                const uint32_t a = p * MEDIA_PAGE_SIZE, b = a + MEDIA_PAGE_SIZE;
                if (off < b && off + len > a) return true;
            }
        }
        return false;
    }

    void program(uint32_t off, const uint8_t *src, uint32_t len) {
        for (uint32_t i = 0; i < len; i++) {
            if (mem[off + i] != 0xFF) violations++;
            mem[off + i] &= src[i];
        }
    }

    void cut() {
        power_off = true;
        hit = true;
        ops_left = bytes_left = -1;
    }

    int write(uint32_t off, const uint8_t *src, uint32_t len) {
        if (power_off || off + len > mem.size()) return -1;
        if (touches_exclusive(off, len)) map_violations++;
        writes++;
        if (len == 32 && off % MEDIA_PAGE_SIZE == 0 && off < MEDIA_DIR_PAGES * MEDIA_PAGE_SIZE) header_writes++;
        uint32_t keep = len;
        bool tear = false;
        if (ops_left == 0) {
            keep = len ? rng() % len : 0;
            tear = true;
        } else if (ops_left > 0) {
            ops_left--;
        }
        if (bytes_left >= 0 && (long)len > bytes_left) {
            if (!tear || (uint32_t)bytes_left < keep) keep = (uint32_t)bytes_left;
            tear = true;
        } else if (bytes_left > 0) {
            bytes_left -= len;
        }
        program(off, src, keep);
        if (tear) {
            if (keep < len) { // the next byte half programmed
                const uint8_t b = (uint8_t)(src[keep] | (rng() & 0xFF));
                if (mem[off + keep] != 0xFF) violations++;
                mem[off + keep] &= b;
            }
            cut();
            return -1;
        }
        return 0;
    }

    int erase(uint32_t off, uint32_t len) {
        if (power_off || off % MEDIA_SECTOR_SIZE || len % MEDIA_SECTOR_SIZE || off + len > mem.size()) return -1;
        if (touches_exclusive(off, len)) map_violations++;
        erases++;
        if (ops_left == 0) {
            const uint32_t sectors = len / MEDIA_SECTOR_SIZE;
            const uint32_t done = rng() % sectors;
            memset(mem.data() + off, 0xFF, done * MEDIA_SECTOR_SIZE);
            uint8_t *s = mem.data() + off + done * MEDIA_SECTOR_SIZE;
            for (uint32_t i = 0; i < MEDIA_SECTOR_SIZE; i++)
                if (rng() & 1) s[i] = 0xFF;
            cut();
            return -1;
        }
        if (ops_left > 0) ops_left--;
        memset(mem.data() + off, 0xFF, len);
        return 0;
    }

    std::vector<Map *> maps;

    int map(const uint8_t *pages, uint32_t n, const void **out, void **handle) {
        if (power_off || n == 0) return -1;
        Map *m = new Map;
        m->pages.assign(pages, pages + n);
        m->exclusive = n > 1;
        bool contiguous = true;
        for (uint32_t i = 1; i < n; i++) contiguous = contiguous && pages[i] == pages[0] + i;
        if (contiguous) {
            *out = mem.data() + pages[0] * MEDIA_PAGE_SIZE;
        } else {
            m->copy.resize(n * MEDIA_PAGE_SIZE);
            for (uint32_t i = 0; i < n; i++)
                memcpy(m->copy.data() + i * MEDIA_PAGE_SIZE, mem.data() + pages[i] * MEDIA_PAGE_SIZE, MEDIA_PAGE_SIZE);
            *out = m->copy.data();
        }
        maps.push_back(m);
        maps_live++;
        *handle = m;
        return 0;
    }

    void unmap(void *handle) {
        Map *m = static_cast<Map *>(handle);
        for (size_t i = 0; i < maps.size(); i++) {
            if (maps[i] != m) continue;
            if (!m->copy.empty()) {
                for (size_t k = 0; k < m->pages.size(); k++)
                    if (memcmp(m->copy.data() + k * MEDIA_PAGE_SIZE, mem.data() + m->pages[k] * MEDIA_PAGE_SIZE,
                               MEDIA_PAGE_SIZE))
                        map_violations++;
            }
            maps.erase(maps.begin() + (long)i);
            maps_live--;
            delete m;
            return;
        }
    }
};
