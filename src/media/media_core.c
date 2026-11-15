// Media store core (see media_core.h). On-flash format and the reasoning
// behind it: docs/MEDIA-STORE.md, "On-flash format".
//
// RAM. Static (the state `g`): the open-file table, the mapping table and
// two upload slots. Heap: 6 bytes per page (sectors in use, sectors known
// erased, wear) and the index, one uint16 per live entry (its offset in the
// active directory half, sorted by bucket and key) in a block that grows in
// steps of 16 entries. The records themselves are read in place: the active
// directory half stays mapped. Numbers: docs/MEDIA-STORE.md.
#include "media_core.h"

#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "esp_log.h"
static const char *TAG = "media";
#define LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#elif defined(MEDIA_HOST_LOG)
#include <stdio.h>
#define LOGE(...) (fprintf(stderr, "media: " __VA_ARGS__), fputc('\n', stderr))
#define LOGW(...) (fprintf(stderr, "media: " __VA_ARGS__), fputc('\n', stderr))
#else
#define LOGE(...) ((void)0)
#define LOGW(...) ((void)0)
#endif

#define PAGE MEDIA_PAGE_SIZE
#define SECTOR MEDIA_SECTOR_SIZE
#define SPP 16u /* sectors per page */
#define HALF PAGE

#define DIR_MAGIC 0x5249444Du /* "MDIR" */
#define DIR_VERSION 1u
#define HDR_LEN 32u
#define REC_PUT 0x01u
#define REC_DEL 0x02u
#define REC_FIXED 36u
#define REC_MAX 256u
#define DEL_LEN 16u
#define KILL_NONE 0xFFFFu
#define DIR_MAX_RECORDS 1024u
#define DIR_LIVE_MAX (48u * 1024u)   /* live records; leaves 16 KB of log per snapshot */
/* Directory snapshot when idle (media_maintain), see dir_snapshot_due(). */
#define DIR_IDLE_DEAD_MIN 64u         /* dead records, at least */
#define DIR_IDLE_DEAD_RATIO 2u        /* and at least this many per live entry */
#define DIR_IDLE_TAIL_RECORDS 8u      /* or (with the minimum above) fewer record slots than this left */
#define DIR_IDLE_TAIL (8u * REC_MAX)  /* or less log than this left (bytes) ... */
#define DIR_IDLE_GAIN (8u * 1024u)    /* ... of which a snapshot wins back at least this much */

/* The index block grows by this many entries (32 bytes): see idx_reserve(). */
#define IDX_STEP 16u

/* Heap: the free map (once, at mount) and the index. The native tests put a
 * failing allocator here. */
#ifndef MEDIA_ALLOC
#define MEDIA_ALLOC(n) malloc(n)
#define MEDIA_FREE(p) free(p)
#endif

#ifdef MEDIA_CORE_STATS
media_core_stats_t media_core_stats;
#define COUNT(field, n) (media_core_stats.field += (n))
#else
#define COUNT(field, n) ((void)0)
#endif

enum { LOC_EMPTY = 0, LOC_SECTORS = 1, LOC_PAGES = 2 };

typedef struct {
    uint8_t kind;
    uint8_t page, first, nsec; /* LOC_SECTORS */
    uint8_t npages;            /* LOC_PAGES */
    uint8_t pages[MEDIA_MAX_FILE_PAGES];
} loc_t;

typedef struct {
    uint8_t type, bucket, keylen;
    uint16_t len, kill;
    uint32_t seq, size, mtime;
    uint8_t md5[16];
    loc_t loc;
    char key[MEDIA_KEY_MAX];
} rec_t;

typedef struct {
    uint32_t seq, size;
    const uint8_t *data;
    uint32_t gen;
    uint16_t refs;
    uint8_t used, dead, map;
    uint8_t kind, page, first, nsec; /* where it lies, to free it on last close */
    uint8_t pagebits[MEDIA_MAX_PAGES / 8];
} open_t;

typedef struct {
    const void *ptr;
    void *handle;
    uint16_t refs;
    uint16_t page; /* shared page this maps, or NO_PAGE for a large file */
} map_t;
#define NO_PAGE 0xFFFFu
#define NO_MAP 0xFFu

struct media_writer {
    uint8_t used, bucket, keylen;
    uint16_t reclen;
    uint32_t size, mtime, written;
    loc_t loc;
    char key[MEDIA_KEY_MAX];
};

static struct {
    const media_flash_t *f;
    bool mounted;
    uint16_t npages;
    uint8_t active;        /* directory half in use */
    bool half_erased[2];   /* known blank */
    bool stale;            /* the other half has to be erased before reuse */
    bool dirty;            /* log tail not trustworthy: snapshot before appending */
    /* An append failed after its write was tried and the snapshot that
     * drops the record failed too (the record may be valid on flash while
     * RAM does not count it), or the free map could not be restored. Until
     * a snapshot succeeds (it rebuilds the free map): the space of a failed
     * commit stays in use, nothing is allocated, media_maintain rests. */
    bool unsettled;
    bool idle_failed;      /* the idle snapshot failed: not again before the directory changes */
    bool compact_dirty;    /* something was freed since the last compaction search */
    uint32_t gen, next_seq, end;
    uint16_t nrec;
    uint32_t live_bytes;
    uint32_t open_gen;
    /* The active half, mapped: every directory read after the headers goes
     * through it. */
    const uint8_t *dir;
    void *dir_handle;
    /* The index: byte offsets (in the active half) of the live PUT records,
     * sorted by (bucket, key). Built at mount and after a snapshot, updated
     * by every append. A heap block of idx_cap entries, sized to what the
     * store holds (idx_reserve). */
    uint16_t nidx; /* live entries */
    uint16_t idx_cap;
    uint16_t *idx;
    uint16_t *used;   /* per page: sectors in use (live, open, reserved) */
    uint16_t *erased; /* per page: sectors known to be 0xFF */
    /* Wear leveling (RAM only, see "Allocation" in docs/MEDIA-STORE.md):
     * large files take the next free pages from `cursor` on (next-fit, like
     * the littlefs block allocator), small files are appended to `fill`. */
    uint16_t cursor;       /* page where the next large allocation starts looking */
    uint16_t fill;         /* page small files are appended to, NO_PAGE: none yet */
    uint8_t fill_sec;      /* next sector of `fill` */
    bool hint_fresh;       /* the last alloc_small() took a fresh page */
    bool compact_wanted;   /* a large reservation failed: compact even with free pages */
    uint32_t erase_sectors; /* sectors erased since mount (a page erase counts 16) */
    uint16_t *wear;         /* per page: sectors erased since mount (saturates) */
    open_t open[MEDIA_MAX_OPEN];
    map_t maps[MEDIA_MAX_OPEN];
    media_writer_t writers[MEDIA_MAX_WRITERS];
} g;

/* ---------------------------------------------------------------- helpers */

static void put16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
    put16(p, v);
    put16(p + 2, v >> 16);
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return get16(p) | ((uint32_t)get16(p + 2) << 16); }

/* CRC-32 (IEEE 802.3, reflected, as zlib), nibble table. */
static uint32_t crc32_calc(const uint8_t *p, size_t n) {
    static const uint32_t t[16] = {0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
                                   0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
                                   0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
                                   0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    uint32_t c = 0xFFFFFFFFu;
    COUNT(crc_bytes, n);
    while (n--) {
        c ^= *p++;
        c = (c >> 4) ^ t[c & 15];
        c = (c >> 4) ^ t[c & 15];
    }
    return ~c;
}

static unsigned popcount16(uint16_t v) {
    unsigned n = 0;
    for (; v; v &= (uint16_t)(v - 1)) n++;
    return n;
}

static uint16_t run_mask(unsigned first, unsigned n) { return (uint16_t)(((1u << n) - 1u) << first); }

static void lock(void) {
    if (g.f && g.f->lock) g.f->lock(g.f->ctx);
}
static void unlock(void) {
    if (g.f && g.f->unlock) g.f->unlock(g.f->ctx);
}

static int fl_read(uint32_t off, void *dst, uint32_t len) { return g.f->read(g.f->ctx, off, dst, len); }
static int fl_write(uint32_t off, const void *src, uint32_t len) { return g.f->write(g.f->ctx, off, src, len); }
static int fl_erase(uint32_t off, uint32_t len) {
    const int e = g.f->erase(g.f->ctx, off, len);
    if (e == 0) {
        for (uint32_t s = off / SECTOR; s < (off + len) / SECTOR; s++) {
            g.erase_sectors++;
            if (g.wear[s / SPP] != 0xFFFFu) g.wear[s / SPP]++;
        }
    }
    return e;
}

/* Mount only: one bit per record of the half, set when the record is dead
 * (128 bytes on the stack). At run time the index says what is live. */
static bool is_dead(const uint8_t *dead, uint16_t o) { return (dead[o >> 3] >> (o & 7)) & 1; }
static void set_dead(uint8_t *dead, uint16_t o) { dead[o >> 3] |= (uint8_t)(1u << (o & 7)); }

static bool key_ok(int bucket, const char *key, uint8_t *kl) {
    if (bucket < 0 || bucket >= MEDIA_BUCKET_COUNT || !key) return false;
    size_t n = 0;
    for (; key[n]; n++) {
        if (n >= MEDIA_KEY_MAX) return false;
        const uint8_t c = (uint8_t)key[n];
        if (c < 0x20 || c == 0x7F) return false;
    }
    if (n == 0) return false;
    *kl = (uint8_t)n;
    return true;
}

static int key_cmp(const char *a, size_t al, const char *b, size_t bl) {
    const int c = memcmp(a, b, al < bl ? al : bl);
    if (c) return c;
    return al < bl ? -1 : al > bl ? 1 : 0;
}

static void loc_for_size(uint32_t size, loc_t *l) {
    memset(l, 0, sizeof *l);
    if (size == 0) {
        l->kind = LOC_EMPTY;
    } else if (size <= MEDIA_SMALL_MAX) {
        l->kind = LOC_SECTORS;
        l->nsec = (uint8_t)((size + SECTOR - 1) / SECTOR);
    } else {
        l->kind = LOC_PAGES;
        l->npages = (uint8_t)((size + PAGE - 1) / PAGE);
    }
}

static uint32_t loc_bytes(const loc_t *l) {
    return l->kind == LOC_SECTORS ? 4u : l->kind == LOC_PAGES ? l->npages : 0u;
}

static uint32_t loc_alloc(const loc_t *l) {
    return l->kind == LOC_SECTORS ? l->nsec * SECTOR : l->kind == LOC_PAGES ? l->npages * PAGE : 0u;
}

static uint16_t put_len(uint8_t keylen, const loc_t *l) {
    return (uint16_t)(((REC_FIXED + loc_bytes(l) + keylen + 3u) & ~3u) + 4u);
}

static void loc_mark(const loc_t *l, bool used) {
    if (l->kind == LOC_SECTORS) {
        const uint16_t m = run_mask(l->first, l->nsec);
        if (used) g.used[l->page] |= m;
        else g.used[l->page] &= (uint16_t)~m;
    } else if (l->kind == LOC_PAGES) {
        for (unsigned i = 0; i < l->npages; i++) g.used[l->pages[i]] = used ? 0xFFFFu : 0u;
    }
    if (!used) g.compact_dirty = true;
}

static bool loc_free(const loc_t *l) {
    if (l->kind == LOC_SECTORS) return (g.used[l->page] & run_mask(l->first, l->nsec)) == 0;
    if (l->kind == LOC_PAGES) {
        for (unsigned i = 0; i < l->npages; i++)
            if (g.used[l->pages[i]]) return false;
    }
    return true;
}

/* ------------------------------------------------------------ records */

static bool rec_crc_ok(const uint8_t *p, uint32_t len) { return get32(p + len - 4) == crc32_calc(p, len - 4); }

/* Decode and validate the fields. The CRC is not checked here: mount checks
 * it once (rec_crc_ok), an append is compared with what was written. */
static int rec_decode(const uint8_t *p, uint32_t len, rec_t *r) {
    COUNT(rec_decodes, 1);
    memset(r, 0, sizeof *r);
    r->type = p[0];
    r->bucket = p[1];
    r->len = (uint16_t)len;
    r->seq = get32(p + 4);
    r->kill = get16(p + 8);
    if (r->type == REC_DEL) return len == DEL_LEN && r->kill != KILL_NONE ? 0 : -1;
    if (r->type != REC_PUT || len < REC_FIXED + 8u) return -1;
    if (r->bucket >= MEDIA_BUCKET_COUNT) return -1;
    r->keylen = p[10];
    r->size = get32(p + 12);
    r->mtime = get32(p + 16);
    memcpy(r->md5, p + 20, 16);
    if (r->keylen == 0 || r->keylen > MEDIA_KEY_MAX || r->size > MEDIA_FILE_MAX) return -1;
    loc_t *l = &r->loc;
    loc_for_size(r->size, l);
    if (p[11] != l->kind) return -1;
    uint32_t off = REC_FIXED;
    if (l->kind == LOC_SECTORS) {
        l->page = p[36];
        l->first = p[37];
        if (p[38] != l->nsec || l->first + l->nsec > SPP) return -1;
        if (l->page < MEDIA_DIR_PAGES || l->page >= g.npages) return -1;
        off += 4;
    } else if (l->kind == LOC_PAGES) {
        if (off + l->npages > len) return -1;
        for (unsigned i = 0; i < l->npages; i++) {
            l->pages[i] = p[off + i];
            if (l->pages[i] < MEDIA_DIR_PAGES || l->pages[i] >= g.npages) return -1;
        }
        off += l->npages;
    }
    if (len != put_len(r->keylen, l) || off + r->keylen > len) return -1;
    memcpy(r->key, p + off, r->keylen);
    return 0;
}

static uint16_t rec_encode(const rec_t *r, uint8_t *buf) {
    const uint16_t len = r->type == REC_DEL ? (uint16_t)DEL_LEN : put_len(r->keylen, &r->loc);
    memset(buf, 0, len);
    buf[0] = r->type;
    buf[1] = r->bucket;
    put16(buf + 2, len);
    put32(buf + 4, r->seq);
    put16(buf + 8, r->kill);
    if (r->type == REC_PUT) {
        const loc_t *l = &r->loc;
        buf[10] = r->keylen;
        buf[11] = l->kind;
        put32(buf + 12, r->size);
        put32(buf + 16, r->mtime);
        memcpy(buf + 20, r->md5, 16);
        uint32_t off = REC_FIXED;
        if (l->kind == LOC_SECTORS) {
            buf[36] = l->page;
            buf[37] = l->first;
            buf[38] = l->nsec;
            off += 4;
        } else if (l->kind == LOC_PAGES) {
            memcpy(buf + off, l->pages, l->npages);
            off += l->npages;
        }
        memcpy(buf + off, r->key, r->keylen);
    }
    put32(buf + len - 4, crc32_calc(buf, len - 4u));
    return len;
}

/* --------------------------------------------------- directory reading */

/* The records of the active half are read in place through g.dir. After
 * mount the log is trusted: no CRC, and only what is needed is decoded. */

/* The record that starts at off: 1 and its length, 0 end of log, -2 no
 * record can start here (torn). */
static int rec_frame(uint32_t off, uint32_t *len) {
    if (off + 4 > HALF) return 0;
    const uint8_t *p = g.dir + off;
    if (get32(p) == 0xFFFFFFFFu) return 0;
    const uint32_t n = get16(p + 2);
    if (n < DEL_LEN || n > REC_MAX || (n & 3u) || off + n > HALF) return -2;
    *len = n;
    return 1;
}

/* The length of the record at off; 0 when the log has no record there (a
 * damaged log: the caller stops). Never reads outside the log. */
static uint32_t rec_len(uint32_t off) {
    if (off < HDR_LEN || off + DEL_LEN > g.end) return 0;
    const uint32_t n = get16(g.dir + off + 2);
    return n < DEL_LEN || n > REC_MAX || (n & 3u) || off + n > g.end ? 0 : n;
}

/* The whole record at off: 0 ok. */
static int rec_load(uint32_t off, rec_t *r) {
    const uint32_t len = rec_len(off);
    return len ? rec_decode(g.dir + off, len, r) : -1;
}

/* The key of the PUT record at off: it follows the location. A key that
 * would not lie inside the log (damaged record) is given as empty. */
static const char *rec_key(uint32_t off, size_t *keylen) {
    const uint8_t *p = g.dir + off;
    uint32_t k = REC_FIXED;
    if (p[11] == LOC_SECTORS) k += 4;
    else if (p[11] == LOC_PAGES) k += (get32(p + 12) + PAGE - 1) / PAGE;
    if (off + k + p[10] > g.end) {
        *keylen = 0;
        return (const char *)p;
    }
    *keylen = p[10];
    return (const char *)p + k;
}

/* The PUT record at off against (bucket, key): < 0, 0, > 0. */
static int rec_cmp(uint32_t off, uint8_t bucket, const char *key, size_t keylen) {
    COUNT(key_cmps, 1);
    const uint8_t b = g.dir[off + 1];
    if (b != bucket) return b < bucket ? -1 : 1;
    size_t kl;
    const char *k = rec_key(off, &kl);
    return key_cmp(k, kl, key, keylen);
}

/* The ordinal of the record at off: a walk over the record lengths. Only an
 * append that kills a record needs it. 0 ok, -1 the log is damaged. */
static int ord_of(uint32_t off, uint16_t *ord) {
    uint16_t o = 0;
    uint32_t p = HDR_LEN;
    while (p < off) {
        const uint32_t len = rec_len(p);
        if (!len) return -1;
        p += len;
        o++;
    }
    if (p != off) return -1;
    *ord = o;
    return 0;
}

/* ------------------------------------------------------------- the index */

/* The first index position whose entry is >= (bucket, key); > with `upper`.
 * *equal (may be NULL): an entry with this bucket and key exists (without
 * `upper` it is the one at the position returned). */
static uint16_t idx_search(uint8_t bucket, const char *key, size_t keylen, bool upper, bool *equal) {
    uint16_t lo = 0, hi = g.nidx;
    bool eq = false;
    while (lo < hi) {
        const uint16_t mid = (uint16_t)((lo + hi) / 2u);
        const int c = rec_cmp(g.idx[mid], bucket, key, keylen);
        if (c == 0) eq = true;
        if (c < 0 || (upper && c == 0)) lo = (uint16_t)(mid + 1u);
        else hi = mid;
    }
    if (equal) *equal = eq;
    return lo;
}

/* The PUT record at off (len bytes) is live. A key is live once; should a
 * damaged log hold it twice, the one earlier in the log comes first and is
 * the one a lookup finds. */
static void idx_insert(uint32_t off, uint32_t len) {
    size_t kl;
    const char *k = rec_key(off, &kl);
    const uint16_t i = idx_search(g.dir[off + 1], k, kl, true, NULL);
    memmove(&g.idx[i + 1], &g.idx[i], (size_t)(g.nidx - i) * sizeof g.idx[0]);
    g.idx[i] = (uint16_t)off;
    g.nidx++;
    g.live_bytes += len;
}

/* The record at off is no longer live. */
static void idx_remove(uint32_t off) {
    for (uint16_t i = 0; i < g.nidx; i++) {
        if (g.idx[i] != off) continue;
        memmove(&g.idx[i], &g.idx[i + 1], (size_t)(g.nidx - i - 1u) * sizeof g.idx[0]);
        g.nidx--;
        g.live_bytes -= rec_len(off);
        return;
    }
}

/* The record at off is in the index (it is a live PUT). A scan of the
 * offsets: used by the walks in log order (snapshot, compaction). */
static bool idx_has(uint32_t off) {
    for (uint16_t i = 0; i < g.nidx; i++)
        if (g.idx[i] == off) return true;
    return false;
}

/* Give the index block `cap` entries (>= nidx): 0 ok, -1 no memory, and
 * then nothing has changed. */
static int idx_resize(unsigned cap) {
    uint16_t *nu = (uint16_t *)MEDIA_ALLOC(cap * sizeof *nu);
    if (!nu) return -1;
    if (g.nidx) memcpy(nu, g.idx, g.nidx * sizeof *nu);
    if (g.idx) MEDIA_FREE(g.idx);
    g.idx = nu;
    g.idx_cap = (uint16_t)cap;
    return 0;
}

/* Room for n entries: 0 ok, -1 no memory (nothing has changed). The block
 * is n rounded up to IDX_STEP entries. A step of 16 entries is 32 bytes:
 * a grow is an allocate, copy, free of a block this small, once per 16 new
 * files, and the unused tail is 30 bytes at most; a smaller step would pay
 * the allocator's header and rounding (4 to 8 bytes per block) and a
 * grow more often for less than that. Called before anything is written
 * for the operation that needs the entry. */
static int idx_reserve(unsigned n) {
    if (n <= g.idx_cap) return 0;
    if (n > DIR_MAX_RECORDS) return -1;
    return idx_resize((n + IDX_STEP - 1u) / IDX_STEP * IDX_STEP); /* DIR_MAX_RECORDS is a multiple */
}

/* After a snapshot: give back the steps the index no longer needs (entries
 * were deleted). Keeps an entry for every upload in progress. When there is
 * no memory for the smaller block the larger one stays. */
static void idx_trim(void) {
    unsigned need = g.nidx ? g.nidx : 1u;
    for (int i = 0; i < MEDIA_MAX_WRITERS; i++) need += g.writers[i].used != 0;
    const unsigned cap = (need + IDX_STEP - 1u) / IDX_STEP * IDX_STEP;
    if (cap < g.idx_cap) (void)idx_resize(cap);
}

/* The index of the active half: a walk over the record lengths and one
 * binary insertion per live entry. `dead`: the liveness bitmap of mount;
 * NULL after a snapshot, where every record is a live PUT. 0 ok, -1 the log
 * is damaged (the index then holds the entries before the damage), -2 no
 * memory for the index (mount only: a snapshot does not add entries). */
static int idx_build(const uint8_t *dead) {
    unsigned live = 0;
    uint32_t off = HDR_LEN;
    for (uint16_t o = 0; o < g.nrec; o++) {
        const uint32_t len = rec_len(off);
        if (!len) break;
        live += g.dir[off] == REC_PUT && !(dead && is_dead(dead, o));
        off += len;
    }
    g.nidx = 0;
    g.live_bytes = 0;
    if (idx_reserve(live ? live : 1u)) return -2;
    off = HDR_LEN;
    for (uint16_t o = 0; o < g.nrec; o++) {
        const uint32_t len = rec_len(off);
        if (!len) return -1;
        if (g.dir[off] == REC_PUT && !(dead && is_dead(dead, o))) idx_insert(off, len);
        off += len;
    }
    return 0;
}

/* The live entry (bucket, key): 1 found (*off: where its record is), 0 not
 * found, -1 unreadable. */
static int dir_find(uint8_t bucket, const char *key, uint8_t keylen, rec_t *r, uint32_t *off) {
    bool equal;
    const uint16_t i = idx_search(bucket, key, keylen, false, &equal);
    if (!equal) return 0;
    if (rec_load(g.idx[i], r)) return -1;
    *off = g.idx[i];
    return 1;
}

/* Map directory half `half` (nothing is mapped). */
static int dir_map(uint8_t half) {
    const uint8_t page = half;
    const void *ptr = NULL;
    void *handle = NULL;
    if (g.f->map(g.f->ctx, &page, 1, &ptr, &handle)) return -1;
    g.dir = (const uint8_t *)ptr;
    g.dir_handle = handle;
    return 0;
}

static void dir_unmap(void) {
    if (g.dir) g.f->unmap(g.f->ctx, g.dir_handle);
    g.dir = NULL;
    g.dir_handle = NULL;
}

/* The active half is mapped. It is not after a mapping failed in
 * dir_readback(): every operation that reads the directory starts here and
 * tries again. */
static bool dir_ok(void) { return g.dir || dir_map(g.active) == 0; }

typedef struct {
    bool valid;
    uint32_t gen, next_seq;
} hdr_t;

static int hdr_read(uint8_t half, hdr_t *h) {
    uint8_t b[HDR_LEN];
    memset(h, 0, sizeof *h);
    if (fl_read(half * HALF, b, HDR_LEN)) return -1;
    h->valid = get32(b) == DIR_MAGIC && get16(b + 4) == DIR_VERSION && get16(b + 6) == HDR_LEN &&
               get32(b + 28) == crc32_calc(b, 28);
    h->gen = get32(b + 8);
    h->next_seq = get32(b + 12);
    return 0;
}

static int hdr_write(uint8_t half, uint32_t gen, uint32_t next_seq) {
    uint8_t b[HDR_LEN];
    memset(b, 0, sizeof b);
    put32(b, DIR_MAGIC);
    put16(b + 4, DIR_VERSION);
    put16(b + 6, HDR_LEN);
    put32(b + 8, gen);
    put32(b + 12, next_seq);
    put16(b + 16, g.npages);
    put16(b + 18, MEDIA_DIR_PAGES);
    put32(b + 28, crc32_calc(b, 28));
    return fl_write(half * HALF, b, HDR_LEN);
}

/* 1 blank, 0 not blank, -1 io */
static int verify_blank(uint32_t off, uint32_t len) {
    uint8_t buf[256];
    while (len) {
        const uint32_t n = len < sizeof buf ? len : (uint32_t)sizeof buf;
        if (fl_read(off, buf, n)) return -1;
        for (uint32_t i = 0; i < n; i++)
            if (buf[i] != 0xFF) return 0;
        off += n;
        len -= n;
    }
    return 1;
}

/* --------------------------------------------------- directory writing */

/* The n records just written into the half now mapped, [HDR_LEN, end):
 * every one framed and its CRC right. They become the log that is trusted. */
static bool dir_verify(uint32_t end, uint16_t n) {
    uint32_t off = HDR_LEN;
    for (uint16_t i = 0; i < n; i++) {
        uint32_t len = 0;
        if (off >= end || rec_frame(off, &len) != 1 || off + len > end || !rec_crc_ok(g.dir + off, len)) return false;
        off += len;
    }
    return off == end;
}

static void open_mark(const open_t *o, bool used);

/* The free map from what owns space: the live entries, the open files (a
 * dead one keeps its space until the last close) and the uploads in
 * progress. *r is scratch. 0 ok. */
static int map_rebuild(rec_t *r) {
    for (unsigned p = MEDIA_DIR_PAGES; p < g.npages; p++) g.used[p] = 0;
    for (uint16_t i = 0; i < g.nidx; i++) {
        if (rec_load(g.idx[i], r)) return -1;
        loc_mark(&r->loc, true);
    }
    for (int i = 0; i < MEDIA_MAX_OPEN; i++)
        if (g.open[i].used) open_mark(&g.open[i], true);
    for (int i = 0; i < MEDIA_MAX_WRITERS; i++)
        if (g.writers[i].used) loc_mark(&g.writers[i].loc, true);
    g.compact_dirty = true;
    return 0;
}

/* Copy the live records into the other half, then write its header (the
 * commit point), then switch to it. *kill_off (may be NULL; the offset of a
 * record in the old half, 0 = none) is translated to the new half.
 *
 * The records are checked through the mapping before the header is written.
 * After the header write the header is read back, whatever the write
 * returned: when it is on flash the snapshot is committed (a write that
 * reports an error after programming everything must not leave RAM on the
 * old half while a reboot would take the new one). When it cannot be read,
 * which half a reboot takes is not known: RAM stays on the old half, dirty,
 * so nothing is appended to it before the next snapshot, and both halves
 * hold the same entries. */
static media_err_t dir_snapshot(uint32_t *kill_off) {
    if (!dir_ok()) return MEDIA_ERR_IO;
    const uint8_t old = g.active, nu = (uint8_t)(old ^ 1u);
    const uint32_t base = nu * HALF;
    if (!g.half_erased[nu]) {
        if (fl_erase(base, HALF)) return MEDIA_ERR_IO;
    }
    g.half_erased[nu] = false;
    rec_t r;
    uint8_t buf[REC_MAX];
    uint32_t pos = HDR_LEN, off = HDR_LEN, newkill = 0;
    uint16_t n = 0;
    for (uint16_t o = 0; o < g.nrec; o++) {
        const uint32_t at = off, rlen = rec_len(at);
        if (!rlen) return MEDIA_ERR_IO;
        off += rlen;
        if (g.dir[at] != REC_PUT || !idx_has(at)) continue;
        if (rec_decode(g.dir + at, rlen, &r)) return MEDIA_ERR_IO;
        r.kill = KILL_NONE;
        const uint16_t len = rec_encode(&r, buf);
        if (pos + len > HALF) return MEDIA_ERR_NO_SPACE;
        if (fl_write(base + pos, buf, len)) return MEDIA_ERR_IO;
        if (kill_off && *kill_off == at) newkill = pos;
        pos += len;
        n++;
    }
    /* Map the new half before its header commits it (its records are all
     * written, so a mapping that is a copy has them). Until the header is
     * known to be there the old half stays the active, mapped one. */
    const uint8_t *const old_dir = g.dir;
    void *const old_handle = g.dir_handle;
    g.dir = NULL;
    bool committed = false;
    if (dir_map(nu) == 0 && dir_verify(pos, n)) {
        (void)hdr_write(nu, g.gen + 1u, g.next_seq);
        hdr_t h;
        if (hdr_read(nu, &h)) g.dirty = true;
        else committed = h.valid && h.gen == g.gen + 1u;
    }
    if (!committed) {
        dir_unmap();
        g.dir = old_dir;
        g.dir_handle = old_handle;
        return MEDIA_ERR_IO;
    }
    g.f->unmap(g.f->ctx, old_handle);
    g.gen++;
    g.active = nu;
    g.half_erased[old] = false;
    g.stale = true;
    g.end = pos;
    g.nrec = n;
    g.dirty = false;
    g.idle_failed = false;
    if (kill_off) *kill_off = newkill;
    /* neither can fail on records that dir_verify() passed (the index holds
     * as many entries as before: no memory is needed) */
    if (idx_build(NULL) || (g.unsettled && map_rebuild(&r))) {
        g.dirty = true;
        return MEDIA_ERR_IO;
    }
    g.unsettled = false;
    idx_trim();
    return MEDIA_OK;
}

static bool dir_blank(uint32_t off, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        if (g.dir[off + i] != 0xFF) return false;
    return true;
}

/* The record just written at off, read back through the mapping. On the
 * device the mapping shows a write at once (the flash driver invalidates
 * the cache of mapped pages, see media_esp.c). A flash whose mappings are
 * copies (the host tool's) shows it after mapping again: unmap first, the
 * device does not map a page twice. 0 = it is there; -1 it is not, or the
 * half could not be mapped again (g.dir is NULL then, see dir_ok()). */
static int dir_readback(uint32_t off, const uint8_t *buf, uint32_t len) {
    if (memcmp(g.dir + off, buf, len) == 0) return 0;
    dir_unmap();
    if (dir_map(g.active)) return -1;
    return memcmp(g.dir + off, buf, len) == 0 ? 0 : -1;
}

/* Append r (seq filled in). kill_off: the record it kills (its offset in
 * the active half), 0 = none; r->kill is set from it. The record is the
 * commit point of the operation. Keeps the liveness bitmap and the index up
 * to date.
 *
 * When the write was tried and failed, or does not read back, the record
 * may still be complete on flash, where a reboot would count it. A snapshot
 * is written at once: it is made from RAM, so it drops the record. When
 * that fails too the store is `unsettled` (see there) and the caller must
 * not free the space the record refers to.
 *
 * A PUT that kills nothing adds an index entry: the room for it is made
 * before anything is written (media_put_begin has done it already; here
 * again for an upload whose key was deleted meanwhile). MEDIA_ERR_UNAVAILABLE
 * when there is no memory: nothing was written. A replace, a move and a
 * DEL need no entry. */
static media_err_t dir_append(rec_t *r, uint32_t kill_off) {
    uint8_t buf[REC_MAX];
    const uint16_t len = r->type == REC_DEL ? (uint16_t)DEL_LEN : put_len(r->keylen, &r->loc);
    if (g.nidx >= DIR_MAX_RECORDS) return MEDIA_ERR_NO_SPACE; /* a snapshot would not make room */
    const bool adds = r->type == REC_PUT && !kill_off;
    if (adds && idx_reserve(g.nidx + 1u)) return MEDIA_ERR_UNAVAILABLE;
    for (int attempt = 0; attempt < 2; attempt++) {
        if (g.dirty || g.nrec >= DIR_MAX_RECORDS || g.end + len > HALF) {
            const media_err_t e = dir_snapshot(&kill_off);
            if (e != MEDIA_OK) return e;
            if (g.nrec >= DIR_MAX_RECORDS || g.end + len > HALF) return MEDIA_ERR_NO_SPACE;
        }
        r->kill = KILL_NONE;
        if (kill_off && ord_of(kill_off, &r->kill)) {
            LOGE("directory: damaged log before 0x%x", (unsigned)kill_off);
            g.dirty = true;
            return MEDIA_ERR_IO;
        }
        if (r->type == REC_DEL && r->kill == KILL_NONE) return MEDIA_ERR_IO; /* cannot happen */
        if (r->type == REC_PUT && !kill_off && idx_reserve(g.nidx + 1u)) return MEDIA_ERR_UNAVAILABLE; /* not `adds`: cannot happen */
        if (!dir_blank(g.end, len)) {
            LOGW("directory tail not blank at 0x%x, snapshot", (unsigned)g.end);
            g.dirty = true;
            continue;
        }
        rec_encode(r, buf);
        if (fl_write(g.active * HALF + g.end, buf, len) || dir_readback(g.end, buf, len)) {
            LOGE("directory: append at 0x%x failed", (unsigned)g.end);
            g.dirty = true;
            if (dir_snapshot(NULL) != MEDIA_OK) g.unsettled = true;
            return MEDIA_ERR_IO;
        }
        const uint32_t at = g.end;
        g.end += len;
        g.nrec++;
        if (kill_off) idx_remove(kill_off);
        if (r->type == REC_PUT) idx_insert(at, len);
        g.idle_failed = false;
        return MEDIA_OK;
    }
    return MEDIA_ERR_IO;
}

/* --------------------------------------------------------- open files */

static int open_find(uint32_t seq) {
    for (int i = 0; i < MEDIA_MAX_OPEN; i++)
        if (g.open[i].used && !g.open[i].dead && g.open[i].seq == seq) return i;
    return -1;
}

static bool page_mapped(unsigned page) {
    for (int i = 0; i < MEDIA_MAX_OPEN; i++)
        if (g.maps[i].refs && g.maps[i].page == page) return true;
    return false;
}

static int map_alloc(void) {
    for (int i = 0; i < MEDIA_MAX_OPEN; i++)
        if (!g.maps[i].refs) return i;
    return -1;
}

static void map_release(uint8_t m) {
    if (m == NO_MAP) return;
    map_t *mp = &g.maps[m];
    if (mp->refs && --mp->refs == 0) {
        g.f->unmap(g.f->ctx, mp->handle);
        if (mp->page != NO_PAGE) g.compact_dirty = true; /* its files may move now */
        memset(mp, 0, sizeof *mp);
    }
}

/* The space of an open file, in the free map. */
static void open_mark(const open_t *o, bool used) {
    if (o->kind == LOC_SECTORS) {
        const uint16_t m = run_mask(o->first, o->nsec);
        if (used) g.used[o->page] |= m;
        else g.used[o->page] &= (uint16_t)~m;
    } else if (o->kind == LOC_PAGES) {
        for (unsigned p = 0; p < g.npages; p++)
            if (o->pagebits[p >> 3] & (1u << (p & 7))) g.used[p] = used ? 0xFFFFu : 0u;
    }
    g.compact_dirty = true;
}

static void open_free_space(const open_t *o) { open_mark(o, false); }

static media_err_t open_map(open_t *o, const rec_t *r) {
    const loc_t *l = &r->loc;
    memset(o, 0, sizeof *o);
    o->seq = r->seq;
    o->size = r->size;
    o->kind = l->kind;
    o->map = NO_MAP;
    if (l->kind == LOC_SECTORS) {
        o->page = l->page;
        o->first = l->first;
        o->nsec = l->nsec;
        int m = -1;
        for (int i = 0; i < MEDIA_MAX_OPEN; i++)
            if (g.maps[i].refs && g.maps[i].page == l->page) m = i;
        if (m < 0) {
            m = map_alloc();
            if (m < 0) return MEDIA_ERR_BUSY;
            const uint8_t page = l->page;
            if (g.f->map(g.f->ctx, &page, 1, &g.maps[m].ptr, &g.maps[m].handle)) return MEDIA_ERR_IO;
            g.maps[m].page = page;
        }
        g.maps[m].refs++;
        o->map = (uint8_t)m;
        o->data = (const uint8_t *)g.maps[m].ptr + l->first * SECTOR;
    } else if (l->kind == LOC_PAGES) {
        for (unsigned i = 0; i < l->npages; i++) o->pagebits[l->pages[i] >> 3] |= (uint8_t)(1u << (l->pages[i] & 7));
        const int m = map_alloc();
        if (m < 0) return MEDIA_ERR_BUSY;
        if (g.f->map(g.f->ctx, l->pages, l->npages, &g.maps[m].ptr, &g.maps[m].handle)) return MEDIA_ERR_IO;
        g.maps[m].page = NO_PAGE;
        g.maps[m].refs = 1;
        o->map = (uint8_t)m;
        o->data = (const uint8_t *)g.maps[m].ptr;
    }
    o->refs = 1;
    o->used = 1;
    if (++g.open_gen > 0xFFFFFFu) g.open_gen = 1;
    o->gen = g.open_gen;
    return MEDIA_OK;
}

/* r was killed by the record just appended (which took it out of the
 * index): free its space now, or on the last close when it is open. */
static void retire(const rec_t *r) {
    const int i = open_find(r->seq);
    if (i >= 0) g.open[i].dead = 1;
    else loc_mark(&r->loc, false);
}

/* ---------------------------------------------------------- allocation */

/* Wear leveling. A freed page used to be the first one reused (lowest page,
 * erased first), so a file replaced over and over wore out the same pages
 * while the rest of the store stayed fresh. As in the littlefs block
 * allocator, allocation is next-fit instead: a cursor sweeps the data pages
 * and wraps, so every free page gets its turn. The cursor lives in RAM; it
 * starts at a pseudo-random page on mount so reboots do not restart the
 * sweep at the same place. Data at rest is never moved (no static leveling). */

/* Compaction (moving small files out of sparse pages) only runs while fewer
 * whole pages than this are free, or after a large reservation failed: it
 * costs writes and erases, and next-fit leaves sparse pages behind on purpose. */
#define COMPACT_BELOW 4u

static unsigned next_page(unsigned p) { return p + 1u < g.npages ? p + 1u : MEDIA_DIR_PAGES; }

static unsigned free_pages(void) {
    unsigned n = 0;
    for (unsigned p = MEDIA_DIR_PAGES; p < g.npages; p++) n += g.used[p] == 0;
    return n;
}

/* First run of n free sectors in page p starting at or after `from`, -1 none. */
static int run_at(unsigned p, unsigned from, unsigned n) {
    const uint16_t u = g.used[p];
    for (unsigned first = from; first + n <= SPP; first++)
        if (!(u & run_mask(first, n))) return (int)first;
    return -1;
}

/* Small files are appended to a fill page; a full fill page is followed by
 * the next fresh page from the cursor. Sectors freed in older pages are left
 * alone: once all files of a page are gone the page is free again and the
 * cursor reuses it in turn. Only while whole pages run low are the holes in
 * shared pages filled first. Pure: reserve() calls it more than once. */
static bool alloc_small(loc_t *l) {
    const unsigned n = l->nsec;
    if (g.fill != NO_PAGE) {
        const int s = run_at(g.fill, g.fill_sec, n);
        if (s >= 0) {
            l->page = (uint8_t)g.fill;
            l->first = (uint8_t)s;
            g.hint_fresh = false;
            return true;
        }
    }
    const bool low = free_pages() < COMPACT_BELOW;
    for (int pass = 0; pass < 2; pass++) {
        const bool holes = (pass == 0) == low;
        unsigned p = g.cursor;
        for (unsigned k = MEDIA_DIR_PAGES; k < g.npages; k++, p = next_page(p)) {
            const uint16_t u = g.used[p];
            if (holes ? (u == 0 || u == 0xFFFFu) : u != 0) continue;
            const int s = run_at(p, 0, n);
            if (s < 0) continue;
            l->page = (uint8_t)p;
            l->first = (uint8_t)s;
            g.hint_fresh = !holes;
            return true;
        }
    }
    return false;
}

/* The next n free pages from the cursor on, in sweep order. The pages need
 * not be contiguous: a mapping takes any list of pages. Pure. */
static bool alloc_large(loc_t *l) {
    const unsigned n = l->npages;
    unsigned got = 0, p = g.cursor;
    for (unsigned k = MEDIA_DIR_PAGES; k < g.npages && got < n; k++, p = next_page(p))
        if (!g.used[p]) l->pages[got++] = (uint8_t)p;
    return got == n;
}

/* A reservation is final: move the cursors past it. */
static void note_alloc(const loc_t *l) {
    if (l->kind == LOC_PAGES) {
        g.cursor = (uint16_t)next_page(l->pages[l->npages - 1u]);
    } else if (l->kind == LOC_SECTORS && (l->page == g.fill || g.hint_fresh)) {
        if (l->page != g.fill) g.cursor = (uint16_t)next_page(l->page);
        g.fill = l->page;
        g.fill_sec = (uint8_t)(l->first + l->nsec);
    }
}

static bool alloc_find(loc_t *l) {
    if (l->kind == LOC_SECTORS) return alloc_small(l);
    if (l->kind == LOC_PAGES) return alloc_large(l);
    return true;
}

/* The oldest (lowest seq) live RENDER entry that is not open, with
 * after < seq <= upto: 1 found (*off: where its record is), 0 none, -1
 * unreadable. The entries of a bucket are one run of the index. */
static int evictable_oldest(uint32_t after, uint32_t upto, rec_t *out, uint32_t *off) {
    uint32_t best = 0, best_seq = 0;
    for (uint16_t i = idx_search(MEDIA_BUCKET_RENDER, "", 0, false, NULL); i < g.nidx; i++) {
        const uint8_t *p = g.dir + g.idx[i];
        if (p[1] != MEDIA_BUCKET_RENDER) break;
        const uint32_t seq = get32(p + 4);
        if (p[11] == LOC_EMPTY || seq <= after || seq > upto || open_find(seq) >= 0) continue;
        if (!best || seq < best_seq) {
            best = g.idx[i];
            best_seq = seq;
        }
    }
    if (!best) return 0;
    if (rec_load(best, out)) return -1;
    *off = best;
    return 1;
}

static void evict_restore(uint32_t upto) {
    rec_t r;
    uint32_t o;
    uint32_t after = 0;
    int e;
    while ((e = evictable_oldest(after, upto, &r, &o)) == 1) {
        loc_mark(&r.loc, true);
        after = r.seq;
    }
    if (e < 0) g.dirty = g.unsettled = true; /* the free map is rebuilt by the next snapshot */
}

/* Find and mark space for l; evict RENDER entries (oldest first) only when
 * it does not fit otherwise, and only when evicting makes it fit. */
static media_err_t reserve(loc_t *l) {
    if (l->kind == LOC_EMPTY) return MEDIA_OK;
    if (alloc_find(l)) {
        loc_mark(l, true);
        note_alloc(l);
        return MEDIA_OK;
    }
    rec_t r;
    uint32_t o;
    uint32_t upto = 0;
    bool fits = false;
    for (;;) {
        const int e = evictable_oldest(upto, 0xFFFFFFFFu, &r, &o);
        if (e < 0) {
            evict_restore(upto);
            return MEDIA_ERR_IO;
        }
        if (e == 0) break;
        loc_mark(&r.loc, false); /* simulated: still live on flash */
        upto = r.seq;
        if (alloc_find(l)) {
            fits = true;
            break;
        }
    }
    if (!fits) {
        evict_restore(upto);
        /* whole pages may still be won back from sparse shared pages */
        if (l->kind == LOC_PAGES) g.compact_wanted = true;
        return MEDIA_ERR_NO_SPACE;
    }
    /* Make the evictions real, oldest first. */
    for (;;) {
        const int e = evictable_oldest(0, upto, &r, &o);
        if (e == 0) break;
        if (e < 0) {
            evict_restore(upto);
            return MEDIA_ERR_IO;
        }
        rec_t d;
        memset(&d, 0, sizeof d);
        d.type = REC_DEL;
        d.bucket = r.bucket;
        d.seq = g.next_seq;
        const media_err_t err = dir_append(&d, o);
        if (err != MEDIA_OK) {
            evict_restore(upto);
            return err;
        }
        g.next_seq++;
    }
    if (!alloc_find(l)) return MEDIA_ERR_NO_SPACE;
    loc_mark(l, true);
    note_alloc(l);
    return MEDIA_OK;
}

/* -------------------------------------------------------------- mount */

static media_err_t mount_locked(void) {
    hdr_t h[2];
    if (hdr_read(0, &h[0]) || hdr_read(1, &h[1])) return MEDIA_ERR_IO;
    int act;
    if (!h[0].valid && !h[1].valid) {
        LOGW("no valid directory: formatting the media store");
        if (fl_erase(0, HALF) || hdr_write(0, 1, 1)) return MEDIA_ERR_IO;
        h[0].gen = 1;
        h[0].next_seq = 1;
        act = 0;
    } else if (h[1].valid && (!h[0].valid || (int32_t)(h[1].gen - h[0].gen) > 0)) {
        act = 1;
    } else {
        act = 0;
    }
    g.active = (uint8_t)act;
    g.gen = h[act].gen;
    g.next_seq = h[act].next_seq ? h[act].next_seq : 1u;
    g.stale = true; /* the other half: verified or erased by media_maintain */
    g.compact_dirty = true;
    for (unsigned p = 0; p < MEDIA_DIR_PAGES; p++) g.used[p] = 0xFFFFu;

    if (dir_map(g.active)) return MEDIA_ERR_IO;

    /* The one pass that checks every record (CRC, fields, kill) and marks
     * the dead ones. */
    uint8_t dead[DIR_MAX_RECORDS / 8];
    memset(dead, 0, sizeof dead);
    rec_t r;
    uint16_t n = 0;
    uint32_t maxseq = 0, off = HDR_LEN;
    for (;;) {
        uint32_t len = 0;
        const int e = rec_frame(off, &len);
        if (e == 0) break;
        bool ok = e == 1 && n < DIR_MAX_RECORDS && rec_crc_ok(g.dir + off, len) &&
                  rec_decode(g.dir + off, len, &r) == 0;
        if (ok && r.kill != KILL_NONE) ok = r.kill < n && !is_dead(dead, r.kill);
        if (!ok) {
            LOGW("directory: torn or invalid record at 0x%x, ignored from there", (unsigned)off);
            g.dirty = true;
            break;
        }
        if (r.kill != KILL_NONE) set_dead(dead, r.kill);
        if (r.type == REC_DEL) set_dead(dead, n);
        if (r.seq > maxseq) maxseq = r.seq;
        off += len;
        n++;
    }
    g.end = off;
    g.nrec = n;
    if (maxseq + 1u > g.next_seq) g.next_seq = maxseq + 1u;

    /* A later record can kill an earlier one, so what is live is known only
     * now: the index from the bitmap (its memory sized to the live entries),
     * the free map from the index. */
    const int b = idx_build(dead);
    if (b) return b == -2 ? MEDIA_ERR_UNAVAILABLE : MEDIA_ERR_IO;
    for (uint16_t i = 0; i < g.nidx; i++) {
        if (rec_load(g.idx[i], &r)) return MEDIA_ERR_IO;
        if (!loc_free(&r.loc)) LOGE("directory: %.*s overlaps another entry", (int)r.keylen, r.key);
        loc_mark(&r.loc, true);
    }
    return MEDIA_OK;
}

media_err_t media_mount(const media_flash_t *flash) {
    if (g.mounted) return MEDIA_OK;
    if (!flash || flash->size % PAGE || flash->size / PAGE < MEDIA_DIR_PAGES + 1u ||
        flash->size / PAGE > MEDIA_MAX_PAGES)
        return MEDIA_ERR_UNAVAILABLE;
    memset(&g, 0, sizeof g);
    g.f = flash;
    g.npages = (uint16_t)(flash->size / PAGE);
    g.used = (uint16_t *)MEDIA_ALLOC(3u * g.npages * sizeof(uint16_t));
    if (!g.used) {
        g.f = NULL;
        return MEDIA_ERR_UNAVAILABLE;
    }
    memset(g.used, 0, 3u * g.npages * sizeof(uint16_t));
    g.erased = g.used + g.npages;
    g.wear = g.erased + g.npages;
    g.fill = NO_PAGE;
    g.cursor = MEDIA_DIR_PAGES;
    lock();
    const media_err_t e = mount_locked();
    if (e == MEDIA_OK) {
        /* Start the sweep somewhere else after every reboot (littlefs seeds
         * its allocator from the CRCs it reads on mount): next_seq and gen
         * change with every write, so this differs from boot to boot. */
        uint32_t x = g.next_seq ^ (g.gen * 0x9E3779B9u);
        x ^= x >> 16;
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        g.cursor = (uint16_t)(MEDIA_DIR_PAGES + x % (g.npages - MEDIA_DIR_PAGES));
        g.mounted = true;
    }
    unlock();
    if (e != MEDIA_OK) {
        dir_unmap();
        if (g.idx) MEDIA_FREE(g.idx);
        MEDIA_FREE(g.used);
        memset(&g, 0, sizeof g);
    }
    return e;
}

void media_unmount(void) {
    if (!g.mounted) return;
    lock();
    for (int i = 0; i < MEDIA_MAX_OPEN; i++)
        if (g.maps[i].refs) g.f->unmap(g.f->ctx, g.maps[i].handle);
    dir_unmap();
    g.mounted = false;
    unlock();
    if (g.idx) MEDIA_FREE(g.idx);
    MEDIA_FREE(g.used);
    memset(&g, 0, sizeof g);
}

/* --------------------------------------------------------- public API */

media_err_t media_open(media_bucket_t bucket, const char *key, media_file_t *out) {
    if (!out) return MEDIA_ERR_INVALID;
    memset(out, 0, sizeof *out);
    uint8_t kl;
    if (!key_ok((int)bucket, key, &kl)) return MEDIA_ERR_INVALID;
    if (!g.mounted) return MEDIA_ERR_UNAVAILABLE;
    lock();
    rec_t r;
    uint32_t at;
    media_err_t err = MEDIA_OK;
    const int e = dir_ok() ? dir_find((uint8_t)bucket, key, kl, &r, &at) : -1;
    int i = -1;
    if (e < 0) err = MEDIA_ERR_IO;
    else if (e == 0) err = MEDIA_ERR_NOT_FOUND;
    else if ((i = open_find(r.seq)) >= 0) {
        if (g.open[i].refs == 0xFFFFu) err = MEDIA_ERR_BUSY;
        else g.open[i].refs++;
    } else {
        for (int k = 0; k < MEDIA_MAX_OPEN && i < 0; k++)
            if (!g.open[k].used) i = k;
        if (i < 0) {
            err = MEDIA_ERR_BUSY;
        } else {
            err = open_map(&g.open[i], &r);
            if (err != MEDIA_OK) {
                map_release(g.open[i].map);
                memset(&g.open[i], 0, sizeof g.open[i]);
            }
        }
    }
    if (err == MEDIA_OK) {
        out->data = g.open[i].data;
        out->size = g.open[i].size;
        out->handle = (g.open[i].gen << 8) | (uint32_t)(i + 1);
    }
    unlock();
    return err;
}

void media_close(media_file_t *file) {
    if (!file) return;
    const uint32_t h = file->handle;
    memset(file, 0, sizeof *file);
    if (!h || !g.mounted) return;
    lock();
    const unsigned i = (h & 0xFFu) - 1u;
    if (i < MEDIA_MAX_OPEN && g.open[i].used && g.open[i].gen == h >> 8) {
        open_t *o = &g.open[i];
        if (--o->refs == 0) {
            map_release(o->map);
            if (o->dead) open_free_space(o);
            memset(o, 0, sizeof *o);
        }
    }
    unlock();
}

static media_writer_t *writer_check(media_writer_t *w) {
    for (int i = 0; i < MEDIA_MAX_WRITERS; i++)
        if (w == &g.writers[i] && w->used) return w;
    return NULL;
}

media_err_t media_put_begin(media_bucket_t bucket, const char *key, uint32_t size, uint32_t mtime,
                            media_writer_t **out) {
    if (!out) return MEDIA_ERR_INVALID;
    *out = NULL;
    uint8_t kl;
    if (!key_ok((int)bucket, key, &kl) || size > MEDIA_FILE_MAX) return MEDIA_ERR_INVALID;
    if (!g.mounted) return MEDIA_ERR_UNAVAILABLE;
    lock();
    media_err_t err = MEDIA_OK;
    media_writer_t *w = NULL;
    uint32_t inflight_n = 0, inflight_b = 0;
    for (int i = 0; i < MEDIA_MAX_WRITERS; i++) {
        if (g.writers[i].used) {
            inflight_n++;
            inflight_b += g.writers[i].reclen;
        } else if (!w) {
            w = &g.writers[i];
        }
    }
    loc_t l;
    loc_for_size(size, &l);
    const uint16_t reclen = put_len(kl, &l);
    if (!w) err = MEDIA_ERR_BUSY;
    else if (!dir_ok() || (g.unsettled && dir_snapshot(NULL) != MEDIA_OK)) err = MEDIA_ERR_IO;
    else if (g.nidx + inflight_n + 1u > MEDIA_MAX_ENTRIES ||
             g.live_bytes + inflight_b + reclen > DIR_LIVE_MAX)
        err = MEDIA_ERR_NO_SPACE;
    else {
        /* An index entry for this upload and for every other one in
         * progress, before anything is written (evictions, data). A replace
         * needs none: it does not fail for memory. */
        bool exists = false;
        (void)idx_search((uint8_t)bucket, key, kl, false, &exists);
        if (idx_reserve(g.nidx + inflight_n + 1u) && !exists) err = MEDIA_ERR_UNAVAILABLE;
        else err = reserve(&l);
    }
    if (err == MEDIA_OK) {
        memset(w, 0, sizeof *w);
        w->used = 1;
        w->bucket = (uint8_t)bucket;
        w->keylen = kl;
        memcpy(w->key, key, kl);
        w->size = size;
        w->mtime = mtime;
        w->reclen = reclen;
        w->loc = l;
        *out = w;
    }
    unlock();
    return err;
}

static uint32_t writer_phys(const media_writer_t *w, uint32_t pos) {
    const loc_t *l = &w->loc;
    if (l->kind == LOC_SECTORS) return l->page * PAGE + l->first * SECTOR + pos;
    return l->pages[pos / PAGE] * PAGE + pos % PAGE;
}

media_err_t media_put_write(media_writer_t *w, const void *data, uint32_t len) {
    if (!w || (!data && len)) return MEDIA_ERR_INVALID;
    if (!g.mounted) return MEDIA_ERR_UNAVAILABLE;
    lock();
    media_err_t err = MEDIA_OK;
    if (!writer_check(w)) err = MEDIA_ERR_INVALID;
    else if (len > w->size - w->written) err = MEDIA_ERR_SIZE;
    const uint8_t *src = (const uint8_t *)data;
    while (err == MEDIA_OK && len) {
        const uint32_t phys = writer_phys(w, w->written);
        const unsigned p = phys / PAGE, s = (phys % PAGE) / SECTOR;
        if (phys % SECTOR == 0) {
            /* entering a sector: make sure it is erased, then it is no longer.
             * A whole-page erase only when no sector of it is known erased:
             * erasing an erased sector again is wear for nothing. */
            if (w->loc.kind == LOC_PAGES && phys % PAGE == 0 && g.erased[p] == 0) {
                if (fl_erase(p * PAGE, PAGE)) err = MEDIA_ERR_IO;
                else g.erased[p] = 0xFFFFu;
            } else if (!((g.erased[p] >> s) & 1u)) {
                if (fl_erase(p * PAGE + s * SECTOR, SECTOR)) err = MEDIA_ERR_IO;
                else g.erased[p] |= (uint16_t)(1u << s);
            }
            g.erased[p] &= (uint16_t)~(1u << s);
            if (err != MEDIA_OK) break;
        }
        uint32_t n = SECTOR - phys % SECTOR;
        if (n > len) n = len;
        if (fl_write(phys, src, n)) {
            err = MEDIA_ERR_IO;
            break;
        }
        w->written += n;
        src += n;
        len -= n;
    }
    unlock();
    return err;
}

static void writer_release(media_writer_t *w) {
    loc_mark(&w->loc, false);
    memset(w, 0, sizeof *w);
}

/* The commit of w failed. Its space is free again, unless its record may
 * have reached the flash (`unsettled`): then it stays in use until a
 * snapshot has dropped the record and rebuilt the free map. */
static void writer_fail(media_writer_t *w) {
    if (g.unsettled) memset(w, 0, sizeof *w);
    else writer_release(w);
}

media_err_t media_put_commit(media_writer_t *w, const uint8_t md5[16]) {
    if (!w) return MEDIA_ERR_INVALID;
    if (!g.mounted) return MEDIA_ERR_UNAVAILABLE;
    lock();
    media_err_t err = MEDIA_OK;
    if (!writer_check(w)) {
        err = MEDIA_ERR_INVALID;
    } else if (w->written != w->size) {
        writer_release(w);
        err = MEDIA_ERR_SIZE;
    } else {
        rec_t old, nr;
        uint32_t at = 0;
        const int e = dir_ok() ? dir_find(w->bucket, w->key, w->keylen, &old, &at) : -1;
        if (e < 0) {
            err = MEDIA_ERR_IO;
        } else {
            memset(&nr, 0, sizeof nr);
            nr.type = REC_PUT;
            nr.bucket = w->bucket;
            nr.keylen = w->keylen;
            memcpy(nr.key, w->key, w->keylen);
            nr.seq = g.next_seq;
            nr.size = w->size;
            nr.mtime = w->mtime;
            if (md5) memcpy(nr.md5, md5, 16);
            nr.loc = w->loc;
            err = dir_append(&nr, e ? at : 0u);
        }
        if (err == MEDIA_OK) {
            g.next_seq++;
            if (e) retire(&old);
            memset(w, 0, sizeof *w); /* the space now belongs to the entry */
        } else {
            writer_fail(w);
        }
    }
    unlock();
    return err;
}

void media_put_abort(media_writer_t *w) {
    if (!w || !g.mounted) return;
    lock();
    if (writer_check(w)) writer_release(w);
    unlock();
}

media_err_t media_delete(media_bucket_t bucket, const char *key) {
    uint8_t kl;
    if (!key_ok((int)bucket, key, &kl)) return MEDIA_ERR_INVALID;
    if (!g.mounted) return MEDIA_ERR_UNAVAILABLE;
    lock();
    rec_t r;
    uint32_t at;
    media_err_t err = MEDIA_OK;
    const int e = dir_ok() ? dir_find((uint8_t)bucket, key, kl, &r, &at) : -1;
    if (e < 0) err = MEDIA_ERR_IO;
    else if (e == 0) err = MEDIA_ERR_NOT_FOUND;
    else {
        rec_t d;
        memset(&d, 0, sizeof d);
        d.type = REC_DEL;
        d.bucket = r.bucket;
        d.seq = g.next_seq;
        err = dir_append(&d, at);
        if (err == MEDIA_OK) {
            g.next_seq++;
            retire(&r);
        }
    }
    unlock();
    return err;
}

static void fill_stat(const rec_t *r, media_stat_t *out) {
    memset(out, 0, sizeof *out);
    memcpy(out->key, r->key, r->keylen);
    out->size = r->size;
    out->mtime = r->mtime;
    memcpy(out->md5, r->md5, 16);
}

media_err_t media_stat(media_bucket_t bucket, const char *key, media_stat_t *out) {
    uint8_t kl;
    if (!out || !key_ok((int)bucket, key, &kl)) return MEDIA_ERR_INVALID;
    if (!g.mounted) return MEDIA_ERR_UNAVAILABLE;
    lock();
    rec_t r;
    uint32_t at;
    const int e = dir_ok() ? dir_find((uint8_t)bucket, key, kl, &r, &at) : -1;
    if (e == 1) fill_stat(&r, out);
    unlock();
    return e < 0 ? MEDIA_ERR_IO : e == 0 ? MEDIA_ERR_NOT_FOUND : MEDIA_OK;
}

media_err_t media_next(media_bucket_t bucket, const char *after, media_stat_t *out) {
    if (!out || (int)bucket < 0 || bucket >= MEDIA_BUCKET_COUNT) return MEDIA_ERR_INVALID;
    if (!g.mounted) return MEDIA_ERR_UNAVAILABLE;
    if (!after) after = "";
    const size_t alen = strlen(after);
    lock();
    /* the entries of a bucket are one run of the index, in key order */
    rec_t r;
    int e = -1;
    if (dir_ok()) {
        const uint16_t i = idx_search((uint8_t)bucket, after, alen, true, NULL);
        e = i < g.nidx && g.dir[g.idx[i] + 1u] == (uint8_t)bucket ? (rec_load(g.idx[i], &r) ? -1 : 1) : 0;
    }
    if (e == 1) fill_stat(&r, out);
    unlock();
    return e < 0 ? MEDIA_ERR_IO : e == 0 ? MEDIA_ERR_NOT_FOUND : MEDIA_OK;
}

void media_usage(media_usage_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (!g.mounted) return;
    lock();
    out->total = (g.npages - MEDIA_DIR_PAGES) * PAGE;
    out->erase_sectors = g.erase_sectors;
    for (unsigned p = 0; p < g.npages; p++)
        if (g.wear[p] > out->erase_page_max) out->erase_page_max = g.wear[p];
    out->dir_generation = g.gen;
    for (unsigned p = MEDIA_DIR_PAGES; p < g.npages; p++) out->free += (SPP - popcount16(g.used[p])) * SECTOR;
    for (int i = 0; i < MEDIA_MAX_OPEN; i++) {
        const open_t *o = &g.open[i];
        if (!o->used || !o->dead) continue;
        if (o->kind == LOC_SECTORS) out->reclaimable += o->nsec * SECTOR;
        else if (o->kind == LOC_PAGES)
            for (unsigned b = 0; b < sizeof o->pagebits; b++) out->reclaimable += popcount16(o->pagebits[b]) * PAGE;
    }
    out->used = out->total - out->free - out->reclaimable;
    for (uint16_t i = dir_ok() ? idx_search(MEDIA_BUCKET_RENDER, "", 0, false, NULL) : g.nidx; i < g.nidx; i++) {
        const uint8_t *p = g.dir + g.idx[i];
        if (p[1] != MEDIA_BUCKET_RENDER) break;
        loc_t l;
        loc_for_size(get32(p + 12), &l);
        out->derived += loc_alloc(&l);
    }
    unlock();
}

/* ---------------------------------------------------------- maintenance */

/* Move one small file out of the sparsest shared page into a fuller one.
 * 1 did something (a move or one erase), 0 nothing to do, -1 error. */
static int compact_step(void) {
    if (!g.compact_dirty) return 0;
    uint8_t tried[MEDIA_MAX_PAGES / 8];
    memset(tried, 0, sizeof tried);
    for (;;) {
        int src = -1;
        unsigned pc = SPP + 1;
        for (unsigned p = MEDIA_DIR_PAGES; p < g.npages; p++) {
            const unsigned c = popcount16(g.used[p]);
            if (c == 0 || c > SPP / 2 || p == g.fill || (tried[p >> 3] & (1u << (p & 7))) || page_mapped(p))
                continue; /* the fill page is sparse only because it is still filling */
            if (c < pc) {
                pc = c;
                src = (int)p;
            }
        }
        if (src < 0) {
            g.compact_dirty = false;
            return 0;
        }
        tried[src >> 3] |= (uint8_t)(1u << (src & 7));
        rec_t r;
        uint32_t walk = HDR_LEN, src_off = 0;
        int dst = -1, dfirst = 0;
        /* the files of the page, in log order */
        for (uint16_t o = 0; o < g.nrec && dst < 0; o++) {
            const uint8_t *p = g.dir + walk;
            const uint32_t at = walk, rlen = rec_len(at);
            if (!rlen) return -1;
            walk += rlen;
            if (p[0] != REC_PUT || rlen < REC_FIXED + 8u || p[11] != LOC_SECTORS || p[36] != src || !idx_has(at)) continue;
            if (rec_decode(p, rlen, &r)) return -1;
            int bestscore = -1;
            for (unsigned q = MEDIA_DIR_PAGES; q < g.npages; q++) {
                const uint16_t u = g.used[q];
                if ((int)q == src || u == 0 || u == 0xFFFFu || popcount16(u) < pc) continue;
                for (unsigned first = 0; first + r.loc.nsec <= SPP; first++) {
                    const uint16_t m = run_mask(first, r.loc.nsec);
                    if (u & m) continue;
                    const int score = ((g.erased[q] & m) == m ? 32 : 0) + (int)popcount16(u);
                    if (score > bestscore) {
                        bestscore = score;
                        dst = (int)q;
                        dfirst = (int)first;
                    }
                }
            }
            if (dst >= 0) src_off = at;
        }
        if (dst < 0) continue;
        if (g.live_bytes + r.len > HALF - HDR_LEN) {
            g.compact_dirty = false;
            return 0;
        }
        const uint16_t m = run_mask((unsigned)dfirst, r.loc.nsec);
        if ((g.erased[dst] & m) != m) {
            /* one erase per call: the next call finds the same target */
            for (unsigned s = (unsigned)dfirst; s < (unsigned)dfirst + r.loc.nsec; s++) {
                if ((g.erased[dst] >> s) & 1u) continue;
                if (fl_erase(dst * PAGE + s * SECTOR, SECTOR)) return -1;
                g.erased[dst] |= (uint16_t)(1u << s);
                return 1;
            }
        }
        loc_t nl = r.loc;
        nl.page = (uint8_t)dst;
        nl.first = (uint8_t)dfirst;
        loc_mark(&nl, true);
        g.erased[dst] &= (uint16_t)~m;
        uint8_t buf[512];
        for (uint32_t off = 0; off < r.size;) {
            uint32_t n = r.size - off;
            if (n > sizeof buf) n = sizeof buf;
            if (fl_read(r.loc.page * PAGE + r.loc.first * SECTOR + off, buf, n) ||
                fl_write(nl.page * PAGE + nl.first * SECTOR + off, buf, n)) {
                loc_mark(&nl, false);
                return -1;
            }
            off += n;
        }
        rec_t nr = r;
        nr.loc = nl;
        /* same seq: it is the same entry, moved */
        if (dir_append(&nr, src_off) != MEDIA_OK) {
            if (!g.unsettled) loc_mark(&nl, false); /* else the copy may be the live one after a reboot */
            return -1;
        }
        loc_mark(&r.loc, false);
        return 1;
    }
}

/* Free sectors of the fill page that the next small files will take. */
static uint16_t fill_todo(void) {
    if (g.fill == NO_PAGE) return 0;
    const uint16_t ahead = (uint16_t)(0xFFFFu << g.fill_sec);
    return (uint16_t)(~(g.used[g.fill] | g.erased[g.fill]) & ahead);
}

static int preerase_sector(unsigned p, unsigned s) {
    const int v = verify_blank(p * PAGE + s * SECTOR, SECTOR);
    if (v < 0) return -1;
    if (v == 0 && fl_erase(p * PAGE + s * SECTOR, SECTOR)) return -1;
    g.erased[p] |= (uint16_t)(1u << s);
    return 1;
}

/* Verify free space blank, else erase it, ahead of where the next
 * allocations go: free pages in sweep order from the cursor, then the rest
 * of the fill page. Holes in other shared pages are erased when they are
 * used. A page with some sectors already erased is done sector by sector,
 * so no sector is erased twice. 1 did something, 0 nothing to do, -1 error. */
static int preerase_step(void) {
    unsigned p = g.cursor;
    for (unsigned k = MEDIA_DIR_PAGES; k < g.npages; k++, p = next_page(p)) {
        if (g.used[p] || g.erased[p] == 0xFFFFu) continue;
        if (g.erased[p] == 0) {
            const int v = verify_blank(p * PAGE, PAGE);
            if (v < 0) return -1;
            if (v == 0 && fl_erase(p * PAGE, PAGE)) return -1;
            g.erased[p] = 0xFFFFu;
            return 1;
        }
        for (unsigned s = 0; s < SPP; s++)
            if (!((g.erased[p] >> s) & 1u)) return preerase_sector(p, s);
    }
    const uint16_t todo = fill_todo();
    for (unsigned s = 0; s < SPP; s++)
        if ((todo >> s) & 1u) return preerase_sector(g.fill, s);
    return 0;
}

static bool compact_needed(void) {
    return g.compact_dirty && (g.compact_wanted || free_pages() < COMPACT_BELOW);
}

/* A directory snapshot that pays, for media_maintain. Not while an upload
 * is in progress, and not again after one failed until the directory has
 * changed (else a flash that fails would be erased on every call).
 * - The dead records are many: at least DIR_IDLE_DEAD_MIN, and
 *   DIR_IDLE_DEAD_RATIO per live entry (mount and the ordinal walks read
 *   them all).
 * - Or the half is nearly full, the next appends would snapshot inside a
 *   put or delete, and a snapshot wins something: by record slots with at
 *   least DIR_IDLE_DEAD_MIN dead records, by bytes with at least
 *   DIR_IDLE_GAIN of dead records. (With close to 1000 live entries a half
 *   has few slots to win: there the snapshot stays in the put.)
 * A snapshot leaves no dead record, so two idle snapshots are at least
 * DIR_IDLE_DEAD_MIN dead records (32 deletes, 64 replaces) or
 * DIR_IDLE_GAIN bytes of dead records apart. */
static bool dir_snapshot_due(void) {
    const unsigned dead = (unsigned)g.nrec - g.nidx;
    if (g.idle_failed || dead == 0) return false;
    for (int i = 0; i < MEDIA_MAX_WRITERS; i++)
        if (g.writers[i].used) return false;
    if (dead >= DIR_IDLE_DEAD_MIN &&
        (dead >= DIR_IDLE_DEAD_RATIO * g.nidx || g.nrec + DIR_IDLE_TAIL_RECORDS > DIR_MAX_RECORDS))
        return true;
    return g.end + DIR_IDLE_TAIL > HALF && g.end - HDR_LEN - g.live_bytes >= DIR_IDLE_GAIN;
}

static bool work_pending(void) {
    if (g.stale || dir_snapshot_due() || compact_needed() || fill_todo()) return true;
    for (unsigned p = MEDIA_DIR_PAGES; p < g.npages; p++)
        if (!g.used[p] && g.erased[p] != 0xFFFFu) return true;
    return false;
}

bool media_maintain(void) {
    if (!g.mounted) return false;
    lock();
    if (!dir_ok() || g.unsettled) {
        /* nothing in the background until a put or delete has brought a
         * snapshot through: the free map is not to be trusted, and a retry
         * here would erase a directory half on every call */
        unlock();
        return false;
    }
    int did = 0;
    if (g.stale) {
        const uint8_t other = (uint8_t)(g.active ^ 1u);
        const int v = verify_blank(other * HALF, HALF);
        if (v < 0 || (v == 0 && fl_erase(other * HALF, HALF))) {
            did = -1;
        } else {
            g.half_erased[other] = true;
            g.stale = false;
            did = 1;
        }
    }
    if (did == 0 && dir_snapshot_due()) {
        /* the other half is blank by now (above): no erase here; the old
         * half is erased by the next call */
        did = dir_snapshot(NULL) == MEDIA_OK ? 1 : -1;
        if (did < 0) g.idle_failed = true;
    }
    if (did == 0 && compact_needed()) {
        did = compact_step();
        if (did == 0) g.compact_wanted = false; /* nothing left to win */
    }
    if (did == 0) did = preerase_step();
    const bool pending = did >= 0 && work_pending();
    unlock();
    return pending;
}
