// Heap probe: which allocations are alive when the free heap is at its
// lowest. Diagnostic builds only; a normal build compiles this file to
// nothing.
//
// To use (bench device, USB):
//   1. in sdkconfig.esp32 replace "CONFIG_HEAP_TRACING_OFF=y" and the
//      STANDALONE line by
//          CONFIG_HEAP_TRACING_STANDALONE=y
//          CONFIG_HEAP_TRACING=y
//          CONFIG_HEAP_TRACING_STACK_DEPTH=5
//      (do not commit that; "git checkout sdkconfig.esp32" afterwards)
//   2. PLATFORMIO_BUILD_FLAGS="-D FB_HEAP_PROBE" pio run -e esp32 -t upload
//   3. log the serial output to a file, do what is to be measured
//   4. tools/scripts/heap_probe_decode.py <log>
//
// It records every allocation made after start-up that is not yet freed. A
// task samples the free heap every tick; at each new low it sums the live
// allocations by call site, and prints that table once the heap has been
// quiet for two seconds (then it starts over for the next measurement).
// The probe itself takes about 13 KB of heap (records and its task): the
// device has that much less than normal while it runs. More records or a
// deeper stack leave too little to boot btn_a00001's configuration.
#ifdef FB_HEAP_PROBE
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_heap_trace.h>
#include <cstring>

namespace {

constexpr size_t kRecords = 170;
constexpr size_t kSites = 64;
constexpr size_t kDepth = CONFIG_HEAP_TRACING_STACK_DEPTH;

heap_trace_record_t s_records[kRecords];

struct Site {
    void* callers[kDepth];
    uint32_t bytes;
    uint16_t count;
};
Site s_sites[kSites];
size_t s_siteCount = 0;
uint32_t s_low = 0;
uint32_t s_lowAt = 0;
uint32_t s_liveAtLow = 0;
bool s_dirty = false;

void snapshot() {
    s_siteCount = 0;
    s_liveAtLow = 0;
    const size_t n = heap_trace_get_count();
    for (size_t i = 0; i < n; i++) {
        heap_trace_record_t r;
        if (heap_trace_get(i, &r) != ESP_OK || r.address == nullptr) continue;
        s_liveAtLow += r.size;
        size_t s = 0;
        for (; s < s_siteCount; s++) {
            if (memcmp(s_sites[s].callers, r.alloced_by, sizeof(r.alloced_by)) == 0) break;
        }
        if (s == s_siteCount) {
            if (s_siteCount == kSites) { s = kSites - 1; }   // overflow: lump into the last
            else { memcpy(s_sites[s].callers, r.alloced_by, sizeof(r.alloced_by)); s_sites[s].bytes = 0; s_sites[s].count = 0; s_siteCount++; }
        }
        s_sites[s].bytes += r.size;
        s_sites[s].count++;
    }
}

void report() {
    printf("\nHEAPPROBE low=%u live=%u sites=%u records=%u\n", (unsigned)s_low, (unsigned)s_liveAtLow,
           (unsigned)s_siteCount, (unsigned)heap_trace_get_count());
    for (size_t s = 0; s < s_siteCount; s++) {
        printf("HEAPSITE %u %u", (unsigned)s_sites[s].bytes, (unsigned)s_sites[s].count);
        for (size_t d = 0; d < kDepth; d++) printf(" %p", s_sites[s].callers[d]);
        printf("\n");
    }
    printf("HEAPEND\n");
}

void probeTask(void*) {
    s_low = heap_caps_get_free_size(MALLOC_CAP_DEFAULT) - 3000;
    while (true) {
        const uint32_t free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        if (free + 1024 < s_low) {
            s_low = free;
            s_lowAt = millis();
            snapshot();
            s_dirty = true;
        } else if (s_dirty && millis() - s_lowAt > 2000) {
            report();
            s_dirty = false;
            s_low = heap_caps_get_free_size(MALLOC_CAP_DEFAULT) - 3000;   // next test starts fresh
        }
        vTaskDelay(1);
    }
}

}  // namespace

void heapProbeStart() {
    heap_trace_init_standalone(s_records, kRecords);
    heap_trace_start(HEAP_TRACE_LEAKS);
    xTaskCreatePinnedToCore(probeTask, "heapprobe", 4096, nullptr, 20, nullptr, 0);
}
#endif  // FB_HEAP_PROBE
