/*
 * fb-emulator.wasm: the FreeButton script engine as the device runs it, for
 * the web IDE's virtual device (docs/EMULATOR.md).
 *
 * This file is the part of src/scripting/ScriptEngine.cpp that matters to a
 * script: one context on a fixed heap, bytecode loaded BEFORE the fb tree is
 * built, the loop() pass (queued MQTT events, then one timer), the restart.
 * The JavaScript glue is the firmware's own: mquickjs_stdlib.c is included
 * below, unchanged, so that this file can read its tables (timers) for
 * emu.stats() and see the exceptions the glue swallows.
 *
 * Built by tools/js/build_emulator_wasm.sh. Host: web/src/js/emulator/.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "mquickjs.h"
#include "mquickjs_ext.h"
#include "emu.h"
#include "fbh.h"

/* --- script errors ---------------------------------------------------------
 * The glue fetches the exception of a failed timer / MQTT / sensor callback
 * with JS_GetException() and at most logs it. Routed through here, the host
 * gets every one of them with its stack ('error' event). */
FBH_IMPORT(fbh_script_error)
void fbh_script_error(int where, const char *message, size_t message_len, const char *stack, size_t stack_len);

enum { FBE_ERR_COMPILE = 0, FBE_ERR_LOAD = 1, FBE_ERR_RUN = 2, FBE_ERR_CALLBACK = 3, FBE_ERR_KEY = 4, FBE_ERR_EVAL = 5 };

static char last_error[512];
static char last_stack[768];

static void copy_text(char *out, size_t cap, const char *text, size_t len)
{
    if (len >= cap)
        len = cap - 1;
    memcpy(out, text, len);
    out[len] = '\0';
}

static char dump_text[1024];
static size_t dump_len;

static void dump_write(void *opaque, const void *buf, size_t len)
{
    (void)opaque;
    if (dump_len + len >= sizeof(dump_text))
        len = sizeof(dump_text) - 1 - dump_len;
    memcpy(dump_text + dump_len, buf, len);
    dump_len += len;
}

/* Text and stack of `exception` into last_error / last_stack, and to the
 * host. Returns the exception (it may have moved: the GC moves objects). */
static JSValue report_exception(JSContext *ctx, JSValue exception, int where)
{
    JSGCRef ref;
    JSValue *held = JS_PushGCRef(ctx, &ref);
    *held = exception;

    last_error[0] = '\0';
    last_stack[0] = '\0';
    {
        JSCStringBuf buf;
        size_t len = 0;
        const char *text = JS_ToCStringLen(ctx, &len, *held, &buf);
        if (text)
            copy_text(last_error, sizeof(last_error), text, len);
        else
            (void)JS_GetException(ctx);
    }
    /* The compile context has no prototypes to call toString() on: the
     * engine's own dump of the value ("SyntaxError: ...", then the stack). */
    if (last_error[0] == '\0' && where == FBE_ERR_COMPILE) {
        dump_len = 0;
        JS_SetLogFunc(ctx, dump_write);
        JS_PrintValueF(ctx, *held, JS_DUMP_LONG);
        dump_text[dump_len] = '\0';
        char *newline = strchr(dump_text, '\n');
        if (newline) {
            *newline = '\0';
            copy_text(last_stack, sizeof(last_stack), newline + 1, strlen(newline + 1));
        }
        /* The dump names the class "Error" (no class names in this
         * context either); the parser only throws SyntaxError. */
        if (strncmp(dump_text, "Error: ", 7) == 0)
            snprintf(last_error, sizeof(last_error), "SyntaxError: %s", dump_text + 7);
        else
            copy_text(last_error, sizeof(last_error), dump_text, strlen(dump_text));
        if (last_error[0] == '\0')
            strcpy(last_error, "Compilation failed");
        fbh_script_error(where, last_error, strlen(last_error), last_stack, strlen(last_stack));
        return JS_PopGCRef(ctx, &ref);
    }
    if (JS_IsError(ctx, *held)) {
        JSValue stack = JS_GetPropertyStr(ctx, *held, "stack");
        if (JS_IsString(ctx, stack)) {
            JSCStringBuf buf;
            size_t len = 0;
            const char *text = JS_ToCStringLen(ctx, &len, stack, &buf);
            if (text)
                copy_text(last_stack, sizeof(last_stack), text, len);
        } else if (JS_IsException(stack)) {
            (void)JS_GetException(ctx);
        }
    }
    if (last_error[0] == '\0')
        strcpy(last_error, "exception");
    fbh_script_error(where, last_error, strlen(last_error), last_stack, strlen(last_stack));
    return JS_PopGCRef(ctx, &ref);
}

static JSValue fbe_glue_exception(JSContext *ctx)
{
    return report_exception(ctx, JS_GetException(ctx), FBE_ERR_CALLBACK);
}

/* --- the firmware's JavaScript glue, unchanged ------------------------------ */
#define JS_GetException fbe_glue_exception
#include "scripting/mquickjs_stdlib.c"
#undef JS_GetException

#include "scripting/device_hardware.h"

/* --- clock ------------------------------------------------------------------
 * Everything in the glue that reads time (Date.now, setTimeout,
 * JS_ProcessTimers, performance.now) uses gettimeofday(): the host's clock. */
int gettimeofday(struct timeval *restrict tv, void *restrict tz)
{
    (void)tz;
    const double ms = fbh_now_ms();
    const int64_t whole = (int64_t)ms;
    tv->tv_sec = (time_t)(whole / 1000);
    tv->tv_usec = (suseconds_t)((whole % 1000) * 1000);
    return 0;
}

/* The device booted when the module started: millis() and the FreeRTOS tick
 * count from there (system.uptime(), the firmware's rate limits). */
static double boot_ms = -1;

unsigned long millis(void)
{
    const double now = fbh_now_ms();
    if (boot_ms < 0)
        boot_ms = now;
    return (unsigned long)(int64_t)(now - boot_ms);
}

uint32_t xTaskGetTickCount(void)
{
    return (uint32_t)millis();
}

/* --- logging ----------------------------------------------------------------- */
void fbe_log(int level, const char *tag, const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0)
        n = 0;
    if ((size_t)n >= sizeof(line))
        n = sizeof(line) - 1;
    fbh_log(level, tag, line, (size_t)n);
}

static void console_write(const char *text, size_t len)
{
    fbh_console(text, len);
}

/* The engine's own output (JS_SetLogFunc): the device sends it to the UART. */
static void engine_log(void *opaque, const void *buf, size_t len)
{
    (void)opaque;
    fbh_log('J', "mquickjs", (const char *)buf, len);
}

/* --- runaway scripts ----------------------------------------------------------
 * The device has no interrupt handler: a script that never returns blocks the
 * loop task (and the task watchdog reports it). A page cannot afford that, so
 * the host gives every entry into the engine a time budget; when it is used
 * up the engine throws InternalError "interrupted". A script that catches it
 * and carries on gets interrupted again; after kMaxInterrupts the instance
 * traps and the host discards it. */
enum { kMaxInterrupts = 64 };
static int interrupt_count;

static int interrupt_handler(JSContext *ctx, void *opaque)
{
    (void)ctx;
    (void)opaque;
    if (!fbh_interrupt())
        return 0;
    if (++interrupt_count > kMaxInterrupts)
        __builtin_trap();
    return 1;
}

/* --- engine --------------------------------------------------------------------- */
static uint8_t *heap_buffer;
static uint32_t heap_size;
static JSContext *ctx;
static bool device_api_ready;
/* The image the context executes in place: outside the JS heap, as the
 * device's is in flash. Freed only after the context. */
static uint8_t *running_image;
/* Output of fbe_compile(). */
static uint8_t *compiled_image;
static uint32_t compiled_len;
static char eval_result[512];

static bool create_context(void)
{
    device_api_ready = false;
    ctx = JS_NewContext(heap_buffer, heap_size, &js_stdlib);
    if (!ctx)
        return false;
    JS_SetLogFunc(ctx, engine_log);
    JS_SetConsoleWriteFunc(console_write);
    JS_SetInterruptHandler(ctx, interrupt_handler);
    return true;
}

static void init_device_api(void)
{
    if (!ctx || device_api_ready)
        return;
    device_hw_init_fb(ctx);
    device_api_ready = true;
}

/* ScriptEngine::restart() up to the new context: registrations, context,
 * image, then what the script changed in the device state. */
static void free_context(void)
{
    if (ctx) {
        js_bindings_reset(ctx);
        JS_FreeContext(ctx);
        ctx = NULL;
        device_api_ready = false;
    }
    free(running_image);
    running_image = NULL;
}

/* Allocates the JS heap (the device: 16 KB, src/main.cpp). 0 = ok. */
FBE_EXPORT(fbe_init) int fbe_init(uint32_t heap_bytes)
{
    free_context();
    free(heap_buffer);
    heap_size = heap_bytes & ~7u;
    heap_buffer = aligned_alloc(8, heap_size);
    return heap_buffer ? 0 : -1;
}

FBE_EXPORT(fbe_alloc) void *fbe_alloc(uint32_t size) { return malloc(size ? size : 1); }
FBE_EXPORT(fbe_free) void fbe_free(void *p) { free(p); }

FBE_EXPORT(fbe_error) const char *fbe_error(void) { return last_error; }
FBE_EXPORT(fbe_error_stack) const char *fbe_error_stack(void) { return last_stack; }

/* Source -> position-independent bytecode image (header + data, base 0), in
 * a throwaway context as lib/mquickjs/emscripten_wrapper.c does for the IDE's
 * compiler. Returns the image size, -1 on a syntax error (fbe_error()). */
FBE_EXPORT(fbe_compile) int fbe_compile(const char *source, uint32_t len)
{
    static uint8_t mem[128 * 1024] __attribute__((aligned(8)));
    JSContext *c = JS_NewContext2(mem, sizeof(mem), &js_stdlib, 1);
    JSBytecodeHeader hdr;
    const uint8_t *data;
    uint32_t data_len;

    interrupt_count = 0;
    JSValue code = JS_Parse(c, source, len, "<input>", 0);
    if (JS_IsException(code)) {
        report_exception(c, JS_GetException(c), FBE_ERR_COMPILE);
        JS_FreeContext(c);
        return -1;
    }
    JS_PrepareBytecode(c, &hdr, &data, &data_len, code);
    JS_RelocateBytecode2(c, &hdr, (uint8_t *)data, data_len, 0, 0);

    free(compiled_image);
    compiled_len = sizeof(hdr) + data_len;
    compiled_image = malloc(compiled_len);
    if (!compiled_image) {
        JS_FreeContext(c);
        strcpy(last_error, "out of memory for the bytecode image");
        return -1;
    }
    memcpy(compiled_image, &hdr, sizeof(hdr));
    memcpy(compiled_image + sizeof(hdr), data, data_len);
    JS_FreeContext(c);
    return (int)compiled_len;
}

FBE_EXPORT(fbe_compiled_image) const uint8_t *fbe_compiled_image(void) { return compiled_image; }

/* ROM atoms identity (JS_StdlibAtomsId), as the IDE's compiler and
 * /api/bytecode/next_address report it. */
FBE_EXPORT(fbe_stdlib_id) uint32_t fbe_stdlib_id(void) { return JS_StdlibAtomsId(&js_stdlib); }

/*
 * Stops what runs, then runs `image` (len bytes, base 0; copied) as the boot
 * script, or only builds fb when image is NULL (the device without a script).
 * ScriptEngine::restart() + startUserScript() + runBootScript().
 * restore != 0: ConfigManager::restoreConfigState() first, as a restart does.
 *
 * Returns 0, or: -1 no engine, -2 not bytecode / relocation failed,
 * -3 load failed, -4 the script threw (the context stays, with what the
 * script registered before the error, as on the device).
 */
static int start_script(const uint8_t *image, uint32_t len, int restore)
{
    last_error[0] = '\0';
    last_stack[0] = '\0';
    interrupt_count = 0;
    if (!heap_buffer) {
        strcpy(last_error, "no engine");
        return -1;
    }
    free_context();
    /* The MQTT task takes the old script's subscriptions off the brokers
     * before the new script makes its own (which then get their retained
     * messages again, docs/SCRIPT-RESTART.md). */
    fbe_mqtt_pump();
    if (restore)
        fbe_device_restore_config_state();
    if (!create_context()) {
        strcpy(last_error, "no engine");
        return -1;
    }
    if (!image) {
        init_device_api();
        return 0;
    }

    running_image = malloc(len ? len : 1);
    if (!running_image) {
        strcpy(last_error, "map failed");
        init_device_api();
        return -2;
    }
    memcpy(running_image, image, len);
    if (!JS_IsBytecode(running_image, len) || JS_RelocateBytecode(ctx, running_image, len) != 0) {
        strcpy(last_error, "relocation mismatch");
        init_device_api();
        return -2;
    }

    /* Bytecode first: fb creates RAM atoms, which would stop the load. */
    JSValue main_func = JS_LoadBytecode(ctx, running_image);
    if (JS_IsException(main_func)) {
        report_exception(ctx, JS_GetException(ctx), FBE_ERR_LOAD);
        init_device_api();
        return -3;
    }
    init_device_api();

    JSValue result = JS_Run(ctx, main_func);
    if (JS_IsException(result)) {
        report_exception(ctx, JS_GetException(ctx), FBE_ERR_RUN);
        return -4;
    }
    return 0;
}

FBE_EXPORT(fbe_start) int fbe_start(const uint8_t *image, uint32_t len, int restore)
{
    const int rc = start_script(image, len, restore);
    fbe_device_after_entry();
    return rc;
}

/* The script stops: no handlers, timers or subscriptions; no context. The
 * device state stays as the script left it, and the device runs on. */
FBE_EXPORT(fbe_stop) void fbe_stop(void)
{
    free_context();
    fbe_device_after_entry();
}

FBE_EXPORT(fbe_running) int fbe_running(void) { return ctx != NULL; }

/*
 * One pass of the firmware's loop(): the queued MQTT events (at most
 * kMaxRecordsPerPass), then one due timer. Returns the milliseconds until
 * the next pass is needed: 0 = work is left, -1 = nothing pending.
 */
FBE_EXPORT(fbe_loop_pass) int fbe_loop_pass(void)
{
    interrupt_count = 0;
    /* The firmware's other work of a pass (device.cpp). */
    int more = fbe_device_pass();
    int64_t delay = -1;
    if (ctx) {
        more |= fbe_mqtt_process_pending();
        delay = JS_ProcessTimers(ctx);
    }
    fbe_device_after_entry();
    if (more)
        return 0;
    return delay < 0 ? -1 : (int)delay;
}

/* seam_button.cpp, after a key handler ran: the glue leaves its exception pending. */
void fbe_report_pending_exception(void)
{
    if (!ctx)
        return;
    JSValue exception = JS_GetException(ctx);
    if (exception != JS_UNINITIALIZED)
        report_exception(ctx, exception, FBE_ERR_KEY);
}

void fbe_enter_engine(void) { interrupt_count = 0; }

/* A REPL line (ScriptEngine::execute). 0 = ok, text of the value in
 * fbe_eval_result(); -1 = exception (fbe_error()). */
FBE_EXPORT(fbe_eval) int fbe_eval(const char *source, uint32_t len)
{
    eval_result[0] = '\0';
    if (!ctx) {
        strcpy(last_error, "Script engine not initialized");
        return -1;
    }
    interrupt_count = 0;
    JSValue value = JS_Eval(ctx, source, len, "<repl>", JS_EVAL_RETVAL);
    if (JS_IsException(value)) {
        report_exception(ctx, JS_GetException(ctx), FBE_ERR_EVAL);
        fbe_device_after_entry();
        return -1;
    }
    JSCStringBuf buf;
    size_t n = 0;
    const char *text = JS_ToCStringLen(ctx, &n, value, &buf);
    if (text)
        copy_text(eval_result, sizeof(eval_result), text, n);
    else
        (void)JS_GetException(ctx);
    fbe_device_after_entry();
    return 0;
}

FBE_EXPORT(fbe_eval_result) const char *fbe_eval_result(void) { return eval_result; }

/* ScriptEngine::measureHeap(), for the firmware's JS memory sensors. */
void fbe_js_heap(size_t *used, size_t *free_bytes)
{
    *used = 0;
    *free_bytes = 0;
    if (ctx) {
        JS_GC(ctx);
        JS_GetMemoryUsage(ctx, used, free_bytes);
    }
}

/*
 * out[0..9]: JS heap used, free, size (ScriptEngine::measureHeap: after a GC;
 * /api/device jsHeapUsed / jsHeapFree / jsHeapSize), timers pending, timer
 * slots, MQTT events dropped, MQTT queue bytes used, queue size, JS
 * subscriptions, running.
 */
FBE_EXPORT(fbe_stats) void fbe_stats(uint32_t *out)
{
    size_t used = 0, free_bytes = 0;
    int timers = 0, subscriptions = 0;
    if (ctx) {
        JS_GC(ctx);
        JS_GetMemoryUsage(ctx, &used, &free_bytes);
    }
    for (int i = 0; i < MAX_TIMERS; i++)
        timers += js_timer_list[i].allocated ? 1 : 0;
    for (int b = 0; b < MAX_BROKERS; b++)
        for (int i = 0; i < MAX_SUBSCRIPTIONS_PER_BROKER; i++)
            subscriptions += js_mqtt_brokers[b].subscriptions[i].allocated ? 1 : 0;
    out[0] = (uint32_t)used;
    out[1] = (uint32_t)free_bytes;
    out[2] = heap_size;
    out[3] = (uint32_t)timers;
    out[4] = MAX_TIMERS;
    out[5] = fbe_mqtt_dropped();
    out[6] = fbe_mqtt_queue_used();
    out[7] = fbe_mqtt_queue_bytes();
    out[8] = (uint32_t)subscriptions;
    out[9] = ctx != NULL;
}

/* The firmware's limits, for the host's UI and tests (never to enforce them:
 * the glue above does that). */
FBE_EXPORT(fbe_limit) int fbe_limit(int which)
{
    switch (which) {
    case 0: return MAX_BROKERS;
    case 1: return MAX_SUBSCRIPTIONS_PER_BROKER;
    case 2: return MAX_TIMERS;
    case 3: return MAX_BUTTONS;
    case 4: return MAX_SENSORS;
    case 5: return MAX_DISPLAY_ITEMS;
    case 6: return MQTT_MAX_TOPIC_LEN;
    case 7: return MQTT_MAX_PAYLOAD_LEN;
    case 8: return (int)fbe_mqtt_queue_bytes();
    case 9: return FB_MQTT_BUFFER_SIZE;
    case 10: return FB_BUTTON_UPDATE_INTERVAL_MS;
    case 11: return FB_LOOP_DELAY_MS;
    case 12: return MAX_JS_SOURCE_SIZE;
    case 13: return MAX_JS_BYTECODE_SIZE;
    case 14: return FB_LED_COUNT;
    default: return -1;
    }
}

/* MQTT topic filter match, the firmware's (mqtt_topic_match.h), for the
 * host's mock broker. filter is NUL-terminated. */
FBE_EXPORT(fbe_topic_matches) int fbe_topic_matches(const char *filter, const char *topic, uint32_t topic_len)
{
    return mqtt_topic_matches(filter, topic, topic_len);
}
