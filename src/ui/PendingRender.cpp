//
// PendingRender: see PendingRender.h.
//

#include "PendingRender.h"

#include <atomic>

namespace freebutton::ui {

namespace {

// Head of the list. Only accessed with the LVGL lock held.
PendingRender* g_head = nullptr;

// Set by writers after they stored new text; cleared by the LVGL task before
// it walks the list, so a write that lands during the walk is seen next tick.
std::atomic<bool> g_requested{false};

// requestPendingRenderLater(): calls of applyPendingRenders() left until the
// widgets are walked again (0 = none pending). LVGL task only. The LVGL loop
// calls it every few milliseconds, so this is a fraction of a second.
constexpr int kRetryAfterCalls = 50;
int g_retryIn = 0;

} // namespace

// Both walk the list (a few dozen widgets at most, and only when a widget is
// created, redrawn or deleted) instead of keeping a per-widget "linked" flag.
// Appended at the tail: the walk then goes in link (config) order, so
// widgets built when a page is shown stack the way the config lists them.
void PendingRender::linkPending() {
    PendingRender** p = &g_head;
    for (; *p; p = &(*p)->nextPending_) {
        if (*p == this) {
            return;
        }
    }
    nextPending_ = nullptr;
    *p = this;
}

void PendingRender::unlinkPending() {
    for (PendingRender** p = &g_head; *p; p = &(*p)->nextPending_) {
        if (*p == this) {
            *p = nextPending_;
            nextPending_ = nullptr;
            return;
        }
    }
}

void requestPendingRender() {
    g_requested.store(true, std::memory_order_release);
}

void requestPendingRenderLater() {
    if (g_retryIn == 0) {
        g_retryIn = kRetryAfterCalls;
    }
}

void applyPendingRenders() {
    const bool retryNow = g_retryIn > 0 && --g_retryIn == 0;
    if (!g_requested.exchange(false, std::memory_order_acq_rel) && !retryNow) {
        return;
    }
    g_retryIn = 0;  // this walk is the retry; a widget asks again if it still fails
    bool again = false;
    // Release first, then build (PendingRender::shownNow). Both passes keep
    // the list (config) order.
    for (PendingRender* r = g_head; r; r = r->nextPending_) {
        if (!r->shownNow()) {
            again |= r->applyPending();
        }
    }
    for (PendingRender* r = g_head; r; r = r->nextPending_) {
        if (r->shownNow()) {
            again |= r->applyPending();
        }
    }
    if (again) {
        g_requested.store(true, std::memory_order_release);
    }
}

} // namespace freebutton::ui
