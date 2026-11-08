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

} // namespace

// Both walk the list (a few dozen widgets at most, and only when a widget is
// created, redrawn or deleted) instead of keeping a per-widget "linked" flag.
void PendingRender::linkPending() {
    for (const PendingRender* r = g_head; r; r = r->nextPending_) {
        if (r == this) {
            return;
        }
    }
    nextPending_ = g_head;
    g_head = this;
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

void applyPendingRenders() {
    if (!g_requested.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    bool again = false;
    for (PendingRender* r = g_head; r; r = r->nextPending_) {
        again |= r->applyPending();
    }
    if (again) {
        g_requested.store(true, std::memory_order_release);
    }
}

} // namespace freebutton::ui
