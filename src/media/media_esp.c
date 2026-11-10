// Media store, ESP-IDF backend: the `media` partition (data, subtype 0x42)
// as a media_flash_t for the core (media_core.c), and media_init().
//
// Mapping: spi_flash_mmap_pages() takes ABSOLUTE 64 KB flash page numbers
// and maps a non-contiguous page list block by block, first-fit in the
// virtual address space; it does not check that the blocks ended up
// adjacent. So after mapping more than one block we check every page with
// spi_flash_cache2phys() and refuse a mapping that is not contiguous.
// The core shares one mapping per shared page (mapping a page twice fails
// with ESP_ERR_INVALID_STATE in spi_flash_mmap_pages).
#ifdef ESP_PLATFORM

#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "media_core.h"
#include "spi_flash_mmap.h"

static const char *TAG = "media";

#define MEDIA_PARTITION_SUBTYPE ((esp_partition_subtype_t)0x42)

static const esp_partition_t *s_part;
static StaticSemaphore_t s_mutex_buf;
static SemaphoreHandle_t s_mutex;
static media_flash_t s_flash;

static int esp_read(void *ctx, uint32_t off, void *dst, uint32_t len) {
    (void)ctx;
    return esp_partition_read(s_part, off, dst, len) == ESP_OK ? 0 : -1;
}

static int esp_write(void *ctx, uint32_t off, const void *src, uint32_t len) {
    (void)ctx;
    const esp_err_t e = esp_partition_write(s_part, off, src, len);
    if (e != ESP_OK) ESP_LOGE(TAG, "write 0x%x+%u: %s", (unsigned)off, (unsigned)len, esp_err_to_name(e));
    return e == ESP_OK ? 0 : -1;
}

static int esp_erase(void *ctx, uint32_t off, uint32_t len) {
    (void)ctx;
    const esp_err_t e = esp_partition_erase_range(s_part, off, len);
    if (e != ESP_OK) ESP_LOGE(TAG, "erase 0x%x+%u: %s", (unsigned)off, (unsigned)len, esp_err_to_name(e));
    return e == ESP_OK ? 0 : -1;
}

static int esp_map(void *ctx, const uint8_t *pages, uint32_t n, const void **out, void **handle) {
    (void)ctx;
    if (n == 0 || n > MEDIA_MAX_FILE_PAGES) return -1;
    int abs_pages[MEDIA_MAX_FILE_PAGES]; /* task stack: internal RAM (no PSRAM) */
    for (uint32_t i = 0; i < n; i++) abs_pages[i] = (int)((s_part->address + pages[i] * MEDIA_PAGE_SIZE) / MEDIA_PAGE_SIZE);
    const void *ptr = NULL;
    spi_flash_mmap_handle_t h = 0;
    const esp_err_t e = spi_flash_mmap_pages(abs_pages, n, SPI_FLASH_MMAP_DATA, &ptr, &h);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "map %u pages: %s", (unsigned)n, esp_err_to_name(e));
        return -1;
    }
    for (uint32_t i = 1; i < n; i++) {
        const size_t phys = spi_flash_cache2phys((const uint8_t *)ptr + i * MEDIA_PAGE_SIZE);
        if (phys != (size_t)abs_pages[i] * MEDIA_PAGE_SIZE) {
            ESP_LOGE(TAG, "map %u pages: virtual range not contiguous (page %u)", (unsigned)n, (unsigned)i);
            spi_flash_munmap(h);
            return -1;
        }
    }
    *out = ptr;
    *handle = (void *)(uintptr_t)h;
    return 0;
}

static void esp_unmap(void *ctx, void *handle) {
    (void)ctx;
    spi_flash_munmap((spi_flash_mmap_handle_t)(uintptr_t)handle);
}

static void esp_lock(void *ctx) {
    (void)ctx;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void esp_unlock(void *ctx) {
    (void)ctx;
    xSemaphoreGive(s_mutex);
}

media_err_t media_init(void) {
    if (s_part) return MEDIA_OK; /* mounted, or tried */
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, MEDIA_PARTITION_SUBTYPE, "media");
    if (!p) {
        ESP_LOGW(TAG, "no media partition");
        return MEDIA_ERR_UNAVAILABLE;
    }
    if (p->address % MEDIA_PAGE_SIZE || p->size % MEDIA_PAGE_SIZE) {
        ESP_LOGE(TAG, "media partition 0x%x+0x%x is not 64 KB aligned", (unsigned)p->address, (unsigned)p->size);
        return MEDIA_ERR_UNAVAILABLE;
    }
    if (!s_mutex) s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buf);
    s_part = p;
    s_flash.ctx = NULL;
    s_flash.size = p->size;
    s_flash.read = esp_read;
    s_flash.write = esp_write;
    s_flash.erase = esp_erase;
    s_flash.map = esp_map;
    s_flash.unmap = esp_unmap;
    s_flash.lock = esp_lock;
    s_flash.unlock = esp_unlock;
    const media_err_t e = media_mount(&s_flash);
    if (e != MEDIA_OK) {
        ESP_LOGE(TAG, "mount failed (%d)", (int)e);
        s_part = NULL;
        return e;
    }
    media_usage_t u;
    media_usage(&u);
    ESP_LOGI(TAG, "media 0x%x+0x%x: %u KB used, %u KB free", (unsigned)p->address, (unsigned)p->size,
             (unsigned)(u.used / 1024), (unsigned)(u.free / 1024));
    return MEDIA_OK;
}

#endif /* ESP_PLATFORM */
